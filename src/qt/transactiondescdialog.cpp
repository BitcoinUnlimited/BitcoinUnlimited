// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactiondescdialog.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#include "ui_transactiondescdialog.h"
#pragma GCC diagnostic pop

#include "transactiontablemodel.h"

#include <QModelIndex>

TransactionDescDialog::TransactionDescDialog(const QModelIndex &idx, QWidget *parent)
    : QDialog(parent), ui(new Ui::TransactionDescDialog)
{
    ui->setupUi(this);
    QString desc = idx.data(TransactionTableModel::LongDescriptionRole).toString();
    ui->detailText->setHtml(desc);
}

TransactionDescDialog::~TransactionDescDialog() { delete ui; }
