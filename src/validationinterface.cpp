// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validationinterface.h"
#include <boost/bind/bind.hpp>

static CMainSignals g_signals;

CMainSignals &GetMainSignals() { return g_signals; }
void RegisterValidationInterface(CValidationInterface *pwalletIn)
{
    g_signals.UpdatedBlockTip.connect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, boost::arg<1>()));
    g_signals.SyncTransaction.connect(boost::bind(
        &CValidationInterface::SyncTransaction, pwalletIn, boost::arg<1>(), boost::arg<2>(), boost::arg<3>()));
    g_signals.UpdatedTransaction.connect(
        boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, boost::arg<1>()));
    g_signals.SetBestChain.connect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, boost::arg<1>()));
    g_signals.Inventory.connect(boost::bind(&CValidationInterface::Inventory, pwalletIn, boost::arg<1>()));
    g_signals.Broadcast.connect(
        boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, boost::arg<1>()));
    g_signals.BlockChecked.connect(
        boost::bind(&CValidationInterface::BlockChecked, pwalletIn, boost::arg<1>(), boost::arg<2>()));
    g_signals.ScriptForMining.connect(
        boost::bind(&CValidationInterface::GetScriptForMining, pwalletIn, boost::arg<1>()));
    g_signals.BlockFound.connect(boost::bind(&CValidationInterface::ResetRequestCount, pwalletIn, boost::arg<1>()));
}

void UnregisterValidationInterface(CValidationInterface *pwalletIn)
{
    g_signals.BlockFound.disconnect(boost::bind(&CValidationInterface::ResetRequestCount, pwalletIn, boost::arg<1>()));
    g_signals.ScriptForMining.disconnect(
        boost::bind(&CValidationInterface::GetScriptForMining, pwalletIn, boost::arg<1>()));
    g_signals.BlockChecked.disconnect(
        boost::bind(&CValidationInterface::BlockChecked, pwalletIn, boost::arg<1>(), boost::arg<2>()));
    g_signals.Broadcast.disconnect(
        boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, boost::arg<1>()));
    g_signals.Inventory.disconnect(boost::bind(&CValidationInterface::Inventory, pwalletIn, boost::arg<1>()));
    g_signals.SetBestChain.disconnect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, boost::arg<1>()));
    g_signals.UpdatedTransaction.disconnect(
        boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, boost::arg<1>()));
    g_signals.SyncTransaction.disconnect(boost::bind(
        &CValidationInterface::SyncTransaction, pwalletIn, boost::arg<1>(), boost::arg<2>(), boost::arg<3>()));
    g_signals.UpdatedBlockTip.disconnect(
        boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, boost::arg<1>()));
}

void UnregisterAllValidationInterfaces()
{
    g_signals.BlockFound.disconnect_all_slots();
    g_signals.ScriptForMining.disconnect_all_slots();
    g_signals.BlockChecked.disconnect_all_slots();
    g_signals.Broadcast.disconnect_all_slots();
    g_signals.Inventory.disconnect_all_slots();
    g_signals.SetBestChain.disconnect_all_slots();
    g_signals.UpdatedTransaction.disconnect_all_slots();
    g_signals.SyncTransaction.disconnect_all_slots();
    g_signals.UpdatedBlockTip.disconnect_all_slots();
}

void SyncWithWallets(const CTransactionRef &ptx, const CBlock *pblock, int txIdx)
{
    g_signals.SyncTransaction(ptx, pblock, txIdx);
}
