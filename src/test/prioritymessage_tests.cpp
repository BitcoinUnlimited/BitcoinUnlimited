// Copyright (c) 2014-2015 The Bitcoin Core developers
// Copyright (c) 2015-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "net.h"

#include "test/test_bitcoin.h"

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(prioritize_messages_tests, TestingSetup)

// Return the netmessage string for a block/xthin/graphene request
static uint256 GetMessageHash(CSerializeData &data)
{
    if (data.size() < 60)
        throw "data size not large enough";

    // Convert data to a string so we can pass this to a datastream insert.
    std::string strData(data.begin(), data.end());

    // Now copy the inv data to get the hash
    CInv inv;
    CDataStream ssInv(SER_NETWORK, PROTOCOL_VERSION);
    ssInv.insert(ssInv.begin(), strData.begin() + 24, strData.begin() + 60);
    ssInv >> inv;
    return inv.hash;
}

static bool CheckMsgQ(CNode *pnode, std::vector<CInv> &vMsgOrder)
{
    for (size_t i = 0; i < pnode->vSendMsg.size(); i++)
    {
        if (GetMessageHash(pnode->vSendMsg[i]) != vMsgOrder[i].hash)
            return false;
    }
    return true;
}

BOOST_AUTO_TEST_CASE(prioritize_messages)
{
    // create dummy test addrs
    CAddress addr_priorityq1(ipaddress(0xa0b0c001, 10000));
    CAddress addr_priorityq2(ipaddress(0xa0b0c002, 10000));

    // create test nodes
    CNode prioritynode1(INVALID_SOCKET, addr_priorityq1, "", true);
    CNode prioritynode2(INVALID_SOCKET, addr_priorityq2, "", true);

    // reusable datastream object
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    // reusable vector for testing positions of messages
    std::vector<CInv> vMsgQResult;

    // Test the prioritization of send messages (When we send priority messages we have
    // to put them in the correct position in the send queue). To test this we add various
    // priority and non-priority messages and check their relative positions in the send
    // queue.

    // There are two types of ordering, one where the first message may have been partially sent
    // and the other where it has not been partially sent. In the case of the first message
    // being partially sent, then the priority message would be placed behind that message.


    /** Test prioritization where the first message in queue has already been paritially sent on
     *  on the first peer and also where the first message has NOT been partially sent on the
     *  second peer.
     */

    // Set the nSendOffset to be either non-zero (partially sent) or zero (not partially sent).
    prioritynode1.nSendOffset = 1;
    prioritynode2.nSendOffset = 0;

    // Add three non-priority messages (msg1, msg2 and msg3) to prime the queue
    uint256 hash1 = GetRandHash();
    CInv msg1(MSG_TX, hash1);
    prioritynode1.PushMessage(NetMsgType::GETDATA, msg1);
    prioritynode2.PushMessage(NetMsgType::GETDATA, msg1);

    uint256 hash2 = GetRandHash();
    CInv msg2(MSG_TX, hash2);
    prioritynode1.PushMessage(NetMsgType::GETDATA, msg2);
    prioritynode2.PushMessage(NetMsgType::GETDATA, msg2);

    uint256 hash3 = GetRandHash();
    CInv msg3(MSG_TX, hash3);
    prioritynode1.PushMessage(NetMsgType::GETDATA, msg3);
    prioritynode2.PushMessage(NetMsgType::GETDATA, msg3);

    // Send one priority message (pri1) and verify that it is in the correct position in the queue. It
    // should be after the first non-priority message but before the second.
    // Result: Front of queue after sending pri1 => msg1 pri1 msg2 msg3
    uint256 hash4 = GetRandHash();
    CInv pri1(MSG_GRAPHENEBLOCK, hash4);
    ss.clear();
    ss << pri1;
    ss << 500; // just some random number
    prioritynode1.PushMessage(NetMsgType::GET_GRAPHENE, ss);
    prioritynode2.PushMessage(NetMsgType::GET_GRAPHENE, ss);

    // verify relative position of each message
    vMsgQResult.clear();
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    BOOST_CHECK(CheckMsgQ(&prioritynode1, vMsgQResult));

    vMsgQResult.clear();
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    BOOST_CHECK(CheckMsgQ(&prioritynode2, vMsgQResult));

    // Send a second priority message. Verify that it is, positionally, just behind the first priority
    // message sent above.
    // Result: Front of queue after sending pri2 => msg1 pri1 pri2 msg2 msg3
    uint256 hash5 = GetRandHash();
    CInv pri2(MSG_GRAPHENEBLOCK, hash5);
    ss.clear();
    ss << pri2;
    ss << 500; // just some random number
    prioritynode1.PushMessage(NetMsgType::GET_GRAPHENE, ss);
    prioritynode2.PushMessage(NetMsgType::GET_GRAPHENE, ss);

    // verify relative position of each message
    vMsgQResult.clear();
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(pri2);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    BOOST_CHECK(CheckMsgQ(&prioritynode1, vMsgQResult));

    vMsgQResult.clear();
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(pri2);
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    BOOST_CHECK(CheckMsgQ(&prioritynode2, vMsgQResult));

    // Send a third priority message. Verify that it is, positionally, just behind the second
    // priority message sent above.
    // Result: Front of queue after sending pri3 => msg1 pri1 pri2 pri3 msg2 msg3
    uint256 hash6 = GetRandHash();
    CInv pri3(MSG_GRAPHENEBLOCK, hash6);
    ss.clear();
    ss << pri3;
    ss << 500; // just some random number
    prioritynode1.PushMessage(NetMsgType::GET_GRAPHENE, ss);
    prioritynode2.PushMessage(NetMsgType::GET_GRAPHENE, ss);

    // verify relative position of each message
    vMsgQResult.clear();
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(pri2);
    vMsgQResult.push_back(pri3);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    BOOST_CHECK(CheckMsgQ(&prioritynode1, vMsgQResult));

    vMsgQResult.clear();
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(pri2);
    vMsgQResult.push_back(pri3);
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    BOOST_CHECK(CheckMsgQ(&prioritynode2, vMsgQResult));

    // Send a non priority message and verify that it is behind all others and the priority
    // messages still maintain their positions in the queue
    // Result: Front of queue after sending msg4 => msg1 pri1 pri2 pri3 msg2 msg3 msg4
    uint256 hash7 = GetRandHash();
    CInv msg4(MSG_TX, hash7);
    prioritynode1.PushMessage(NetMsgType::GETDATA, msg4);
    prioritynode2.PushMessage(NetMsgType::GETDATA, msg4);

    // verify relative position of each message
    vMsgQResult.clear();
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(pri2);
    vMsgQResult.push_back(pri3);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    vMsgQResult.push_back(msg4);
    BOOST_CHECK(CheckMsgQ(&prioritynode1, vMsgQResult));

    vMsgQResult.clear();
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(pri2);
    vMsgQResult.push_back(pri3);
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    vMsgQResult.push_back(msg4);
    BOOST_CHECK(CheckMsgQ(&prioritynode2, vMsgQResult));

    // Send a fourth priority message. Verify that it is, positionally, just behind the third
    // priority message but in front of the non-priority message.
    // Result: Front of queue after sending pri4 => msg1 pri1 pri2 pri3 pri4 msg2 msg3 msg4
    uint256 hash8 = GetRandHash();
    CInv pri4(MSG_GRAPHENEBLOCK, hash8);
    ss.clear();
    ss << pri4;
    ss << 500; // just some random number
    prioritynode1.PushMessage(NetMsgType::GET_GRAPHENE, ss);
    prioritynode2.PushMessage(NetMsgType::GET_GRAPHENE, ss);

    // verify relative position of each message
    vMsgQResult.clear();
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(pri2);
    vMsgQResult.push_back(pri3);
    vMsgQResult.push_back(pri4);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    vMsgQResult.push_back(msg4);
    BOOST_CHECK(CheckMsgQ(&prioritynode1, vMsgQResult));

    vMsgQResult.clear();
    vMsgQResult.push_back(pri1);
    vMsgQResult.push_back(pri2);
    vMsgQResult.push_back(pri3);
    vMsgQResult.push_back(pri4);
    vMsgQResult.push_back(msg1);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    vMsgQResult.push_back(msg4);
    BOOST_CHECK(CheckMsgQ(&prioritynode2, vMsgQResult));

    /** test case where the first two messages were sent on the first peer so that we no longer
     *  have a partially sent message at the front of the queue, and then we add another prioirty
     *  message to the queue.
     */
    // Remove msg1 and pri1 and then send another priority message. Verify the new priority message
    // is behind the fourth priority message sent but in front of the non-priority messages
    // Result: Front of queue after sending pri5 => pri2 pri3 pri4 pri5 msg2 msg3 msg4
    prioritynode1.vSendMsg.pop_front();
    prioritynode1.vSendMsg.pop_front();
    vPrioritySendQ.pop_front();
    prioritynode1.nSendOffset = 0;

    uint256 hash9 = GetRandHash();
    CInv pri5(MSG_GRAPHENEBLOCK, hash9);
    ss.clear();
    ss << pri5;
    ss << 500; // just some random number
    prioritynode1.PushMessage(NetMsgType::GET_GRAPHENE, ss);

    // verify relative position of each message
    vMsgQResult.clear();
    vMsgQResult.push_back(pri2);
    vMsgQResult.push_back(pri3);
    vMsgQResult.push_back(pri4);
    vMsgQResult.push_back(pri5);
    vMsgQResult.push_back(msg2);
    vMsgQResult.push_back(msg3);
    vMsgQResult.push_back(msg4);
    BOOST_CHECK(CheckMsgQ(&prioritynode1, vMsgQResult));


    // cleanup
    vPrioritySendQ.clear();
}

BOOST_AUTO_TEST_SUITE_END()
