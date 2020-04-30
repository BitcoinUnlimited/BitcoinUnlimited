// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_MERKLE_H
#define BITCOIN_CONSENSUS_MERKLE_H

#include <stdint.h>
#include <vector>

#include "primitives/block.h"
#include "primitives/transaction.h"
#include "uint256.h"

uint256 ComputeMerkleRoot(std::vector<uint256> hashes, bool *mutated = nullptr);

/*
To compute a merkle path (AKA merkle proof), pass the index of the element being proved into position.
The merkle proof will be returned, not including the element.
For example, ComputeMerkleBranch(4 elements, 0) will return:
[ element[1], Hash256(element[2], element[3]) ]
*/
std::vector<uint256> ComputeMerkleBranch(const std::vector<uint256> &leaves, uint32_t position);


/* To verify a merkle proof, pass the hash of the element in "leaf", the merkle proof in "branch", and the zero-based
index specifying where the element was in the array when the merkle proof was created.
*/
uint256 ComputeMerkleRootFromBranch(const uint256 &leaf, const std::vector<uint256> &branch, uint32_t position);

/*
 * Compute the Merkle root of the transactions in a block.
 * *mutated is set to true if a duplicated subtree was found.
 */
uint256 BlockMerkleRoot(const CBlock &block, bool *mutated = nullptr);

/*
 * Compute the Merkle branch for the tree of transactions in a block, for a
 * given position.
 * This can be verified using ComputeMerkleRootFromBranch.
 */
std::vector<uint256> BlockMerkleBranch(const CBlock &block, uint32_t position);

#endif // BITCOIN_CONSENSUS_MERKLE_H
