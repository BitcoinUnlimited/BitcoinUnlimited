#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <queue>
#include "sync.h"
#include "weakblock.h"
#include "tweak.h"
#include "util.h"
#include "chainparams.h"

// FIXME: what about asserts here?

static const uint32_t WB_MIN_POW_RATIO = 600;

CCriticalSection cs_weakblocks;

bool weakblocksEnabled()  { LOCK(cs_weakblocks); return wbEnable.value; }

uint32_t weakblocksConsiderPOWRatio() {
    AssertLockHeld(cs_weakblocks);
    return wbConsiderPOWratio.value;
}

uint32_t weakblocksMinPOWRatio() {
    AssertLockHeld(cs_weakblocks);
    return WB_MIN_POW_RATIO;
}

uint256 weakblocksExtractCommitment(const CBlock* block) {
    if (block == nullptr) return uint256();

    if (block->vtx.size()<1) return uint256();

    const CTransactionRef& coinbase = block->vtx[0];

    for (const CTxOut out : coinbase->vout) {
        const CScript& cand = out.scriptPubKey;
        // is it OP_RETURN, size byte (34), 'WB'+32 byte hash?
        if (cand.size() == 36) {
            if (cand[0] == OP_RETURN && cand[1] == 0x22 &&
                cand[2] == 'W' && cand[3] == 'B') {
                uint256 hash;
                std::copy(cand.begin()+4, cand.end(), hash.begin());
                LOG(WB, "Found candidate weak block hash %s in block %s.\n", hash.GetHex(), block->GetHash().GetHex());
                return hash;
            }
        }
    }
    return uint256();
}

CWeakblock::CWeakblock(const CBlock* other) {
    SetNull();
    weak_height_cache = 0;
    weak_height_cache_valid = false;
    *((CBlock*)this) = *other;
}

static bool extends_check(const CBlock* block, const CBlock* underlying) {
    if (block == nullptr || underlying == nullptr) return false;

    if (underlying->vtx.size() > block->vtx.size()) return false;

    // start at index 1 to skip coinbase transaction
    for (size_t i=1; i < underlying->vtx.size(); i++)
        // FIXME: should this compare refs instead? Would that always work?
        if (*underlying->vtx[i] != *block->vtx[i])
            return false;
    return true;
}

bool CWeakblock::extends(const CBlock* underlying) {
    return extends_check(this, underlying);
}

bool CWeakblock::extends(const CBlock& underlying) {
    return extends_check(this, &underlying);
}

bool CWeakblock::extends(const ConstCBlockRef& underlying) {
    return extends_check(this, underlying.get());
}

int CWeakblock::GetWeakHeight() const {
    AssertLockHeld(cs_weakblocks);

    if (weak_height_cache_valid) return weak_height_cache;

    uint256 wbhash = GetHash();

    if (weakstore.to_remove.count(wbhash)) {
        //LOG(WB, "weakHeight(%s) == -1 (block marked for removal)\n", wbhash.GetHex());
        weak_height_cache_valid = true;
        return weak_height_cache = -1;
    }

    if (weakstore.extends_map.count(wbhash)) {
        int prev_height = -1;

        const uint256 underlying_hash = weakstore.extends_map.at(wbhash);

        if (weakstore.hash2wb.count(underlying_hash)) {
            CWeakblockRef underlying_wb = weakstore.hash2wb[underlying_hash];
            if (underlying_wb != nullptr)
                prev_height = underlying_wb->GetWeakHeight();
            else {
                // FIXME: what else to do? this should never happen
                LOG(WB, "GetWeakHeight(): Nullpointer encountered in hash2wb!!\n");
            }
        } else {
            // FIXME: what else to do? this should never happen
            LOG(WB, "GetWeakHeight(): Underlying weakblock not found!!\n");
        }
        if (prev_height >=0) {
            weak_height_cache_valid = true;
            return weak_height_cache = 1 + prev_height;
        }
        else {
            weak_height_cache_valid = true;
            return weak_height_cache = -1;
        }
    } else {
        weak_height_cache_valid = true;
        return weak_height_cache = 0;
    }
}

CWeakblockRef CWeakStore::store(const CBlock* block) {
    const uint256 blockhash = block->GetHash();
    const uint256 underlyinghash = weakblocksExtractCommitment(block);

    LOCK(cs_weakblocks);

    if (hash2wb.count(blockhash) > 0) {
        LOG(WB, "Ignoring attempt to store weak block %s twice.\n", blockhash.GetHex());
        // stored it already
        return nullptr;
    }

    CWeakblockRef underlying = nullptr;
    if (hash2wb.count(underlyinghash) > 0)
        underlying = hash2wb[underlyinghash];

    if (!underlyinghash.IsNull() && underlying == nullptr) {
        LOG(WB, "Weak block %s with unknown underlying block %s. Assuming start of new chain.\n", blockhash.GetHex(), underlyinghash.GetHex());
    } else if (underlying != nullptr && !extends_check(
                   block, underlying.get())) {
        LOG(WB, "WARNING, block %s does not extend weak block %s, even though it says so! Assuming start of new chain.\n", blockhash.GetHex(), underlyinghash.GetHex());
        underlying = nullptr;
    }

    CWeakblockRef wb=std::make_shared<CWeakblock>(block);

    // FIXME: assert ..
    assert(wb->GetHash() == blockhash);

    hash2wb[blockhash] = wb;

    const uint64_t cheapblockhash = blockhash.GetCheapHash();
    if (cheaphash2wb.count(cheapblockhash))
        LOG(WB, "WARNING, weak block cheap hash collision for weak block %s.\n", blockhash.GetHex());
    cheaphash2wb[cheapblockhash] = wb;

    // extend the DAG
    if (underlying != nullptr) {
        extends_map[blockhash]=underlyinghash;
        LOG(WB, "Weakblock %s is referring to underlying weak block %s.\n", wb->GetHash().GetHex(), underlyinghash.GetHex());

        auto wct_iter = find(chain_tips.begin(),
                         chain_tips.end(),
                         underlying);
        if (wct_iter != chain_tips.end()) {
            LOG(WB, "Underlying weak block %s was weak chain tip before. Moving to new weakblock.\n", underlyinghash.GetHex());
            chain_tips.erase(wct_iter);
        }
    }
    chain_tips.push_back(wb);
    LOG(WB, "Tracking weak block %s (short: %x) of %d transaction(s), parent: %s.\n", blockhash.GetHex(), blockhash.GetCheapHash(), wb->vtx.size(), wb->hashPrevBlock.GetHex());
    return wb;
}

CWeakblockRef CWeakStore::Tip() {
    LOCK(cs_weakblocks);

    int max_wheight=-1;
    CWeakblockRef longest = nullptr;

    for (CWeakblockRef wb : chain_tips) {
        int wheight = wb->GetWeakHeight();
        if (wheight > max_wheight) {
            longest = wb;
            max_wheight = wheight;
        }
    }
    return longest;
}

void CWeakStore::expireOld(const bool fThorough) {
    LOCK(cs_weakblocks);
    if (fThorough) {
        hash2wb.clear();
        cheaphash2wb.clear();
        extends_map.clear();
        chain_tips.clear();
        to_remove.clear();
        return;
    } else {
        for (uint256 hash : to_remove) {

            auto hash2wb_iter = hash2wb.find(hash);
            // FIXME: assert
            assert (hash2wb_iter != hash2wb.end());
            CWeakblockRef wb = hash2wb[hash];

            hash2wb.erase(hash2wb_iter);

            auto cheaphash2wb_iter = cheaphash2wb.find(hash.GetCheapHash());
            // cheap hashes might collide ...
            if (cheaphash2wb_iter != cheaphash2wb.end()) {
                cheaphash2wb.erase(cheaphash2wb_iter);
            }

            auto extends_iter = extends_map.find(hash);

            if (extends_iter != extends_map.end())
                extends_map.erase(extends_iter);

            auto wct_iter = find(chain_tips.begin(),
                                 chain_tips.end(),
                wb);
            if (wct_iter != chain_tips.end())
                chain_tips.erase(wct_iter);
        }
        to_remove.clear();
        for (auto hashwb_pair : hash2wb) {
            to_remove.insert(hashwb_pair.first);
            hashwb_pair.second->weak_height_cache_valid = false;
        }
    }
}

CWeakblockRef CWeakStore::byHash(const uint256& hash) const {
    AssertLockHeld(cs_weakblocks);
    if (hash2wb.count(hash))
        return hash2wb.at(hash);
    else return nullptr;
}

CWeakblockRef CWeakStore::byHash(const uint64_t& cheaphash) const {
    AssertLockHeld(cs_weakblocks);
    if (cheaphash2wb.count(cheaphash))
        return cheaphash2wb.at(cheaphash);
    else return nullptr;
}

CWeakblockRef CWeakStore::parent(const uint256& hash) const {
    AssertLockHeld(cs_weakblocks);
    if (extends_map.count(hash))
        return byHash(extends_map.at(hash));
    else return nullptr;
}

size_t CWeakStore::size() const { AssertLockHeld(cs_weakblocks); return hash2wb.size(); }
bool CWeakStore::empty() const { AssertLockHeld(cs_weakblocks); return size() == 0; }

void CWeakStore::consistencyCheck() const {
    LOCK(cs_weakblocks);
    LOG(WB, "Doing internal consistency check.\n");
    assert(hash2wb.count(uint256()) == 0);
    assert(extends_map.count(uint256()) == 0);
    assert(extends_map.size() <= hash2wb.size());
    assert(chain_tips.size() <= hash2wb.size());
    assert(cheaphash2wb.size() <= hash2wb.size());
    for (auto ext_pair : extends_map)
        assert(hash2wb.count(ext_pair.first) > 0);

    for (auto wb : chain_tips)
        assert(hash2wb.count(wb->GetHash()) > 0);

    // TODO: check that extends_map does not have any cycles
}

const std::vector<CWeakblockRef>& CWeakStore::chainTips() const {
    AssertLockHeld(cs_weakblocks);
    return chain_tips;
}

CWeakStore weakstore;
