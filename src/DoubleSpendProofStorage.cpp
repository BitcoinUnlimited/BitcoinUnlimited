// Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "DoubleSpendProofStorage.h"
#include <utiltime.h>
#include <primitives/transaction.h>
#include "hashwrapper.h"
#include "util.h"
#include "dosman.h"

#define SECONDS_TO_KEEP_ORPHANS 90

extern boost::asio::io_service stat_io_service;

DoubleSpendProofStorage::DoubleSpendProofStorage()
    : m_recentRejects(120000, 0.000001),
    m_timer(stat_io_service)
{
    m_timer.expires_from_now(boost::posix_time::minutes(2));
    m_timer.async_wait(std::bind(&DoubleSpendProofStorage::periodicCleanup, this, std::placeholders::_1));
}

DoubleSpendProofStorage::~DoubleSpendProofStorage()
{
    m_timer.cancel();
}

DoubleSpendProof DoubleSpendProofStorage::proof(int proof) const
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto iter = m_proofs.find(proof);
    if (iter != m_proofs.end())
        return iter->second;
    return DoubleSpendProof();
}

int DoubleSpendProofStorage::add(const DoubleSpendProof &proof)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);

    uint256 hash = proof.createHash();
    auto lookupIter = m_dspIdLookupTable.find(hash);
    if (lookupIter != m_dspIdLookupTable.end())
        return lookupIter->second;

    auto iter = m_proofs.find(m_nextId);
    while (iter != m_proofs.end()) {
        if (++m_nextId < 0)
            m_nextId = 1;
        iter = m_proofs.find(m_nextId);
    }
    m_proofs.insert(std::make_pair(m_nextId, proof));
    m_dspIdLookupTable.insert(std::make_pair(hash, m_nextId));

    return m_nextId++;
}

void DoubleSpendProofStorage::addOrphan(const DoubleSpendProof &proof, int peerId)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    const int next = m_nextId;
    const int id = add(proof);
    if (id != next) // it was already in the storage
        return;

    m_orphans.insert(std::make_pair(id, std::make_pair(peerId, GetTime())));
    m_prevTxIdLookupTable[proof.prevTxId().GetCheapHash()].push_back(id);
}

std::list<std::pair<int, int>> DoubleSpendProofStorage::findOrphans(const COutPoint &prevOut)
{
    std::list<std::pair<int, int>> answer;
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto iter = m_prevTxIdLookupTable.find(prevOut.hash.GetCheapHash());
    if (iter == m_prevTxIdLookupTable.end())
        return answer;

    std::deque<int> &q = iter->second;
    for (auto proofId = q.begin(); proofId != q.end(); ++proofId) {
        auto proofIter = m_proofs.find(*proofId);
        assert (proofIter != m_proofs.end());
        if (proofIter->second.prevOutIndex() != int(prevOut.n))
            continue;
        if (proofIter->second.prevTxId() == prevOut.hash) {
            auto orphanIter = m_orphans.find(*proofId);
            if (orphanIter != m_orphans.end()) {
                answer.push_back(std::make_pair(*proofId, orphanIter->second.first));
            }
        }
    }
    return answer;
}

void DoubleSpendProofStorage::claimOrphan(int proofId)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto orphan = m_orphans.find(proofId);
    if (orphan != m_orphans.end()) {
        m_orphans.erase(orphan);

        for (auto iter = m_prevTxIdLookupTable.begin(); iter != m_prevTxIdLookupTable.end(); ++iter) {
            auto &list = iter->second;
            for (auto i = list.begin(); i != list.end(); ++i) {
                if (*i == proofId) {
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
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto iter = m_proofs.find(proof);
    if (iter == m_proofs.end())
        return;

    auto orphan = m_orphans.find(iter->first);
    if (orphan != m_orphans.end()) {
        m_orphans.erase(orphan);
        auto orphanLookup = m_prevTxIdLookupTable.find(iter->second.prevTxId().GetCheapHash());
        if (orphanLookup != m_prevTxIdLookupTable.end()) {
            std::deque<int> &queue = orphanLookup->second;
            if (queue.size() == 1) {
                assert(queue.front() == proof);
                m_prevTxIdLookupTable.erase(orphanLookup);
            }
            else {
                for (auto i = queue.begin(); i != queue.end(); ++i) {
                    if (*i == proof) {
                        queue.erase(i);
                        break;
                    }
                }
            }
        }
    }
    auto hash = iter->second.createHash();
    m_dspIdLookupTable.erase(hash);
    m_proofs.erase(iter);
}

DoubleSpendProof DoubleSpendProofStorage::lookup(const uint256 &proofId) const
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto lookupIter = m_dspIdLookupTable.find(proofId);
    if (lookupIter == m_dspIdLookupTable.end())
        return DoubleSpendProof();
    return m_proofs.at(lookupIter->second);
}

bool DoubleSpendProofStorage::exists(const uint256 &proofId) const
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    return m_dspIdLookupTable.find(proofId) != m_dspIdLookupTable.end();
}

void DoubleSpendProofStorage::periodicCleanup(const boost::system::error_code &error)
{
    if (error)
        return;
    m_timer.expires_from_now(boost::posix_time::minutes(1));
    m_timer.async_wait(std::bind(&DoubleSpendProofStorage::periodicCleanup, this, std::placeholders::_1));

    std::lock_guard<std::recursive_mutex> lock(m_lock);
    auto expire = GetTime() - SECONDS_TO_KEEP_ORPHANS;
    auto iter = m_orphans.begin();
    while (iter != m_orphans.end()) {
        if (iter->second.second <= expire) {
            const int peerId = iter->second.first;
            const int proofId = iter->first;
            iter = m_orphans.erase(iter);
            remove(proofId);

            dosMan.Misbehaving(peerId, 1);
        }
        else {
            ++iter;
        }
    }
    LOG(DSPROOF, "DSP orphan count: %d DSProof count: %d\n", m_orphans.size(), m_proofs.size());
}

bool DoubleSpendProofStorage::isRecentlyRejectedProof(const uint256 &proofHash) const
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    return m_recentRejects.contains(proofHash);
}

void DoubleSpendProofStorage::markProofRejected(const uint256 &proofHash)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    m_recentRejects.insert(proofHash);
}

void DoubleSpendProofStorage::newBlockFound()
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    m_recentRejects.reset();
}
