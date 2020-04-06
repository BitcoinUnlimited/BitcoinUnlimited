// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dosman.h"
#include "bandb.h"
#include "connmgr.h"
#include "ui_interface.h"

CDoSManager::CDoSManager() : setBannedIsDirty(false), nBanThreshold(DEFAULT_BANSCORE_THRESHOLD) {}
/**
 * Call once the command line is parsed so dosman configures itself appropriately.
 */
void CDoSManager::HandleCommandLine() { nBanThreshold = GetArg("-banscore", DEFAULT_BANSCORE_THRESHOLD); }
/**
* Checks if this CNetAddr is in the whitelist
*
* @param[in] addr  The address to check
* @return true if address is in the whitelist, otherwise false
*/
bool CDoSManager::IsWhitelistedRange(const CNetAddr &addr)
{
    LOCK(cs_vWhitelistedRange);
    for (const CSubNet &subnet : vWhitelistedRange)
    {
        if (subnet.Match(addr))
            return true;
    }
    return false;
}

/**
* Add this CSubNet to the whitelist
*
* @param[in] subnet  The subnet to add to the whitelist
*/
void CDoSManager::AddWhitelistedRange(const CSubNet &subnet)
{
    LOCK(cs_vWhitelistedRange);
    vWhitelistedRange.push_back(subnet);
}

/**
* Remove all in-memory ban entries
* Marks the in-memory banlist as dirty
*/
void CDoSManager::ClearBanned()
{
    LOCK(cs_setBanned);
    setBanned.clear();
    setBannedIsDirty = true;
    uiInterface.BannedListChanged();
}

/**
* Check to see if this CNetAddr is currently banned
*
* @param[in] ip  The address to check
* @return true if address currently banned, otherwise false
*/
bool CDoSManager::IsBanned(CNetAddr ip)
{
    LOCK(cs_setBanned);
    for (banmap_t::iterator it = setBanned.begin(); it != setBanned.end(); it++)
    {
        CSubNet subNet = (*it).first;
        CBanEntry banEntry = (*it).second;

        // As soon as we find a matching ban that isn't expired return immediately
        if (subNet.Match(ip) && GetTime() < banEntry.nBanUntil)
            return true;
    }

    // If we got here, we traversed the list and didn't find any non-expired bans for this IP
    return false;
}

/**
* Check if this CSubNet is currently banned
*
* @param[in] subnet  The subnet to check
* @return true if subnet currently banned, otherwise false
*/
bool CDoSManager::IsBanned(CSubNet subnet)
{
    LOCK(cs_setBanned);
    banmap_t::iterator i = setBanned.find(subnet);
    if (i != setBanned.end())
    {
        CBanEntry banEntry = (*i).second;
        // As soon as we find a matching ban that isn't expired return immediately
        if (GetTime() < banEntry.nBanUntil)
            return true;
    }

    // If we got here, we traversed the list and didn't find any non-expired bans for this exact subnet
    return false;
}

/**
* Add this CNetAddr to the banlist for the specified duration
* Marks the in-memory banlist as dirty
*
* @param[in] addr            The address to ban
* @param[in] banReason       The reason for banning this address
* @param[in] bantimeoffset   The duration of the ban in seconds, either a duration or an absolute time
* @param[in] sinceUnixEpoch  Whether or not the bantimeoffset is a relative duration, or absolute time
*/
void CDoSManager::Ban(const CNetAddr &addr,
    const std::string &userAgent,
    const BanReason &banReason,
    int64_t bantimeoffset,
    bool sinceUnixEpoch)
{
    CSubNet subNet(addr);
    Ban(subNet, userAgent, banReason, bantimeoffset, sinceUnixEpoch);
}

/**
* Add this CSubNet to the banlist for the specified duration
* Marks the in-memory banlist as dirty
*
* @param[in] addr            The subnet to ban
* @param[in] banReason       The reason for banning this subnet
* @param[in] bantimeoffset   The duration of the ban in seconds, either a duration or an absolute time
* @param[in] sinceUnixEpoch  Whether or not the bantimeoffset is a relative duration, or absolute time
*/
void CDoSManager::Ban(const CSubNet &subNet,
    const std::string &userAgent,
    const BanReason &banReason,
    int64_t bantimeoffset,
    bool sinceUnixEpoch)
{
    CBanEntry banEntry(GetTime());
    banEntry.userAgent = userAgent;
    banEntry.banReason = banReason;
    if (bantimeoffset <= 0)
    {
        bantimeoffset = GetArg("-bantime", DEFAULT_MISBEHAVING_BANTIME);
        sinceUnixEpoch = false;
    }
    banEntry.nBanUntil = (sinceUnixEpoch ? 0 : GetTime()) + bantimeoffset;


    LOCK(cs_setBanned);
    if (setBanned[subNet].nBanUntil < banEntry.nBanUntil)
        setBanned[subNet] = banEntry;

    setBannedIsDirty = true;
    uiInterface.BannedListChanged();
}

/**
* Remove this CNetAddr from the banlist
* Marks the in-memory banlist as dirty if address was found and removed
*
* @param[in] addr  The address to unban
* @return true if address was in the ban list and was removed, otherwise false
*/
bool CDoSManager::Unban(const CNetAddr &addr)
{
    CSubNet subNet(addr);
    return Unban(subNet);
}

/**
* Remove this CSubNet from the banlist
* Marks the in-memory banlist as dirty if subnet was found and removed
*
* @param[in] subNet  The subnet to unban
* @return true if subnet was in the ban list and was removed, otherwise false
*/
bool CDoSManager::Unban(const CSubNet &subNet)
{
    LOCK(cs_setBanned);
    if (setBanned.erase(subNet))
    {
        setBannedIsDirty = true;

        SweepBannedInternal();
        uiInterface.BannedListChanged();
        return true;
    }
    return false;
}

/**
* Copies current in-memory banlist to the passed in banmap_t
* Intended to allow read-only actions on the banlist without holding the lock
*
* @param[in,out] banMap  The banlist copy
*/
void CDoSManager::GetBanned(banmap_t &banMap)
{
    LOCK(cs_setBanned);
    SweepBannedInternal();
    GetBannedInternal(banMap); // create a thread safe copy
}

/**
* Copies current in-memory banlist to the passed in banmap_t
* Intended to allow read-only actions on the banlist without holding the lock
*
* @param[in,out] banMap  The banlist copy
*/
void CDoSManager::GetBannedInternal(banmap_t &banMap) EXCLUSIVE_LOCKS_REQUIRED(cs_setBanned)
{
    // Ensure lock is held externally as it is required for this internal version of the method
    AssertLockHeld(cs_setBanned);

    SweepBannedInternal();
    banMap = setBanned; // create a thread safe copy
}

/**
* Iterates the in-memory banlist and removes any ban entries where the ban has expired
* Marks the in-memory banlist as dirty if any entries were removed
*/
void CDoSManager::SweepBanned()
{
    LOCK(cs_setBanned);
    SweepBannedInternal();
}

/**
* Iterates the in-memory banlist and removes any ban entries where the ban has expired
* Marks the in-memory banlist as dirty if any entries were removed
*
* This is the internal version of the function which requres a lock is held externally
* This helps avoid taking recursive locks internally
*/
void CDoSManager::SweepBannedInternal() EXCLUSIVE_LOCKS_REQUIRED(cs_setBanned)
{
    // Ensure lock is held externally as it is required for this internal version of the method
    AssertLockHeld(cs_setBanned);

    int64_t now = GetTime();
    banmap_t::iterator it = setBanned.begin();
    while (it != setBanned.end())
    {
        CSubNet subNet = (*it).first;
        CBanEntry banEntry = (*it).second;
        if (now > banEntry.nBanUntil)
        {
            setBanned.erase(it++);
            setBannedIsDirty = true;
            LOG(NET, "%s: Removed banned node ip/subnet from banlist.dat: %s\n", __func__, subNet.ToString());
        }
        else
            ++it;
    }
}

/**
* Check if the current banlist has changes not written to disk
*
* @return true if the in-memory banlist has changes not written to disk, otherwise false
*/
bool CDoSManager::BannedSetIsDirty()
{
    LOCK(cs_setBanned);
    return setBannedIsDirty;
}

/**
 * Increment the misbehaving count score for this node.  If the ban threshold is reached, flag the node to be
 * banned.  No locks are needed to call this function.
 */
void CDoSManager::Misbehaving(CNode *pNode, int howmuch, BanReason reason)
{
    if (howmuch == 0 || !pNode)
        return;

    // Update the old misbehavior
    UpdateMisbehavior(pNode);

    // Add the new misbehavior and check whether to ban
    double prior = pNode->nMisbehavior.load();
    while (true)
    {
        if (pNode->nMisbehavior.compare_exchange_weak(prior, prior + howmuch))
            break;
        prior = pNode->nMisbehavior.load();
    }
    if (pNode->nMisbehavior.load() >= nBanThreshold && prior < nBanThreshold)
    {
        LOGA("%s: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", __func__, pNode->GetLogName(), prior, prior + howmuch);
        pNode->fShouldBan.store(true);
        pNode->nBanType.store(reason);
    }
    else
        LOGA("%s: %s (%d -> %d)\n", __func__, pNode->GetLogName(), prior, prior + howmuch);
}

/** Update the current values of misbehavior by decaying them over a set time period. */
void CDoSManager::UpdateMisbehavior(CNode *pNode)
{
    if (!pNode)
        return;

    if (pNode->nLastMisbehaviorTime.load() == 0)
        pNode->nLastMisbehaviorTime = GetTime();

    // Decay the previous misbehavior over a four hour window
    int64_t nNow = GetTime();
    while (true)
    {
        double nOldMisBehavior = pNode->nMisbehavior.load();
        if (nOldMisBehavior == 0.0)
            break;
        double nNewMisBehavior =
            nOldMisBehavior * pow(1.0 - 1.0 / 14400, (double)(nNow - pNode->nLastMisbehaviorTime.load()));
        if (pNode->nMisbehavior.compare_exchange_weak(nOldMisBehavior, nNewMisBehavior))
            break;
    }
    pNode->nLastMisbehaviorTime.store(nNow);
}

/**
 * Increment the misbehaving count score for this node.  If the ban threshold is reached, flag the node to be
 * banned.  No locks are needed to call this function.
 */
void CDoSManager::Misbehaving(NodeId nodeid, int howmuch)
{
    CNodeRef nodeRef(connmgr->FindNodeFromId(nodeid));

    Misbehaving(nodeRef.get(), howmuch);
}


/**
* Write in-memory banmap to disk
*/
void CDoSManager::DumpBanlist()
{
    int64_t nStart = GetTimeMillis();
    banmap_t banmap;
    {
        LOCK(cs_setBanned);
        // If setBanned is not dirty, don't waste time on disk i/o
        if (!setBannedIsDirty)
            return;

        // Get thread-safe copy of the current banlist
        GetBannedInternal(banmap);

        // Set the dirty flag to false in anticipation of successful flush to disk
        // In the event that the flush fails, we will set the flag back to true below
        // This needs to be done before the current lock is released in case another thread
        // dirties the banlist between now and completion of the write to disk
        setBannedIsDirty = false;
    }

    // Don't hold the lock while performing disk I/O
    CBanDB bandb;
    if (!bandb.Write(banmap))
    {
        // If the write to disk failed we need to set the dirty flag to true
        LOCK(cs_setBanned);
        setBannedIsDirty = true;
    }
    else
        LOG(NET, "Flushed %d banned node ips/subnets to banlist.dat  %dms\n", banmap.size(), GetTimeMillis() - nStart);
}

/**
* Read banmap from disk into memory
*/
void CDoSManager::LoadBanlist()
{
    uiInterface.InitMessage(_("Loading banlist..."));

    // Load addresses from banlist.dat
    int64_t nStart = GetTimeMillis();
    CBanDB bandb;
    banmap_t banmap;
    if (bandb.Read(banmap))
    {
        LOCK(cs_setBanned);
        setBanned = banmap;
        // We just set the in memory banlist to the values from disk, so indicate banlist is not dirty
        setBannedIsDirty = false;
        // Remove any ban entries that were persisted to disk but have since expired
        SweepBannedInternal();

        LOG(NET, "Loaded %d banned node ips/subnets from banlist.dat  %dms\n", banmap.size(), GetTimeMillis() - nStart);
    }
    else
        LOGA("Invalid or missing banlist.dat; recreating\n");
}
