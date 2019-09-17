// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "locklocation.h"

CLockLocation::CLockLocation(const char *pszName,
    const char *pszFile,
    int nLine,
    bool fTryIn,
    OwnershipType eOwnershipIn,
    LockType eLockTypeIn)
{
    mutexName = pszName;
    sourceFile = pszFile;
    sourceLine = nLine;
    fTry = fTryIn;
    eOwnership = eOwnershipIn;
    eLockType = eLockTypeIn;
    fWaiting = true;
}

std::string CLockLocation::ToString() const
{
    return mutexName + "  " + sourceFile + ":" + std::to_string(sourceLine) + (fTry ? " (TRY)" : "") +
           (eOwnership == OwnershipType::EXCLUSIVE ? " (EXCLUSIVE)" : "") + (fWaiting ? " (WAITING)" : "");
}

bool CLockLocation::GetTry() const { return fTry; }
OwnershipType CLockLocation::GetExclusive() const { return eOwnership; }
bool CLockLocation::GetWaiting() const { return fWaiting; }
void CLockLocation::ChangeWaitingToHeld() { fWaiting = false; }
LockType CLockLocation::GetLockType() const { return eLockType; }
std::string CLockLocation::GetFileName() const { return sourceFile; }
int CLockLocation::GetLineNumber() const { return sourceLine; }
std::string CLockLocation::GetMutexName() const { return mutexName; }
