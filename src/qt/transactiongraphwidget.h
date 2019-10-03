// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TRANSACTIONGRAPHWIDGET_H
#define BITCOIN_QT_TRANSACTIONGRAPHWIDGET_H

#include <QQueue>
#include <QWidget>

class ClientModel;

QT_BEGIN_NAMESPACE
class QPaintEvent;
QT_END_NAMESPACE

class TransactionGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TransactionGraphWidget(QWidget *parent = 0);
    void setClientModel(ClientModel *model);

protected:
    void paintEvent(QPaintEvent *);

public Q_SLOTS:
    void setTransactionsPerSecond(double nTxPerSec, double nInstantaneousTxPerSec, double nPeakTxPerSec);
    void setTpsGraphRangeMins(int mins);

public:
    float getInstantaneousTpsPeak_Runtime() const { return fInstantaneousTpsPeak_Runtime; }
    float getInstantaneousTpsPeak_Sampled() const { return fInstantaneousTpsPeak_Sampled; }
    float getInstantaneousTpsPeak_Displayed() const { return fInstantaneousTpsPeak_Displayed; }
    float getInstantaneousTpsAverage_Runtime() const { return fInstantaneousTpsAverage_Runtime; }
    float getInstantaneousTpsAverage_Sampled() const { return fInstantaneousTpsAverage_Sampled; }
    float getInstantaneousTpsAverage_Displayed() const { return fInstantaneousTpsAverage_Displayed; }
    float getSmoothedTpsPeak_Runtime() const { return fSmoothedTpsPeak_Runtime; }
    float getSmoothedTpsPeak_Sampled() const { return fSmoothedTpsPeak_Sampled; }
    float getSmoothedTpsPeak_Displayed() const { return fSmoothedTpsPeak_Displayed; }
    float getSmoothedTpsAverage_Runtime() const { return fSmoothedTpsAverage_Runtime; }
    float getSmoothedTpsAverage_Sampled() const { return fSmoothedTpsAverage_Sampled; }
    float getSmoothedTpsAverage_Displayed() const { return fSmoothedTpsAverage_Displayed; }
    QString getDisplayWindowLabelText() const { return displayWindowLabelText; }
private:
    void paintPath(QPainterPath &avgPath, QPainterPath &peakPath);
    size_t getSamplesInDisplayWindow() const;
    void updateTransactionsPerSecondLabelValues();

    size_t nMinutes;
    size_t nRedrawRateMillis;
    float fDisplayMax;

    float fInstantaneousTpsPeak_Runtime;
    float fInstantaneousTpsPeak_Sampled;
    float fInstantaneousTpsPeak_Displayed;
    float fInstantaneousTpsAverage_Runtime;
    float fInstantaneousTpsAverage_Sampled;
    float fInstantaneousTpsAverage_Displayed;

    float fSmoothedTpsPeak_Runtime;
    float fSmoothedTpsPeak_Sampled;
    float fSmoothedTpsPeak_Displayed;
    float fSmoothedTpsAverage_Runtime;
    float fSmoothedTpsAverage_Sampled;
    float fSmoothedTpsAverage_Displayed;

    QString displayWindowLabelText;

    size_t nTotalSamplesRuntime;
    QQueue<float> vInstantaneousSamples;
    QQueue<float> vSmoothedSamples;

    ClientModel *clientModel;
};

#endif // BITCOIN_QT_TRANSACTIONGRAPHWIDGET_H
