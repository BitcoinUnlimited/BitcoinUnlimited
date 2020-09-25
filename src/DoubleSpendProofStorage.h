// Copyright (C) 2019-2020 Tom Zander <tomz@freedommail.ch>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DOUBLESPENDPROOFSTORAGE_H
#define DOUBLESPENDPROOFSTORAGE_H

#include "DoubleSpendProof.h"
#include "bloom.h"
#include "net.h"

#include <boost/asio.hpp>
#include <boost/unordered_map.hpp>

#include <map>
#include <mutex>
#include <set>

class COutPoint;

class DoubleSpendProofStorage
{
public:
    DoubleSpendProofStorage();
    ~DoubleSpendProofStorage();

    /// returns a double spend proof based on proof-id
    DoubleSpendProof proof(int proof) const;
    /// adds a proof, returns an internal proof-id that proof is known under.
    /// notice that if the proof (by hash) was known, that proof-id is returned instead.
    int add(const DoubleSpendProof &proof);
    /// remove by proof-id
    void remove(int proof);

    /// this add()s and additionally registers this is an orphan.
    /// you can fetch those upto 90s using 'claim()'.
    void addOrphan(const DoubleSpendProof &proof, NodeId peerId);
    /// Returns all (not yet verified) orphans matching prevOut.
    /// Each item is a pair of a proofId and the nodeId that send the proof to us
    std::list<std::pair<int, NodeId> > findOrphans(const COutPoint &prevOut);

    //! Returns how many orphans have this proof id
    int orphanCount(int proofId);

    void claimOrphan(int proofId);

    DoubleSpendProof lookup(const uint256 &proofId) const;
    bool exists(const uint256 &proofId) const;

    // called every minute
    void periodicCleanup(const boost::system::error_code &error);

    bool isRecentlyRejectedProof(const uint256 &proofHash) const;
    void markProofRejected(const uint256 &proofHash);
    void newBlockFound();

private:
    // m_lock guards all the following data structures
    mutable std::recursive_mutex m_lock;

    std::map<int, DoubleSpendProof> m_proofs;
    int m_nextId = 1;
    std::map<int, std::pair<NodeId, int64_t> > m_orphans;

    typedef boost::unordered_map<uint256, int, HashShortener> LookupTable;
    LookupTable m_dspIdLookupTable;
    std::map<uint64_t, std::deque<int> > m_prevTxIdLookupTable;

    CRollingBloomFilter m_recentRejects;

    // initialize timer
    boost::asio::deadline_timer m_timer;
};

#endif
