// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2017-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_GUIUTILTESTS_H
#define BITCOIN_QT_TEST_GUIUTILTESTS_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#include <QObject>
#include <QTest>
#pragma GCC diagnostic pop

class GUIUtilTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void dummyAddressTest();
    void toCurrentEncodingTest();
};

#endif // BITCOIN_QT_TEST_GUIUTILTESTS_H
