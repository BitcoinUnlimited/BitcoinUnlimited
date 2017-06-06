// Copyright (c) 2017 The Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "unlimited.h"
#include "buip055fork.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "clientversion.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "expedited.h"
#include "hash.h"
#include "leakybucket.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "parallel.h"
#include "policy/policy.h"
#include "primitives/block.h"
#include "requestManager.h"
#include "rpc/server.h"
#include "stat.h"
#include "thinblock.h"
#include "timedata.h"
#include "tinyformat.h"
#include "tweak.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "version.h"

#include <atomic>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <inttypes.h>
#include <iomanip>
#include <limits>
#include <queue>


bool ValidateBUIP055Block(const CBlock &block, CValidationState &state)
{
    // Validate transactions are HF compatible
    for (const CTransaction &tx : block.vtx)
    {
        if (!ValidateBUIP055Tx(tx))
            return state.DoS(100,
                             error("transaction is invalid on BUIP055 chain"), REJECT_INVALID, "bad-txns-wrong-fork");
    }
    return true;
}

bool ValidateBUIP055Tx(const CTransaction& tx)
{
    // TODO: validate transactions are HF compatible

    return true;
}
