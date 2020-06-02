// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dag.h"

CDagNode::CDagNode(CSubBlock _subblock)
{
    hash = _subblock.GetHash();
    subblock = _subblock;
    subgraph_id = -1;
}

void CDagNode::AddAncestor(CDagNode* ancestor)
{
    ancestors.emplace(ancestor);
}

void CDagNode::AddDescendant(CDagNode* descendant)
{
    descendants.emplace(descendant);
}

// there is nothing below it
bool CDagNode::IsBase()
{
    return ancestors.empty();
}

// there is nothing above it
bool CDagNode::IsTip()
{
    return descendants.empty();
}

uint16_t CDagNode::GetNodeScore()
{
    uint16_t score = 0;
    std::vector<uint16_t> num_desc_gen;
    std::set<CDagNode*> children;
    std::set<CDagNode*> next_children;
    if (this->IsTip())
    {
        return 0;
    }
    children.insert(this->descendants.begin(), this->descendants.end());
    num_desc_gen.push_back(children.size());
    while(children.empty() == false)
    {
        for (auto &child : children)
        {
            // TODO : this means some nodes will be counted twice if they use two ancestors from different levels
            // unsure how we should handle this scenario
            next_children.insert(child->descendants.begin(), child->descendants.end());
        }
        num_desc_gen.push_back(next_children.size());
        children.clear();
        std::swap(children, next_children);
    }
    for (uint16_t i = 0; i < num_desc_gen.size(); ++i)
    {
        score = (score + (num_desc_gen[i] * (i + 1)));
    }
    return score;
}

bool CDagNode::IsValid()
{
    return (subblock.IsNull() == false && subgraph_id >= 0);
}

int16_t CDagForrest::MergeTrees(const std::set<int16_t> &tree_ids)
{
    int16_t new_subgraph_id = next_subgraph_id;
    next_subgraph_id++;
    for (auto &node : _dag)
    {
        if (tree_ids.count(node->subgraph_id))
        {
            node->subgraph_id = new_subgraph_id;
        }
    }
    return new_subgraph_id;
}

void CDagForrest::Clear()
{
    next_subgraph_id = 0;
    _dag.clear();
}

CDagNode* CDagForrest::Find(const uint256 &hash)
{
    // TODO : replace with std::find
    auto iter = _dag.begin();
    while (iter != _dag.end())
    {
        CDagNode* temp = *iter;
        if (temp->hash == hash)
        {
            return *iter;
        }
        ++iter;
    }
    return nullptr;
}

bool CDagForrest::Insert(const CSubBlock &sub_block)
{
    uint256 sub_block_hash = sub_block.GetHash();
    CDagNode* temp = Find(sub_block_hash);
    if (temp != nullptr)
    {
        // we have this node.
        return false;
    }

    // Create newz
    CDagNode *newNode = new CDagNode(sub_block);
    std::set<int16_t> merge_list;
    for (auto &hash : sub_block.GetAncestorHashes())
    {
        CDagNode* ancestor = Find(hash);
        if (ancestor == nullptr)
        {
            // TODO : A subblock is missing, try to re-request it or something
            continue;
        }
        newNode->AddAncestor(ancestor);
        merge_list.emplace(ancestor->subgraph_id);
        ancestor->AddDescendant(newNode);
    }
    int16_t new_id = -1;
    if (merge_list.size() > 1)
    {
        new_id = MergeTrees(merge_list);
    }
    else if (merge_list.size() == 1)
    {
        new_id = *(merge_list.begin());
    }
    else // if(merge_list.size() == 0)
    {
        new_id = next_subgraph_id;
        next_subgraph_id = next_subgraph_id + 1;
    }
    newNode->subgraph_id = new_id;
    // TODO : should insert to maintain temporal ordering not just emplace_back
    _dag.emplace_back(newNode);
    return true;
}

void CDagForrest::TemporalSort()
{

}

bool CDagForrest::IsTemporallySorted()
{
    return true;
}

bool CDagForrest::IsSubgraphValid(std::set<uint256> sgHashes)
{
    // TODO : replace 15 with consensus k variable
    if (sgHashes.size() < 15)
    {
        // subgraph not large enough
        return false;
    }
    // check if we have all subblocks in our dag
    std::set<CDagNode*> subgraph;
    for (auto &hash : sgHashes)
    {
        CDagNode* node = Find(hash);
        if(node == nullptr)
        {
            // missing a subblock, we need to request it
            // TODO : request missing subblock or throw an error or something
            return false;
        }
        if(node->IsValid() == false)
        {
            // there was an error somewhere and this node does not have valid data
            return false;
        }
        subgraph.emplace(node);
    }
    int16_t tree_id = -1;
    for (auto &node : subgraph)
    {
        if (tree_id < 0)
        {
            tree_id = node->subgraph_id;
        }
        if (tree_id != node->subgraph_id)
        {
            // TODO : handle this somewhow
            // nodes dont all belong to the same subbgraph
            // this can cause a false positive if we are missing a proof that links the trees
            // as long as that proof isnt also in the nodes selected for the subgraph
            return false;
        }
    }
    return true;

}

// K should be 15
std::set<CDagNode*> CDagForrest::GetBestSubgraph(const uint8_t &k)
{
    std::set<CDagNode*> Kset;
    if (_dag.size() < k)
    {
        for (auto &entry : _dag)
        {
            Kset.emplace(entry);
        }
        return Kset;
    }
    std::set<CDagNode*> subgraph;
    auto iter = _dag.end();
    uint8_t j = 0;
    while (j < k)
    {
        iter--;
        Kset.emplace(*iter);
        ++j;
    }
    return Kset;
}
