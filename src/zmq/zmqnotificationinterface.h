<<<<<<< HEAD
// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
=======
// Copyright (c) 2015-2018 The Bitcoin Core developers
>>>>>>> 5df3c1d33e... RPC: Add new getzmqnotifications method.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H
#define BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H

#include "validationinterface.h"
#include <map>
#include <string>

class CBlockIndex;
class CZMQAbstractNotifier;

class CZMQNotificationInterface : public CValidationInterface
{
public:
    virtual ~CZMQNotificationInterface();

    static CZMQNotificationInterface *CreateWithArguments(const std::map<std::string, std::string> &args);
    std::list<const CZMQAbstractNotifier *> GetActiveNotifiers() const;

protected:
    bool Initialize();
    void Shutdown();

    // CValidationInterface
    void SyncTransaction(const CTransactionRef &ptx, const CBlock *pblock, int txIndex = -1);
    void UpdatedBlockTip(const CBlockIndex *pindex);
    void SyncDoubleSpend(const CTransactionRef ptx);

private:
    CZMQNotificationInterface();

    void *pcontext;
    std::list<CZMQAbstractNotifier *> notifiers;
};

extern CZMQNotificationInterface *g_zmq_notification_interface;

#endif // BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H
