// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BOBTAIL_DAG_H
#define BITCOIN_BOBTAIL_DAG_H

#include "uint256.h"
#include "subblock.h"

#include <deque>
#include <set>

class CDagNode
{
public:
    uint256 hash; // the weakblock hash that is this node
    int16_t subgraph_id; // cannot be negative

    CSubBlock subblock;

    std::set<CDagNode*> ancestors; // should point to the nodes of the parentHashes
    std::set<CDagNode*> descendants; // points to the nodes of the children

private:
    CDagNode(){} // disable default constructor

public:
    CDagNode(CSubBlock _subblock);
    void AddAncestor(CDagNode* ancestor);
    void AddDescendant(CDagNode* descendant);
    bool IsBase();
    bool IsTip();
    uint16_t GetNodeScore();
    bool IsValid();
};

class CDagForrest
{
protected:
    int16_t next_subgraph_id;
    std::deque<CDagNode*> _dag;

    int16_t MergeTrees(const std::set<int16_t> &tree_ids);

public:
    CDagForrest()
    {
        Clear();
    }

    void Clear();

    CDagNode* Find(const uint256 &hash);

    bool Insert(const CSubBlock &sub_block);
    void TemporalSort();
    bool IsTemporallySorted();
    bool IsSubgraphValid(std::set<uint256> sgHashes);
    std::set<CDagNode*> GetBestSubgraph(const uint8_t &k);
};

#endif
