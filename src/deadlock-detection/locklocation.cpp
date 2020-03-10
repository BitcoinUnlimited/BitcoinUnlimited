// Copyright (c) 2019 Greg Griffith
// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "locklocation.h"

#ifdef DEBUG_LOCKORDER // this ifdef covers the rest of the file

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
}

std::string CLockLocation::ToString() const
{
    return mutexName + "  " + sourceFile + ":" + std::to_string(sourceLine) + (fTry ? " (TRY)" : "") +
           (eOwnership == OwnershipType::EXCLUSIVE ? " (EXCLUSIVE)" : "(SHARED)") + "(HELD)";
}

bool CLockLocation::GetTry() const { return fTry; }
OwnershipType CLockLocation::GetExclusive() const { return eOwnership; }
LockType CLockLocation::GetLockType() const { return eLockType; }
std::string CLockLocation::GetFileName() const { return sourceFile; }
int CLockLocation::GetLineNumber() const { return sourceLine; }
std::string CLockLocation::GetMutexName() const { return mutexName; }
#endif // end DEBUG_LOCKORDER
