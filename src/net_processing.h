// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include "net.h"

/** Process protocol messages received from a given node */
bool ProcessMessages(CNode *pfrom);


/** Process a single protocol messages received from a given node
    @param pfrom The node this message originated from
    @param strCommand The message type
    @param vRecv The message contents
    @param nStopwatchTimeReceived Stopwatch time in microseconds indicating when this message was received
*/
bool ProcessMessage(CNode *pfrom, std::string strCommand, CDataStream &vRecv, int64_t nStopwatchTimeReceived);

/**
 * Send queued protocol messages to be sent to a give node.
 *
 * @param[in]   pto             The node which we are sending messages to.
 */
bool SendMessages(CNode *pto);
// BU: moves to parallel.h
/** Run an instance of the script checking thread */
// void ThreadScriptCheck();

#endif
