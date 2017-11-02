// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txmempool.h"

#include "clientversion.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "main.h"
#include "policy/fees.h"
#include "streams.h"
#include "timedata.h"
#include "unlimited.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "version.h"

using namespace std;
CTxMemPoolEntry::CTxMemPoolEntry()
    : tx(), nFee(), nTime(0), entryPriority(0), entryHeight(0), hadNoDependencies(0), inChainInputValue(0),
      spendsCoinbase(false), sigOpCount(0), lockPoints()
{
    nTxSize = 0;
    nModSize = 0;
    nUsageSize = 0;
    nCountWithDescendants = 0;
    feeDelta = 0;
    sighashType = 0;
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTransaction &_tx,
    const CAmount &_nFee,
    int64_t _nTime,
    double _entryPriority,
    unsigned int _entryHeight,
    bool poolHasNoInputsOf,
    CAmount _inChainInputValue,
    bool _spendsCoinbase,
    unsigned int _sigOps,
    LockPoints lp)
    : tx(_tx), nFee(_nFee), nTime(_nTime), entryPriority(_entryPriority), entryHeight(_entryHeight),
      hadNoDependencies(poolHasNoInputsOf), inChainInputValue(_inChainInputValue), spendsCoinbase(_spendsCoinbase),
      sigOpCount(_sigOps), lockPoints(lp)
{
    nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    nModSize = tx.CalculateModifiedSize(nTxSize);
    nUsageSize = RecursiveDynamicUsage(tx);

    nCountWithDescendants = 1;
    nSizeWithDescendants = nTxSize;
    nModFeesWithDescendants = nFee;
    CAmount nValueIn = tx.GetValueOut() + nFee;
    assert(inChainInputValue <= nValueIn);
    sighashType = 0;
    feeDelta = 0;
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTxMemPoolEntry &other) { *this = other; }
double CTxMemPoolEntry::GetPriority(unsigned int currentHeight) const
{
    double deltaPriority = ((double)(currentHeight - entryHeight) * inChainInputValue) / nModSize;
    double dResult = entryPriority + deltaPriority;
    if (dResult < 0) // This should only happen if it was called with a height below entry height
        dResult = 0;
    return dResult;
}

void CTxMemPoolEntry::UpdateFeeDelta(int64_t newFeeDelta)
{
    nModFeesWithDescendants += newFeeDelta - feeDelta;
    feeDelta = newFeeDelta;
}

void CTxMemPoolEntry::UpdateLockPoints(const LockPoints &lp) { lockPoints = lp; }
void CTxMemPoolEntry::UpdateRuntimeSigOps(uint64_t _runtimeSigOpCount, uint64_t _runtimeSighashBytes)
{
    runtimeSigOpCount = _runtimeSigOpCount;
    runtimeSighashBytes = _runtimeSighashBytes;
}

// Update the given tx for any in-mempool descendants.
// Assumes that setMemPoolChildren is correct for the given tx and all
// descendants.
bool CTxMemPool::UpdateForDescendants(txiter updateIt,
    int maxDescendantsToVisit,
    cacheMap &cachedDescendants,
    const std::set<uint256> &setExclude)
{
    AssertWriteLockHeld(cs);
    // Track the number of entries (outside setExclude) that we'd need to visit
    // (will bail out if it exceeds maxDescendantsToVisit)
    int nChildrenToVisit = 0;

    setEntries stageEntries, setAllDescendants;
    stageEntries = GetMemPoolChildren(updateIt);

    while (!stageEntries.empty())
    {
        const txiter cit = *stageEntries.begin();
        if (cit->IsDirty())
        {
            // Don't consider any more children if any descendant is dirty
            return false;
        }
        setAllDescendants.insert(cit);
        stageEntries.erase(cit); // BU its ok to erase here because GetMemPoolChildren does not dereference cit
        const setEntries &setChildren = GetMemPoolChildren(cit);
        BOOST_FOREACH (const txiter childEntry, setChildren)
        {
            cacheMap::iterator cacheIt = cachedDescendants.find(childEntry);
            if (cacheIt != cachedDescendants.end())
            {
                // We've already calculated this one, just add the entries for this set
                // but don't traverse again.
                BOOST_FOREACH (const txiter cacheEntry, cacheIt->second)
                {
                    // update visit count only for new child transactions
                    // (outside of setExclude and stageEntries)
                    if (setAllDescendants.insert(cacheEntry).second &&
                        !setExclude.count(cacheEntry->GetTx().GetHash()) && !stageEntries.count(cacheEntry))
                    {
                        nChildrenToVisit++;
                    }
                }
            }
            else if (!setAllDescendants.count(childEntry))
            {
                // Schedule for later processing and update our visit count
                if (stageEntries.insert(childEntry).second && !setExclude.count(childEntry->GetTx().GetHash()))
                {
                    nChildrenToVisit++;
                }
            }
            if (nChildrenToVisit > maxDescendantsToVisit)
            {
                return false;
            }
        }
    }
    // setAllDescendants now contains all in-mempool descendants of updateIt.
    // Update and add to cached descendant map
    int64_t modifySize = 0;
    CAmount modifyFee = 0;
    int64_t modifyCount = 0;
    BOOST_FOREACH (txiter cit, setAllDescendants)
    {
        if (!setExclude.count(cit->GetTx().GetHash()))
        {
            modifySize += cit->GetTxSize();
            modifyFee += cit->GetModifiedFee();
            modifyCount++;
            cachedDescendants[updateIt].insert(cit);
        }
    }
    mapTx.modify(updateIt, update_descendant_state(modifySize, modifyFee, modifyCount));
    return true;
}

// vHashesToUpdate is the set of transaction hashes from a disconnected block
// which has been re-added to the mempool.
// for each entry, look for descendants that are outside hashesToUpdate, and
// add fee/size information for such descendants to the parent.
void CTxMemPool::UpdateTransactionsFromBlock(const std::vector<uint256> &vHashesToUpdate)
{
    WRITELOCK(cs);
    // For each entry in vHashesToUpdate, store the set of in-mempool, but not
    // in-vHashesToUpdate transactions, so that we don't have to recalculate
    // descendants when we come across a previously seen entry.
    cacheMap mapMemPoolDescendantsToUpdate;

    // Use a set for lookups into vHashesToUpdate (these entries are already
    // accounted for in the state of their ancestors)
    std::set<uint256> setAlreadyIncluded(vHashesToUpdate.begin(), vHashesToUpdate.end());

    // Iterate in reverse, so that whenever we are looking at at a transaction
    // we are sure that all in-mempool descendants have already been processed.
    // This maximizes the benefit of the descendant cache and guarantees that
    // setMemPoolChildren will be updated, an assumption made in
    // UpdateForDescendants.
    BOOST_REVERSE_FOREACH (const uint256 &hash, vHashesToUpdate)
    {
        // we cache the in-mempool children to avoid duplicate updates
        setEntries setChildren;
        // calculate children from mapNextTx
        txiter it = mapTx.find(hash);
        if (it == mapTx.end())
        {
            continue;
        }
        std::map<COutPoint, CInPoint>::iterator iter = mapNextTx.lower_bound(COutPoint(hash, 0));
        // First calculate the children, and update setMemPoolChildren to
        // include them, and update their setMemPoolParents to include this tx.
        for (; iter != mapNextTx.end() && iter->first.hash == hash; ++iter)
        {
            const uint256 &childHash = iter->second.ptx->GetHash();
            txiter childIter = mapTx.find(childHash);
            assert(childIter != mapTx.end());
            // We can skip updating entries we've encountered before or that
            // are in the block (which are already accounted for).
            if (setChildren.insert(childIter).second && !setAlreadyIncluded.count(childHash))
            {
                _UpdateChild(it, childIter, true);
                _UpdateParent(childIter, it, true);
            }
        }
        if (!UpdateForDescendants(it, 100, mapMemPoolDescendantsToUpdate, setAlreadyIncluded))
        {
            // Mark as dirty if we can't do the calculation.
            mapTx.modify(it, set_dirty());
        }
    }
}


bool CTxMemPool::CalculateMemPoolAncestors(const CTxMemPoolEntry &entry,
    uint64_t limitAncestorCount,
    uint64_t limitAncestorSize,
    uint64_t limitDescendantCount,
    uint64_t limitDescendantSize,
    std::string &errString,
    bool fSearchForParents /* = true */)
{
    READLOCK(cs);
    setEntries setAncestors;
    return _CalculateMemPoolAncestors(entry, setAncestors, limitAncestorCount, limitAncestorSize, limitDescendantCount,
        limitDescendantSize, errString, fSearchForParents);
}


bool CTxMemPool::_CalculateMemPoolAncestors(const CTxMemPoolEntry &entry,
    setEntries &setAncestors,
    uint64_t limitAncestorCount,
    uint64_t limitAncestorSize,
    uint64_t limitDescendantCount,
    uint64_t limitDescendantSize,
    std::string &errString,
    bool fSearchForParents /* = true */)
{
    AssertLockHeld(cs);
    setEntries parentHashes;
    const CTransaction &tx = entry.GetTx();

    if (fSearchForParents)
    {
        // Get parents of this transaction that are in the mempool
        // GetMemPoolParents() is only valid for entries in the mempool, so we
        // iterate mapTx to find parents.
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            txiter piter = mapTx.find(tx.vin[i].prevout.hash);
            if (piter != mapTx.end())
            {
                parentHashes.insert(piter);
                if (parentHashes.size() + 1 > limitAncestorCount)
                {
                    errString = strprintf(
                        "too many unconfirmed parents: %u [limit: %u]", parentHashes.size(), limitAncestorCount);
                    return false;
                }
            }
        }
    }
    else
    {
        // If we're not searching for parents, we require this to be an
        // entry in the mempool already.
        txiter it = mapTx.iterator_to(entry);
        parentHashes = GetMemPoolParents(it);
    }

    size_t totalSizeWithAncestors = entry.GetTxSize();

    while (!parentHashes.empty())
    {
        txiter stageit = *parentHashes.begin();

        setAncestors.insert(stageit);
        // parentHashes.erase(stageit);  // BU: Core bug, use after free, moved below
        totalSizeWithAncestors += stageit->GetTxSize();

        if (stageit->GetSizeWithDescendants() + entry.GetTxSize() > limitDescendantSize)
        {
            errString = strprintf("exceeds descendant size limit for tx %s [limit: %u]",
                stageit->GetTx().GetHash().ToString(), limitDescendantSize);
            return false;
        }
        else if (stageit->GetCountWithDescendants() + 1 > limitDescendantCount)
        {
            errString = strprintf("too many descendants for tx %s [limit: %u]", stageit->GetTx().GetHash().ToString(),
                limitDescendantCount);
            return false;
        }
        else if (totalSizeWithAncestors > limitAncestorSize)
        {
            errString =
                strprintf(" %u exceeds ancestor size limit [limit: %u]", totalSizeWithAncestors, limitAncestorSize);
            return false;
        }

        const setEntries &setMemPoolParents = GetMemPoolParents(stageit);
        BOOST_FOREACH (const txiter &phash, setMemPoolParents)
        {
            // If this is a new ancestor, add it.
            if (setAncestors.count(phash) == 0)
            {
                parentHashes.insert(phash);
            }
            // removed +1 from test below as per BU: Fix use after free bug
            if (parentHashes.size() + setAncestors.size() > limitAncestorCount)
            {
                errString = strprintf("too many unconfirmed ancestors (%u+%u) [limit: %u]", parentHashes.size(),
                    setAncestors.size(), limitAncestorCount);
                return false;
            }
        }

        parentHashes.erase(stageit); // BU: Fix use after free bug by moving this last
    }

    return true;
}

bool CTxMemPool::ValidateMemPoolAncestors(const std::vector<CTxIn> &txIn,
    uint64_t limitAncestorCount,
    uint64_t limitAncestorSize,
    uint64_t limitDescendantCount,
    uint64_t limitDescendantSize,
    std::string &errString)
{
    AssertLockHeld(cs);
    setEntries parentHashes;
    setEntries setAncestors;
    int mySizeEstimate = 0; // we don't know our own tx size yet: entry.GetTxSize();

    // Get parents of this transaction that are in the mempool
    // GetMemPoolParents() is only valid for entries in the mempool, so we
    // iterate mapTx to find parents.
    for (unsigned int i = 0; i < txIn.size(); i++)
    {
        txiter piter = mapTx.find(txIn[i].prevout.hash);
        if (piter != mapTx.end())
        {
            parentHashes.insert(piter);
            if (parentHashes.size() + 1 > limitAncestorCount) // If we found it in the mempool, its unconfirmed
            {
                errString =
                    strprintf("too many unconfirmed parents: %u [limit: %u]", parentHashes.size(), limitAncestorCount);
                return false;
            }
        }
    }

    size_t totalSizeWithAncestors = mySizeEstimate;

    while (!parentHashes.empty())
    {
        txiter stageit = *parentHashes.begin();

        setAncestors.insert(stageit);
        totalSizeWithAncestors += stageit->GetTxSize();

        if (stageit->GetSizeWithDescendants() + mySizeEstimate > limitDescendantSize)
        {
            errString = strprintf("exceeds descendant size limit for tx %s [limit: %u]",
                stageit->GetTx().GetHash().ToString(), limitDescendantSize);
            return false;
        }
        else if (stageit->GetCountWithDescendants() + 1 > limitDescendantCount)
        {
            errString = strprintf("too many descendants for tx %s [limit: %u]", stageit->GetTx().GetHash().ToString(),
                limitDescendantCount);
            return false;
        }
        else if (totalSizeWithAncestors > limitAncestorSize)
        {
            errString =
                strprintf(" %u exceeds ancestor size limit [limit: %u]", totalSizeWithAncestors, limitAncestorSize);
            return false;
        }

        const setEntries &setMemPoolParents = GetMemPoolParents(stageit);
        BOOST_FOREACH (const txiter &phash, setMemPoolParents)
        {
            // If this is a new ancestor, add it.
            if (setAncestors.count(phash) == 0)
            {
                parentHashes.insert(phash);
            }
            if (parentHashes.size() + setAncestors.size() > limitAncestorCount)
            {
                errString = strprintf("too many unconfirmed ancestors (%u+%u) [limit: %u]", parentHashes.size(),
                    setAncestors.size(), limitAncestorCount);
                return false;
            }
        }

        parentHashes.erase(stageit); // BU: Fix use after free bug by moving this last
    }

    return true;
}


void CTxMemPool::_UpdateAncestorsOf(bool add, txiter it, setEntries &setAncestors)
{
    AssertWriteLockHeld(cs);
    setEntries parentIters = GetMemPoolParents(it);
    // add or remove this tx as a child of each parent
    BOOST_FOREACH (txiter piter, parentIters)
    {
        _UpdateChild(piter, it, add);
    }
    const int64_t updateCount = (add ? 1 : -1);
    const int64_t updateSize = updateCount * it->GetTxSize();
    const CAmount updateFee = updateCount * it->GetModifiedFee();
    BOOST_FOREACH (txiter ancestorIt, setAncestors)
    {
        mapTx.modify(ancestorIt, update_descendant_state(updateSize, updateFee, updateCount));
    }
}

void CTxMemPool::UpdateChildrenForRemoval(txiter it)
{
    AssertWriteLockHeld(cs);
    const setEntries &setMemPoolChildren = GetMemPoolChildren(it);
    BOOST_FOREACH (txiter updateIt, setMemPoolChildren)
    {
        _UpdateParent(updateIt, it, false);
    }
}

void CTxMemPool::_UpdateForRemoveFromMempool(const setEntries &entriesToRemove)
{
    AssertWriteLockHeld(cs);
    // For each entry, walk back all ancestors and decrement size associated with this
    // transaction
    const uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
    BOOST_FOREACH (txiter removeIt, entriesToRemove)
    {
        setEntries setAncestors;
        const CTxMemPoolEntry &entry = *removeIt;
        std::string dummy;
        // Since this is a tx that is already in the mempool, we can call CMPA
        // with fSearchForParents = false.  If the mempool is in a consistent
        // state, then using true or false should both be correct, though false
        // should be a bit faster.
        // However, if we happen to be in the middle of processing a reorg, then
        // the mempool can be in an inconsistent state.  In this case, the set
        // of ancestors reachable via mapLinks will be the same as the set of
        // ancestors whose packages include this transaction, because when we
        // add a new transaction to the mempool in addUnchecked(), we assume it
        // has no children, and in the case of a reorg where that assumption is
        // false, the in-mempool children aren't linked to the in-block tx's
        // until UpdateTransactionsFromBlock() is called.
        // So if we're being called during a reorg, ie before
        // UpdateTransactionsFromBlock() has been called, then mapLinks[] will
        // differ from the set of mempool parents we'd calculate by searching,
        // and it's important that we use the mapLinks[] notion of ancestor
        // transactions as the set of things to update for removal.
        _CalculateMemPoolAncestors(entry, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);
        // Note that UpdateAncestorsOf severs the child links that point to
        // removeIt in the entries for the parents of removeIt.  This is
        // fine since we don't need to use the mempool children of any entries
        // to walk back over our ancestors (but we do need the mempool
        // parents!)
        _UpdateAncestorsOf(false, removeIt, setAncestors);
    }
    // After updating all the ancestor sizes, we can now sever the link between each
    // transaction being removed and any mempool children (ie, update setMemPoolParents
    // for each direct child of a transaction being removed).
    BOOST_FOREACH (txiter removeIt, entriesToRemove)
    {
        UpdateChildrenForRemoval(removeIt);
    }
}

void CTxMemPoolEntry::SetDirty()
{
    nCountWithDescendants = 0;
    nSizeWithDescendants = nTxSize;
    nModFeesWithDescendants = GetModifiedFee();
}

void CTxMemPoolEntry::UpdateState(int64_t modifySize, CAmount modifyFee, int64_t modifyCount)
{
    if (!IsDirty())
    {
        nSizeWithDescendants += modifySize;
        assert(int64_t(nSizeWithDescendants) > 0);
        nModFeesWithDescendants += modifyFee;
        nCountWithDescendants += modifyCount;
        assert(int64_t(nCountWithDescendants) > 0);
    }
}

CTxMemPool::CTxMemPool(const CFeeRate &_minReasonableRelayFee) : nTransactionsUpdated(0)
{
    _clear(); // lock free clear

    // Sanity checks off by default for performance, because otherwise
    // accepting transactions becomes O(N^2) where N is the number
    // of transactions in the pool
    nCheckFrequency = 0;

    minerPolicyEstimator = new CBlockPolicyEstimator(_minReasonableRelayFee);
    minReasonableRelayFee = _minReasonableRelayFee;
}

CTxMemPool::~CTxMemPool() { delete minerPolicyEstimator; }
bool CTxMemPool::isSpent(const COutPoint &outpoint)
{
    AssertWriteLockHeld(cs);
    return mapNextTx.count(outpoint);
}

unsigned int CTxMemPool::GetTransactionsUpdated() const
{
    READLOCK(cs);
    return nTransactionsUpdated;
}

void CTxMemPool::AddTransactionsUpdated(unsigned int n)
{
    WRITELOCK(cs);
    nTransactionsUpdated += n;
}

bool CTxMemPool::addUnchecked(const uint256 &hash,
    const CTxMemPoolEntry &entry,
    setEntries &setAncestors,
    bool fCurrentEstimate)
{
    // Add to memory pool without checking anything.
    // Used by main.cpp AcceptToMemoryPool(), which DOES do
    // all the appropriate checks.
    AssertWriteLockHeld(cs);
    if (mapTx.find(hash) != mapTx.end()) // already inserted
    {
        // LogPrintf("WARNING: transaction already in mempool\n");
        return true;
    }
    indexed_transaction_set::iterator newit = mapTx.insert(entry).first;
    mapLinks.insert(make_pair(newit, TxLinks()));

    // Update transaction for any feeDelta created by PrioritiseTransaction
    // TODO: refactor so that the fee delta is calculated before inserting
    // into mapTx.
    std::map<uint256, std::pair<double, CAmount> >::const_iterator pos = mapDeltas.find(hash);
    if (pos != mapDeltas.end())
    {
        const std::pair<double, CAmount> &deltas = pos->second;
        if (deltas.second)
        {
            mapTx.modify(newit, update_fee_delta(deltas.second));
        }
    }

    // Update cachedInnerUsage to include contained transaction's usage.
    // (When we update the entry for in-mempool parents, memory usage will be
    // further updated.)
    cachedInnerUsage += entry.DynamicMemoryUsage();

    const CTransaction &tx = newit->GetTx();
    std::set<uint256> setParentTransactions;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        mapNextTx[tx.vin[i].prevout] = CInPoint(&tx, i);
        setParentTransactions.insert(tx.vin[i].prevout.hash);
    }
    // Don't bother worrying about child transactions of this one.
    // Normal case of a new transaction arriving is that there can't be any
    // children, because such children would be orphans.
    // An exception to that is if a transaction enters that used to be in a block.
    // In that case, our disconnect block logic will call UpdateTransactionsFromBlock
    // to clean up the mess we're leaving here.

    // Update ancestors with information about this tx
    BOOST_FOREACH (const uint256 &phash, setParentTransactions)
    {
        txiter pit = mapTx.find(phash);
        if (pit != mapTx.end())
        {
            _UpdateParent(newit, pit, true);
        }
    }
    _UpdateAncestorsOf(true, newit, setAncestors);

    nTransactionsUpdated++;
    totalTxSize += entry.GetTxSize();
    txAdded += 1; // BU
    poolSize() = totalTxSize; // BU
    minerPolicyEstimator->processTransaction(entry, fCurrentEstimate);

    return true;
}

void CTxMemPool::removeUnchecked(txiter it)
{
    AssertWriteLockHeld(cs);
    const uint256 hash = it->GetTx().GetHash();
    BOOST_FOREACH (const CTxIn &txin, it->GetTx().vin)
        mapNextTx.erase(txin.prevout);

    totalTxSize -= it->GetTxSize();
    cachedInnerUsage -= it->DynamicMemoryUsage();
    cachedInnerUsage -= memusage::DynamicUsage(mapLinks[it].parents) + memusage::DynamicUsage(mapLinks[it].children);
    mapLinks.erase(it);
    mapTx.erase(it);
    nTransactionsUpdated++;
    minerPolicyEstimator->removeTx(hash);
}

// Calculates descendants of entry that are not already in setDescendants, and adds to
// setDescendants. Assumes entryit is already a tx in the mempool and setMemPoolChildren
// is correct for tx and all descendants.
// Also assumes that if an entry is in setDescendants already, then all
// in-mempool descendants of it are already in setDescendants as well, so that we
// can save time by not iterating over those entries.
void CTxMemPool::_CalculateDescendants(txiter entryit, setEntries &setDescendants)
{
    AssertWriteLockHeld(cs);
    setEntries stage;
    if (setDescendants.count(entryit) == 0)
    {
        stage.insert(entryit);
    }
    // Traverse down the children of entry, only adding children that are not
    // accounted for in setDescendants already (because those children have either
    // already been walked, or will be walked in this iteration).
    while (!stage.empty())
    {
        txiter it = *stage.begin();
        setDescendants.insert(it);
        stage.erase(it); // BU its ok to erase here because GetMemPoolChildren does not dereference it

        const setEntries &setChildren = GetMemPoolChildren(it);
        BOOST_FOREACH (const txiter &childiter, setChildren)
        {
            if (!setDescendants.count(childiter))
            {
                stage.insert(childiter);
            }
        }
    }
}

void CTxMemPool::remove(const CTransaction &origTx, std::list<CTransaction> &removed, bool fRecursive)
{
    WRITELOCK(cs);
    _remove(origTx, removed, fRecursive);
}

void CTxMemPool::_remove(const CTransaction &origTx, std::list<CTransaction> &removed, bool fRecursive)
{
    AssertWriteLockHeld(cs);
    // Remove transaction from memory pool
    setEntries txToRemove;
    txiter origit = mapTx.find(origTx.GetHash());
    if (origit != mapTx.end())
    {
        txToRemove.insert(origit);
    }
    else if (fRecursive)
    {
        // If recursively removing but origTx isn't in the mempool
        // be sure to remove any children that are in the pool. This can
        // happen during chain re-orgs if origTx isn't re-accepted into
        // the mempool for any reason.
        for (unsigned int i = 0; i < origTx.vout.size(); i++)
        {
            std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(origTx.GetHash(), i));
            if (it == mapNextTx.end())
                continue;
            txiter nextit = mapTx.find(it->second.ptx->GetHash());
            assert(nextit != mapTx.end());
            txToRemove.insert(nextit);
        }
    }
    setEntries setAllRemoves;
    if (fRecursive)
    {
        BOOST_FOREACH (txiter it, txToRemove)
        {
            _CalculateDescendants(it, setAllRemoves);
        }
    }
    else
    {
        setAllRemoves.swap(txToRemove);
    }
    BOOST_FOREACH (txiter it, setAllRemoves)
    {
        removed.push_back(it->GetTx());
    }
    _RemoveStaged(setAllRemoves);
}

void CTxMemPool::removeForReorg(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight, int flags)
{
    // Remove transactions spending a coinbase which are now immature and no-longer-final transactions
    WRITELOCK(cs);
    list<CTransaction> transactionsToRemove;
    for (indexed_transaction_set::const_iterator it = mapTx.begin(); it != mapTx.end(); it++)
    {
        const CTransaction &tx = it->GetTx();
        LockPoints lp = it->GetLockPoints();
        bool validLP = TestLockPointValidity(&lp);
        if (!CheckFinalTx(tx, flags) || !CheckSequenceLocks(tx, flags, &lp, validLP))
        {
            // Note if CheckSequenceLocks fails the LockPoints may still be invalid
            // So it's critical that we remove the tx and not depend on the LockPoints.
            transactionsToRemove.push_back(tx);
        }
        else if (it->GetSpendsCoinbase())
        {
            BOOST_FOREACH (const CTxIn &txin, tx.vin)
            {
                indexed_transaction_set::const_iterator it2 = mapTx.find(txin.prevout.hash);
                if (it2 != mapTx.end())
                    continue;
                CoinAccessor coin(*pcoins, txin.prevout);
                if (nCheckFrequency != 0)
                    assert(!coin->IsSpent());
                if (coin->IsSpent() ||
                    (coin->IsCoinBase() && ((signed long)nMemPoolHeight) - coin->nHeight < COINBASE_MATURITY))
                {
                    transactionsToRemove.push_back(tx);
                    break;
                }
            }
        }
        if (!validLP)
        {
            mapTx.modify(it, update_lock_points(lp));
        }
    }
    BOOST_FOREACH (const CTransaction &tx, transactionsToRemove)
    {
        list<CTransaction> removed;
        _remove(tx, removed, true);
    }
}

void CTxMemPool::removeConflicts(const CTransaction &tx, std::list<CTransaction> &removed)
{
    WRITELOCK(cs);
    _removeConflicts(tx, removed);
}

void CTxMemPool::_removeConflicts(const CTransaction &tx, std::list<CTransaction> &removed)
{
    AssertWriteLockHeld(cs);
    // Remove transactions which depend on inputs of tx, recursively
    BOOST_FOREACH (const CTxIn &txin, tx.vin)
    {
        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end())
        {
            const CTransaction &txConflict = *it->second.ptx;
            if (txConflict != tx)
            {
                _remove(txConflict, removed, true);
                _ClearPrioritisation(txConflict.GetHash());
            }
        }
    }
}

/**
 * Called when a block is connected. Removes from mempool and updates the miner fee estimator.
 */
void CTxMemPool::removeForBlock(const std::vector<CTransaction> &vtx,
    unsigned int nBlockHeight,
    std::list<CTransaction> &conflicts,
    bool fCurrentEstimate)
{
    WRITELOCK(cs);
    std::vector<CTxMemPoolEntry> entries;
    BOOST_FOREACH (const CTransaction &tx, vtx)
    {
        uint256 hash = tx.GetHash();

        indexed_transaction_set::iterator i = mapTx.find(hash);
        if (i != mapTx.end())
            entries.push_back(*i);
    }
    BOOST_FOREACH (const CTransaction &tx, vtx)
    {
        std::list<CTransaction> dummy;
        _remove(tx, dummy, false);
        _removeConflicts(tx, conflicts);
        _ClearPrioritisation(tx.GetHash());
    }
    // After the txs in the new block have been removed from the mempool, update policy estimates
    minerPolicyEstimator->processBlock(nBlockHeight, entries, fCurrentEstimate);
    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = true;
}

void CTxMemPool::_clear()
{
    mapLinks.clear();
    mapTx.clear();
    mapNextTx.clear();
    totalTxSize = 0;
    cachedInnerUsage = 0;
    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = false;
    rollingMinimumFeeRate = 0;
    ++nTransactionsUpdated;
}

void CTxMemPool::clear()
{
    WRITELOCK(cs);
    _clear();
}

void CTxMemPool::check(const CCoinsViewCache *pcoins) const
{
    if (nCheckFrequency == 0)
        return;

    if (insecure_rand() >= nCheckFrequency)
        return;


    uint64_t checkTotal = 0;
    uint64_t innerUsage = 0;

    WRITELOCK(cs);
    LogPrint("mempool", "Checking mempool with %u transactions and %u inputs\n", (unsigned int)mapTx.size(),
        (unsigned int)mapNextTx.size());
    CCoinsViewCache mempoolDuplicate(const_cast<CCoinsViewCache *>(pcoins));

    list<const CTxMemPoolEntry *> waitingOnDependants;
    for (indexed_transaction_set::const_iterator it = mapTx.begin(); it != mapTx.end(); it++)
    {
        unsigned int i = 0;
        checkTotal += it->GetTxSize();
        innerUsage += it->DynamicMemoryUsage();
        const CTransaction &tx = it->GetTx();
        txlinksMap::const_iterator linksiter = mapLinks.find(it);
        assert(linksiter != mapLinks.end());
        const TxLinks &links = linksiter->second;
        innerUsage += memusage::DynamicUsage(links.parents) + memusage::DynamicUsage(links.children);
        bool fDependsWait = false;
        setEntries setParentCheck;
        BOOST_FOREACH (const CTxIn &txin, tx.vin)
        {
            // Check that every mempool transaction's inputs refer to available coins, or other mempool tx's.
            indexed_transaction_set::const_iterator it2 = mapTx.find(txin.prevout.hash);
            if (it2 != mapTx.end())
            {
                const CTransaction &tx2 = it2->GetTx();
                assert(tx2.vout.size() > txin.prevout.n && !tx2.vout[txin.prevout.n].IsNull());
                fDependsWait = true;
                setParentCheck.insert(it2);
            }
            else
            {
                assert(pcoins->HaveCoin(txin.prevout));
            }
            // Check whether its inputs are marked in mapNextTx.
            std::map<COutPoint, CInPoint>::const_iterator it3 = mapNextTx.find(txin.prevout);
            assert(it3 != mapNextTx.end());
            assert(it3->second.ptx == &tx);
            assert(it3->second.n == i);
            i++;
        }
        assert(setParentCheck == GetMemPoolParents(it));
        // Check children against mapNextTx
        CTxMemPool::setEntries setChildrenCheck;
        std::map<COutPoint, CInPoint>::const_iterator iter = mapNextTx.lower_bound(COutPoint(it->GetTx().GetHash(), 0));
        int64_t childSizes = 0;
        CAmount childModFee = 0;
        for (; iter != mapNextTx.end() && iter->first.hash == it->GetTx().GetHash(); ++iter)
        {
            txiter childit = mapTx.find(iter->second.ptx->GetHash());
            assert(childit != mapTx.end()); // mapNextTx points to in-mempool transactions
            if (setChildrenCheck.insert(childit).second)
            {
                childSizes += childit->GetTxSize();
                childModFee += childit->GetModifiedFee();
            }
        }
        assert(setChildrenCheck == GetMemPoolChildren(it));
        // Also check to make sure size is greater than sum with immediate children.
        // just a sanity check, not definitive that this calc is correct...
        if (!it->IsDirty())
        {
            assert(it->GetSizeWithDescendants() >= childSizes + it->GetTxSize());
        }
        else
        {
            assert(it->GetSizeWithDescendants() == it->GetTxSize());
            assert(it->GetModFeesWithDescendants() == it->GetModifiedFee());
        }

        if (fDependsWait)
            waitingOnDependants.push_back(&(*it));
        else
        {
            CValidationState state;
            assert(CheckInputs(tx, state, mempoolDuplicate, false, 0, false, NULL));
            UpdateCoins(tx, state, mempoolDuplicate, 1000000);
        }
    }
    unsigned int stepsSinceLastRemove = 0;
    while (!waitingOnDependants.empty())
    {
        const CTxMemPoolEntry *entry = waitingOnDependants.front();
        waitingOnDependants.pop_front();
        CValidationState state;
        if (!mempoolDuplicate.HaveInputs(entry->GetTx()))
        {
            waitingOnDependants.push_back(entry);
            stepsSinceLastRemove++;
            assert(stepsSinceLastRemove < waitingOnDependants.size());
        }
        else
        {
            assert(CheckInputs(entry->GetTx(), state, mempoolDuplicate, false, 0, false, NULL));
            UpdateCoins(entry->GetTx(), state, mempoolDuplicate, 1000000);
            stepsSinceLastRemove = 0;
        }
    }
    for (std::map<COutPoint, CInPoint>::const_iterator it = mapNextTx.begin(); it != mapNextTx.end(); it++)
    {
        uint256 hash = it->second.ptx->GetHash();
        indexed_transaction_set::const_iterator it2 = mapTx.find(hash);
        const CTransaction &tx = it2->GetTx();
        assert(it2 != mapTx.end());
        assert(&tx == it->second.ptx);
        assert(tx.vin.size() > it->second.n);
        assert(it->first == it->second.ptx->vin[it->second.n].prevout);
    }

    assert(totalTxSize == checkTotal);
    assert(innerUsage == cachedInnerUsage);
}

void CTxMemPool::queryHashes(vector<uint256> &vtxid) const
{
    vtxid.clear();

    AssertLockHeld(cs);
    vtxid.reserve(mapTx.size());
    for (indexed_transaction_set::const_iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
        vtxid.push_back(mi->GetTx().GetHash());
}


bool CTxMemPool::_lookup(const uint256 &hash, CTxMemPoolEntry &result) const
{
    AssertLockHeld(cs);
    indexed_transaction_set::const_iterator i = mapTx.find(hash);
    if (i == mapTx.end())
        return false;
    result = *i;
    return true;
}

bool CTxMemPool::lookup(const uint256 &hash, CTxMemPoolEntry &result) const
{
    READLOCK(cs);
    return _lookup(hash, result);
}

bool CTxMemPool::lookup(const uint256 &hash, CTransaction &result) const
{
    READLOCK(cs);
    return _lookup(hash, result);
}

bool CTxMemPool::_lookup(const uint256 &hash, CTransaction &result) const
{
    AssertLockHeld(cs);
    indexed_transaction_set::const_iterator i = mapTx.find(hash);
    if (i == mapTx.end())
        return false;
    result = i->GetTx();
    return true;
}

CFeeRate CTxMemPool::estimateFee(int nBlocks) const
{
    READLOCK(cs);
    return minerPolicyEstimator->estimateFee(nBlocks);
}
CFeeRate CTxMemPool::estimateSmartFee(int nBlocks, int *answerFoundAtBlocks) const
{
    READLOCK(cs);
    return minerPolicyEstimator->estimateSmartFee(nBlocks, answerFoundAtBlocks, *this);
}
double CTxMemPool::estimatePriority(int nBlocks) const
{
    READLOCK(cs);
    return minerPolicyEstimator->estimatePriority(nBlocks);
}
double CTxMemPool::estimateSmartPriority(int nBlocks, int *answerFoundAtBlocks) const
{
    READLOCK(cs);
    return minerPolicyEstimator->estimateSmartPriority(nBlocks, answerFoundAtBlocks, *this);
}

bool CTxMemPool::WriteFeeEstimates(CAutoFile &fileout) const
{
    try
    {
        READLOCK(cs);
        fileout << 109900; // version required to read: 0.10.99 or later
        fileout << CLIENT_VERSION; // version that wrote the file
        minerPolicyEstimator->Write(fileout);
    }
    catch (const std::exception &)
    {
        LogPrintf("CTxMemPool::WriteFeeEstimates(): unable to write policy estimator data (non-fatal)\n");
        return false;
    }
    return true;
}

bool CTxMemPool::ReadFeeEstimates(CAutoFile &filein)
{
    try
    {
        int nVersionRequired, nVersionThatWrote;
        filein >> nVersionRequired >> nVersionThatWrote;
        if (nVersionRequired > CLIENT_VERSION)
            return error("CTxMemPool::ReadFeeEstimates(): up-version (%d) fee estimate file", nVersionRequired);

        WRITELOCK(cs);
        minerPolicyEstimator->Read(filein);
    }
    catch (const std::exception &)
    {
        LogPrintf("CTxMemPool::ReadFeeEstimates(): unable to read policy estimator data (non-fatal)\n");
        return false;
    }
    return true;
}

void CTxMemPool::PrioritiseTransaction(const uint256 hash,
    const string strHash,
    double dPriorityDelta,
    const CAmount &nFeeDelta)
{
    {
        WRITELOCK(cs);
        std::pair<double, CAmount> &deltas = mapDeltas[hash];
        deltas.first += dPriorityDelta;
        deltas.second += nFeeDelta;
        txiter it = mapTx.find(hash);
        if (it != mapTx.end())
        {
            mapTx.modify(it, update_fee_delta(deltas.second));
            // Now update all ancestors' modified fees with descendants
            setEntries setAncestors;
            uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
            std::string dummy;
            _CalculateMemPoolAncestors(*it, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);
            BOOST_FOREACH (txiter ancestorIt, setAncestors)
            {
                mapTx.modify(ancestorIt, update_descendant_state(0, nFeeDelta, 0));
            }
        }
    }
    LogPrintf("PrioritiseTransaction: %s priority += %f, fee += %d\n", strHash, dPriorityDelta, FormatMoney(nFeeDelta));
}

void CTxMemPool::ApplyDeltas(const uint256 hash, double &dPriorityDelta, CAmount &nFeeDelta) const
{
    READLOCK(cs);
    _ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
}
void CTxMemPool::_ApplyDeltas(const uint256 hash, double &dPriorityDelta, CAmount &nFeeDelta) const
{
    std::map<uint256, std::pair<double, CAmount> >::const_iterator pos = mapDeltas.find(hash);
    if (pos == mapDeltas.end())
        return;
    const std::pair<double, CAmount> &deltas = pos->second;
    dPriorityDelta += deltas.first;
    nFeeDelta += deltas.second;
}

void CTxMemPool::ClearPrioritisation(const uint256 hash)
{
    WRITELOCK(cs);
    mapDeltas.erase(hash);
}
void CTxMemPool::_ClearPrioritisation(const uint256 hash) { mapDeltas.erase(hash); }
bool CTxMemPool::HasNoInputsOf(const CTransaction &tx) const
{
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        if (exists(tx.vin[i].prevout.hash))
            return false;
    return true;
}

CCoinsViewMemPool::CCoinsViewMemPool(CCoinsView *baseIn, const CTxMemPool &mempoolIn)
    : CCoinsViewBacked(baseIn), mempool(mempoolIn)
{
}

bool CCoinsViewMemPool::GetCoin(const COutPoint &outpoint, Coin &coin) const
{
    // If an entry in the mempool exists, always return that one, as it's guaranteed to never
    // conflict with the underlying cache, and it cannot have pruned entries (as it contains full)
    // transactions. First checking the underlying cache risks returning a pruned entry instead.
    CTransaction tx;
    if (mempool._lookup(outpoint.hash, tx))
    {
        if (outpoint.n < tx.vout.size())
        {
            coin = Coin(tx.vout[outpoint.n], MEMPOOL_HEIGHT, false);
            return true;
        }
        else
        {
            return false;
        }
    }
    return (base->GetCoin(outpoint, coin) && !coin.IsSpent());
}

bool CCoinsViewMemPool::HaveCoin(const COutPoint &outpoint) const
{
    return mempool.exists(outpoint) || base->HaveCoin(outpoint);
}

size_t CTxMemPool::DynamicMemoryUsage() const
{
    READLOCK(cs);
    // Estimate the overhead of mapTx to be 12 pointers + an allocation, as no exact formula for
    // boost::multi_index_contained is implemented.
    return _DynamicMemoryUsage();
}

size_t CTxMemPool::_DynamicMemoryUsage() const
{
    AssertLockHeld(cs);
    // Estimate the overhead of mapTx to be 12 pointers + an allocation, as no exact formula for
    // boost::multi_index_contained is implemented.
    return memusage::MallocUsage(sizeof(CTxMemPoolEntry) + 12 * sizeof(void *)) * mapTx.size() +
           memusage::DynamicUsage(mapNextTx) + memusage::DynamicUsage(mapDeltas) + memusage::DynamicUsage(mapLinks) +
           cachedInnerUsage;
}

void CTxMemPool::_RemoveStaged(setEntries &stage)
{
    AssertWriteLockHeld(cs);
    _UpdateForRemoveFromMempool(stage);
    BOOST_FOREACH (const txiter &it, stage)
    {
        removeUnchecked(it);
    }
}

int CTxMemPool::Expire(int64_t time)
{
    WRITELOCK(cs);
    indexed_transaction_set::index<entry_time>::type::iterator it = mapTx.get<entry_time>().begin();
    setEntries toremove;
    while (it != mapTx.get<entry_time>().end() && it->GetTime() < time)
    {
        toremove.insert(mapTx.project<0>(it));
        it++;
    }
    setEntries stage;
    BOOST_FOREACH (txiter removeit, toremove)
    {
        _CalculateDescendants(removeit, stage);
    }
    _RemoveStaged(stage);
    return stage.size();
}

bool CTxMemPool::addUnchecked(const uint256 &hash, const CTxMemPoolEntry &entry, bool fCurrentEstimate)
{
    WRITELOCK(cs);
    setEntries setAncestors;
    uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    _CalculateMemPoolAncestors(entry, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy);
    return addUnchecked(hash, entry, setAncestors, fCurrentEstimate);
}

void CTxMemPool::_UpdateChild(txiter entry, txiter child, bool add)
{
    setEntries s;
    AssertLockHeld(cs);
    if (add && mapLinks[entry].children.insert(child).second)
    {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    }
    else if (!add && mapLinks[entry].children.erase(child))
    {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }
}

void CTxMemPool::_UpdateParent(txiter entry, txiter parent, bool add)
{
    setEntries s;
    AssertLockHeld(cs);
    if (add && mapLinks[entry].parents.insert(parent).second)
    {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    }
    else if (!add && mapLinks[entry].parents.erase(parent))
    {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }
}

const CTxMemPool::setEntries &CTxMemPool::GetMemPoolParents(txiter entry) const
{
    AssertLockHeld(cs);
    assert(entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.parents;
}

const CTxMemPool::setEntries &CTxMemPool::GetMemPoolChildren(txiter entry) const
{
    AssertLockHeld(cs);
    assert(entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.children;
}

CFeeRate CTxMemPool::GetMinFee(size_t sizelimit) const
{
    READLOCK(cs);
    return _GetMinFee(sizelimit);
}

CFeeRate CTxMemPool::_GetMinFee(size_t sizelimit) const
{
    AssertLockHeld(cs);
    if (!blockSinceLastRollingFeeBump || rollingMinimumFeeRate == 0)
        return CFeeRate(rollingMinimumFeeRate);

    int64_t time = GetTime();
    if (time > lastRollingFeeUpdate + 10)
    {
        double halflife = ROLLING_FEE_HALFLIFE;
        size_t dmu = _DynamicMemoryUsage();
        if (dmu < sizelimit / 4)
            halflife /= 4;
        else if (dmu < sizelimit / 2)
            halflife /= 2;

        rollingMinimumFeeRate = rollingMinimumFeeRate / pow(2.0, (time - lastRollingFeeUpdate) / halflife);
        lastRollingFeeUpdate = time;

        if (rollingMinimumFeeRate < minReasonableRelayFee.GetFeePerK() / 2)
        {
            rollingMinimumFeeRate = 0;
            return CFeeRate(0);
        }
    }
    static const double maxFeeCutoff = boost::lexical_cast<double>(GetArg("-maxlimitertxfee", DEFAULT_MAXLIMITERTXFEE));
    CFeeRate estimated_fee = std::max(CFeeRate(rollingMinimumFeeRate), minReasonableRelayFee);
    return std::min(estimated_fee, CFeeRate(maxFeeCutoff));
}

void CTxMemPool::trackPackageRemoved(const CFeeRate &rate)
{
    AssertLockHeld(cs);
    if (rate.GetFeePerK() > rollingMinimumFeeRate)
    {
        rollingMinimumFeeRate = rate.GetFeePerK();
        blockSinceLastRollingFeeBump = false;
    }
}

void CTxMemPool::TrimToSize(size_t sizelimit, std::vector<COutPoint> *pvNoSpendsRemaining)
{
    WRITELOCK(cs);
    unsigned nTxnRemoved = 0;
    CFeeRate maxFeeRateRemoved(0);
    while (_DynamicMemoryUsage() > sizelimit)
    {
        indexed_transaction_set::index<descendant_score>::type::iterator it = mapTx.get<descendant_score>().begin();

        // We set the new mempool min fee to the feerate of the removed set, plus the
        // "minimum reasonable fee rate" (ie some value under which we consider txn
        // to have 0 fee). This way, we don't allow txn to enter mempool with feerate
        // equal to txn which were removed with no block in between.
        CFeeRate removed(it->GetModFeesWithDescendants(), it->GetSizeWithDescendants());
        removed += minReasonableRelayFee;
        trackPackageRemoved(removed);
        maxFeeRateRemoved = std::max(maxFeeRateRemoved, removed);

        setEntries stage;
        _CalculateDescendants(mapTx.project<0>(it), stage);
        nTxnRemoved += stage.size();

        std::vector<CTransaction> txn;
        if (pvNoSpendsRemaining)
        {
            txn.reserve(stage.size());
            BOOST_FOREACH (txiter it, stage)
                txn.push_back(it->GetTx());
        }
        _RemoveStaged(stage);
        if (pvNoSpendsRemaining)
        {
            BOOST_FOREACH (const CTransaction &tx, txn)
            {
                BOOST_FOREACH (const CTxIn &txin, tx.vin)
                {
                    if (_exists(txin.prevout.hash))
                        continue;
                    if (!mapNextTx.count(txin.prevout))
                    {
                        pvNoSpendsRemaining->push_back(txin.prevout);
                    }
                }
            }
        }
    }

    if (maxFeeRateRemoved > CFeeRate(0))
        LogPrint(
            "mempool", "Removed %u txn, rolling minimum fee bumped to %s\n", nTxnRemoved, maxFeeRateRemoved.ToString());
}

void CTxMemPool::UpdateTransactionsPerSecond()
{
    boost::mutex::scoped_lock lock(cs_txPerSec);

    static int64_t nLastTime = GetTime();
    double nSecondsToAverage = 60; // Length of time in seconds to smooth the tx rate over
    int64_t nNow = GetTime();

    // Decay the previous tx rate.
    int64_t nDeltaTime = nNow - nLastTime;
    if (nDeltaTime > 0)
    {
        nTxPerSec -= (nTxPerSec / nSecondsToAverage) * nDeltaTime;
        nLastTime = nNow;
    }

    // Add the new tx to the rate
    nTxPerSec += 1 / nSecondsToAverage; // The amount that the new tx will add to the tx rate
    if (nTxPerSec < 0)
        nTxPerSec = 0;
}

SaltedTxidHasher::SaltedTxidHasher()
    : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max()))
{
}
