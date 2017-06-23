// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_PUBLICLABELVIEW_H
#define BITCOIN_QT_PUBLICLABELVIEW_H

#include "guiutil.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"

#include <QWidget>
#include <QKeyEvent>

class PlatformStyle;
class TransactionFilterProxy;
class WalletModel;

QT_BEGIN_NAMESPACE
class QComboBox;
class QDateTimeEdit;
class QFrame;
class QLineEdit;
class QMenu;
class QModelIndex;
class QSignalMapper;
class QTableView;
QT_END_NAMESPACE

/** Widget showing the public labels in the blockchain.
  */
class PublicLabelView : public QWidget
{
    Q_OBJECT

public:
    explicit PublicLabelView(const PlatformStyle *platformStyle, QWidget *parent = 0);

    void setModel(WalletModel *model);

    // Date ranges for filter
    enum DateEnum
    {
        All,
        Today,
        ThisWeek,
        ThisMonth,
        LastMonth,
        ThisYear,
        Range
    };

    enum ColumnWidths {
        STATUS_COLUMN_WIDTH = 30,
        WATCHONLY_COLUMN_WIDTH = 23,
        DATE_COLUMN_WIDTH = 150,
        TYPE_COLUMN_WIDTH = 0,
        AMOUNT_MINIMUM_COLUMN_WIDTH = 150,
        MINIMUM_COLUMN_WIDTH = 23
    };

private:
    WalletModel *model;
    TransactionFilterProxy *transactionProxyModel;
    QTableView *publicLabelView;

    QComboBox *dateWidget;
    QComboBox *watchOnlyWidget;
    QLineEdit *addressWidget;
    QLineEdit *amountWidget;

    QMenu *contextMenu;
    QSignalMapper *mapperThirdPartyTxUrls;

    QFrame *dateRangeWidget;
    QDateTimeEdit *dateFrom;
    QDateTimeEdit *dateTo;

    QWidget *createDateRangeWidget();

    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;

    virtual void resizeEvent(QResizeEvent* event);

    bool eventFilter(QObject *obj, QEvent *event);

private Q_SLOTS:
    void contextualMenu(const QPoint &);
    void dateRangeChanged();
    void showDetails();
    void fillSendCoinsPage();
    void copyAddress();
    void copyLabel();
    void copyAmount();
    void copyTxID();
    void copyTxHex();
    void openThirdPartyTxUrl(QString url);
    void updateWatchOnlyColumn(bool fHaveWatchOnly);

Q_SIGNALS:
    void doubleClicked(const QModelIndex&);
    void menuActionSendPublicLabel(QString address, QString labelPublic);

    /**  Fired when a message should be reported to the user */
    void message(const QString &title, const QString &message, unsigned int style);

public Q_SLOTS:
    void chooseDate(int idx);
    void chooseWatchonly(int idx);
    void changedPrefix(const QString &prefix);
    void changedAmount(const QString &amount);
    void exportClicked();
    void focusTransaction(const QModelIndex&);

};

#endif // BITCOIN_QT_PUBLICLABELVIEW_H
