// Copyright (c) 2017 The Bitcoin Developers
// Copyright (c) 2017-2019 The Bitcoin Unlimited Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_BITCOINADDRESSVALIDATORTESTS_H
#define BITCOIN_QT_TEST_BITCOINADDRESSVALIDATORTESTS_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#include <QObject>
#include <QTest>
#pragma GCC diagnostic pop

class BitcoinAddressValidatorTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void inputTests();
};

#endif
