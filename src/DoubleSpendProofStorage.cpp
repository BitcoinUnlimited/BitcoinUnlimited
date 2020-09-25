// Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "DoubleSpendProofStorage.h"
#include "dosman.h"
#include "hashwrapper.h"
#include "util.h"
#include <primitives/transaction.h>
#include <utiltime.h>

#include <limits>

static constexpr int64_t SECONDS_TO_KEEP_ORPHANS = 90;

extern boost::asio::io_service stat_io_service;

DoubleSpendProofStorage::DoubleSpendProofStorage() : m_recentRejects(120000, 0.000001), m_timer(stat_io_service)
{
    m_timer.expires_from_now(boost::posix_time::minutes(2));
    m_timer.async_wait(std::bind(&DoubleSpendProofStorage::periodicCleanup, this, std::placeholders::_1));
}

DoubleSpendProofStorage::~DoubleSpendProofStorage() { m_timer.cancel(); }
DoubleSpendProof DoubleSpendProofStorage::proof(int proof) const
{
    LOCK(m_lock);
    auto iter = m_proofs.find(proof);
    if (iter != m_proofs.end())
        return iter->second;
    return DoubleSpendProof();
}

std::pair<bool, int32_t> DoubleSpendProofStorage::add(const DoubleSpendProof &proof)
{
    LOCK(m_lock);

    uint256 hash = proof.GetHash();
    auto lookupIter = m_dspIdLookupTable.find(hash);
    if (lookupIter != m_dspIdLookupTable.end())
    {
        claimOrphan(lookupIter->second);
        return {false, lookupIter->second};
    }

    auto iter = m_proofs.find(m_nextId);
    while (iter != m_proofs.end())
    {
        if (++m_nextId < 0)
            m_nextId = 1;
        iter = m_proofs.find(m_nextId);
    }
    m_proofs.emplace(m_nextId, proof);
    m_dspIdLookupTable.emplace(hash, m_nextId);

    return {true, m_nextId++};
}

void DoubleSpendProofStorage::addOrphan(const DoubleSpendProof &proof, NodeId peerId)
{
    LOCK(m_lock);
    const auto res = add(proof);
    if (!res.first) // it was already in the storage
        return;

    const int32_t id = res.second;
    m_orphans.emplace(id, std::make_pair(peerId, GetTime()));
    m_prevTxIdLookupTable[proof.prevTxId().GetCheapHash()].push_back(id);
}

std::list<std::pair<int, int> > DoubleSpendProofStorage::findOrphans(const COutPoint &prevOut)
{
    std::list<std::pair<int, int> > answer;
    LOCK(m_lock);
    auto iter = m_prevTxIdLookupTable.find(prevOut.hash.GetCheapHash());
    if (iter == m_prevTxIdLookupTable.end())
        return answer;

    std::deque<int> &q = iter->second;
    for (auto proofId = q.begin(); proofId != q.end(); ++proofId)
    {
        auto proofIter = m_proofs.find(*proofId);
        DbgAssert(proofIter != m_proofs.end(), );
        if (proofIter != m_proofs.end())
        {
            if (proofIter->second.prevOutIndex() != int(prevOut.n))
                continue;
            if (proofIter->second.prevTxId() == prevOut.hash)
            {
                auto orphanIter = m_orphans.find(*proofId);
                if (orphanIter != m_orphans.end())
                {
                    answer.emplace_back(*proofId, orphanIter->second.first);
                }
            }
        }
        else
            LOG(DSPROOF, "ERROR: no dsproofs found in m_proofs\n");
    }
    return answer;
}

int DoubleSpendProofStorage::orphanCount(int proofId) { return m_orphans.count(proofId); }
void DoubleSpendProofStorage::claimOrphan(int proofId)
{
    LOCK(m_lock);
    auto orphan = m_orphans.find(proofId);
    if (orphan != m_orphans.end())
    {
        m_orphans.erase(orphan);

        for (auto iter = m_prevTxIdLookupTable.begin(); iter != m_prevTxIdLookupTable.end(); ++iter)
        {
            auto &list = iter->second;
            for (auto i = list.begin(); i != list.end(); ++i)
            {
                if (*i == proofId)
                {
                    list.erase(i);
                    if (list.size() == 0)
                        m_prevTxIdLookupTable.erase(iter);
                    return;
                }
            }
        }
    }
}

void DoubleSpendProofStorage::remove(int proof)
{
    LOCK(m_lock);
    auto iter = m_proofs.find(proof);
    if (iter == m_proofs.end())
        return;

    auto orphan = m_orphans.find(iter->first);
    if (orphan != m_orphans.end())
    {
        m_orphans.erase(orphan);
        auto orphanLookup = m_prevTxIdLookupTable.find(iter->second.prevTxId().GetCheapHash());
        if (orphanLookup != m_prevTxIdLookupTable.end())
        {
            std::deque<int> &queue = orphanLookup->second;
            if (queue.size() == 1)
            {
                DbgAssert(queue.front() == proof, );
                if (queue.front() != proof)
                    LOG(DSPROOF, "ERROR: queue front did not equal dsproof\n");
                m_prevTxIdLookupTable.erase(orphanLookup);
            }
            else
            {
                for (auto i = queue.begin(); i != queue.end(); ++i)
                {
                    if (*i == proof)
                    {
                        i = queue.erase(i);
                    }
                }
            }
        }
    }
    auto hash = iter->second.GetHash();
    m_dspIdLookupTable.erase(hash);
    m_proofs.erase(iter);
}

DoubleSpendProof DoubleSpendProofStorage::lookup(const uint256 &proofId) const
{
    LOCK(m_lock);
    auto lookupIter = m_dspIdLookupTable.find(proofId);
    if (lookupIter == m_dspIdLookupTable.end())
        return DoubleSpendProof();
    return m_proofs.at(lookupIter->second);
}

bool DoubleSpendProofStorage::exists(const uint256 &proofId) const
{
    LOCK(m_lock);
    return m_dspIdLookupTable.find(proofId) != m_dspIdLookupTable.end();
}

void DoubleSpendProofStorage::periodicCleanup(const boost::system::error_code &error)
{
    if (error)
        return;
    m_timer.expires_from_now(boost::posix_time::minutes(1));
    m_timer.async_wait(std::bind(&DoubleSpendProofStorage::periodicCleanup, this, std::placeholders::_1));

    LOCK(m_lock);
    auto expire = GetTime() - SECONDS_TO_KEEP_ORPHANS;
    auto iter = m_orphans.begin();
    while (iter != m_orphans.end())
    {
        if (iter->second.second <= expire)
        {
            const int peerId = iter->second.first;
            const int proofId = iter->first;
            iter = m_orphans.erase(iter);
            remove(proofId);

            dosMan.Misbehaving(peerId, 1);
        }
        else
        {
            ++iter;
        }
    }
    LOG(DSPROOF, "DSP orphan count: %d DSProof count: %d\n", m_orphans.size(), m_proofs.size());
}

bool DoubleSpendProofStorage::isRecentlyRejectedProof(const uint256 &proofHash) const
{
    LOCK(m_lock);
    return m_recentRejects.contains(proofHash);
}

void DoubleSpendProofStorage::markProofRejected(const uint256 &proofHash)
{
    LOCK(m_lock);
    m_recentRejects.insert(proofHash);
}

void DoubleSpendProofStorage::newBlockFound()
{
    LOCK(m_lock);
    m_recentRejects.reset();
}

DoubleSpendProofStorage::SaltedHasher::SaltedHasher()
    : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max()))
{
}

size_t DoubleSpendProofStorage::SaltedHasher::operator()(const uint256 &hash) const
{
    return SipHashUint256(k0, k1, hash);
}
