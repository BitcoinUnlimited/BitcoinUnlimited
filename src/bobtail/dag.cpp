// Copyright (c) 2020 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dag.h"

#include "consensus/consensus.h"

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
    return (subblock.IsNull() == false && dag_id >= 0);
}

void CBobtailDag::SetId(int16_t new_id)
{
    id = new_id;
}

bool CBobtailDag::Insert(CDagNode* new_node)
{
    std::set<COutPoint> new_spends;
    for (auto &tx : new_node->subblock.vtx)
    {
        if (tx->IsProofBase() == false)
        {
            for (auto &input : tx->vin)
            {
                // TODO : change to contains in c++17
                if (spent_outputs.count(input.prevout) != 0)
                {
                    return false;
                }
                new_spends.emplace(input.prevout);
            }
        }
    }
    // change to merge in c++17
    spent_outputs.insert(new_spends.begin(), new_spends.end());
    _dag.emplace_back(new_node);
    return true;
}

void CBobtailDagSet::SetNewIds(std::priority_queue<int16_t> &removed_ids)
{
    int16_t last_value;
    for (auto riter = vdags.rbegin(); riter != vdags.rend(); ++riter)
    {
        last_value = removed_ids.top();
        // TODO : dont use assert here
        assert(riter->id != last_value);
        if (riter->id > last_value)
        {
            riter->id = riter->id - removed_ids.size();
        }
        else // <
        {
            removed_ids.pop();
            riter->id = riter->id - removed_ids.size();
        }
        if (removed_ids.empty())
        {
            break;
        }
    }
    // do a check to ensure everything lines up
    for (size_t i = 0; i < vdags.size(); ++i)
    {
        // TODO : dont use assert here
        assert(i == vdags[i].id);
        for (auto &node : vdags[i]._dag)
        {
            node->dag_id = i;
        }
    }
}

bool CBobtailDagSet::MergeDags(std::set<int16_t> &tree_ids, int16_t &new_id)
{
    int16_t base_dag_id = *(tree_ids.begin());
    // remove the first element, it is not being deleted
    tree_ids.erase(tree_ids.begin());
    for (auto &id : tree_ids)
    {
        if (id < 0 || (size_t)id >= vdags.size())
        {
            return false;
        }
        for (CDagNode* node : vdags[id]._dag)
        {
            vdags[base_dag_id].Insert(node);
        }
    }
    std::priority_queue<int16_t> removed_ids;
    // erase after we move all nodes to ensure indexes still align
    // go in reverse order so indexes still align
    for (auto riter = tree_ids.rbegin(); riter != tree_ids.rend(); ++riter)
    {
        vdags.erase(vdags.begin() + (*riter));
        removed_ids.push(*riter);
    }
    SetNewIds(removed_ids);
    new_id = base_dag_id;
    return true;
}

void CBobtailDagSet::Clear()
{
    vdags.clear();
}

CDagNode* CBobtailDagSet::Find(const uint256 &hash)
{
    std::map<uint256, CDagNode*>::iterator iter = mapAllNodes.find(hash);
    if (iter != mapAllNodes.end())
    {
        return iter->second;
    }
    return nullptr;
}

bool CBobtailDagSet::Insert(const CSubBlock &sub_block)
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
        merge_list.emplace(ancestor->dag_id);
        ancestor->AddDescendant(newNode);
    }
    int16_t new_id = -1;
    if (merge_list.size() > 1)
    {
        if (!MergeDags(merge_list, new_id))
        {
            return false;
        }
    }
    else if (merge_list.size() == 1)
    {
        new_id = *(merge_list.begin());
    }
    else // if(merge_list.size() == 0)
    {
        new_id = vdags.size();
        vdags.emplace_back(new_id, newNode);
    }
    newNode->dag_id = new_id;
    vdags[new_id].Insert(newNode);
    // TODO : should insert to maintain temporal ordering not just emplace_back
    mapAllNodes.emplace(newNode->hash, newNode);
    return true;
}

void CBobtailDagSet::TemporalSort()
{

}

bool CBobtailDagSet::IsTemporallySorted()
{
    return true;
}

bool CBobtailDagSet::GetBestDag(std::set<CDagNode*> &dag)
{
    if (vdags.empty())
    {
        return false;
    }
    int16_t best_dag = -1;
    uint64_t best_dag_score = -1;
    // Get all dags that are big enough
    for (size_t i = 0; i < vdags.size(); ++i)
    {
        if (vdags[i]._dag.size() < BOBTAIL_K)
        {
            continue;
        }
        if (vdags[i].score > best_dag_score)
        {
            best_dag = i;
        }
    }
    if (best_dag < 0)
    {
        // should never happen
        return false;
    }
    for (auto& node :vdags[best_dag]._dag)
    {
        dag.emplace(node);
    }
    return true;
}
