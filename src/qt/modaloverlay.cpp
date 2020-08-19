// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "modaloverlay.h"
#include "ui_modaloverlay.h"

#include "guiutil.h"
#include "main.h"

#include <QPropertyAnimation>
#include <QResizeEvent>

ModalOverlay::ModalOverlay(QWidget *parent)
    : QWidget(parent), ui(new Ui::ModalOverlay), bestBlockHeight(0), layerIsVisible(false), userClosed(false)
{
    ui->setupUi(this);
    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(closeClicked()));
    if (parent)
    {
        parent->installEventFilter(this);
        raise();
    }

    blockProcessTime.clear();
    setVisible(false);
}

ModalOverlay::~ModalOverlay() { delete ui; }
bool ModalOverlay::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == parent())
    {
        if (ev->type() == QEvent::Resize)
        {
            QResizeEvent *rev = static_cast<QResizeEvent *>(ev);
            resize(rev->size());
            if (!layerIsVisible)
                setGeometry(0, height(), width(), height());
        }
        else if (ev->type() == QEvent::ChildAdded)
        {
            raise();
        }
    }
    return QWidget::eventFilter(obj, ev);
}

//! Tracks parent widget changes
bool ModalOverlay::event(QEvent *ev)
{
    if (ev->type() == QEvent::ParentAboutToChange)
    {
        if (parent())
            parent()->removeEventFilter(this);
    }
    else if (ev->type() == QEvent::ParentChange)
    {
        if (parent())
        {
            parent()->installEventFilter(this);
            raise();
        }
    }
    return QWidget::event(ev);
}

void ModalOverlay::setKnownBestHeight(int count, const QDateTime &blockDate)
{
    int nHeight = bestBlockHeight.load();
    if (count > nHeight)
    {
        bestBlockHeight.compare_exchange_weak(nHeight, count);
        nHeight = bestBlockHeight.load();
    }
}

void ModalOverlay::tipUpdate(int count, const QDateTime &blockDate, double nVerificationProgress)
{
    QDateTime currentDate = QDateTime::currentDateTime();

    // keep a vector of samples of verification progress at height
    blockProcessTime.push_front(qMakePair(currentDate.currentMSecsSinceEpoch(), nVerificationProgress));

    // show progress speed if we have more then one sample
    if (blockProcessTime.size() >= 2)
    {
        double progressStart = blockProcessTime[0].second;
        double progressDelta = 0;
        double progressPerHour = 0;
        qint64 timeDelta = 0;
        qint64 remainingMSecs = 0;
        double remainingProgress = 1.0 - nVerificationProgress;
        for (int i = 1; i < blockProcessTime.size(); i++)
        {
            QPair<qint64, double> sample = blockProcessTime[i];

            // take first sample after 500 seconds or last available one
            if (sample.first < (currentDate.currentMSecsSinceEpoch() - 500 * 1000) || i == blockProcessTime.size() - 1)
            {
                progressDelta = progressStart - sample.second;
                timeDelta = blockProcessTime[0].first - sample.first;
                progressPerHour = progressDelta / (double)timeDelta * 1000 * 3600;
                remainingMSecs = remainingProgress / progressDelta * timeDelta;
                break;
            }
        }
        // show progress increase per hour
        ui->progressIncreasePerH->setText(QString::number(progressPerHour * 100, 'f', 2) + "%");

        // show expected remaining time
        ui->expectedTimeLeft->setText(GUIUtil::formateNiceTimeOffset(remainingMSecs / 1000.0));

        // keep maximal 5000 samples
        static const int MAX_SAMPLES = 5000;
        if (blockProcessTime.count() > MAX_SAMPLES)
            blockProcessTime.remove(MAX_SAMPLES, blockProcessTime.count() - MAX_SAMPLES);
    }

    // show the last block date
    ui->newestBlockDate->setText(blockDate.toString());

    // show the percentage done according to nVerificationProgress
    ui->percentageProgress->setText(QString::number(nVerificationProgress * 100, 'f', 2) + "%");
    ui->progressBar->setValue(nVerificationProgress * 100);

    // show remaining amount of blocks
    // estimate the number of headers left based on nPowTargetSpacing
    int nEstimateNumHeadersLeft = QDateTime::fromTime_t(pindexBestHeader.load()->nTime).secsTo(currentDate) /
                                  Params().GetConsensus().nPowTargetSpacing;
    bool fHasBestHeader = pindexBestHeader.load()->nHeight >= count;
    if (nEstimateNumHeadersLeft < HEADER_HEIGHT_SYNC_DELTA && fHasBestHeader)
    {
        ui->amountOfBlocksLeft->setText(QString::number(pindexBestHeader.load()->nHeight - bestBlockHeight));
    }
    else
    {
        ui->amountOfBlocksLeft->setText(tr("Unknown. Syncing Headers (%1)...").arg(pindexBestHeader.load()->nHeight));
        ui->expectedTimeLeft->setText(tr("Unknown. Syncing Headers..."));
    }
}

void ModalOverlay::showHide(bool hide, bool userRequested)
{
    if ((layerIsVisible && !hide) || (!layerIsVisible && hide) || (!hide && userClosed && !userRequested))
        return;

    if (!isVisible() && !hide)
        setVisible(true);

    setGeometry(0, hide ? 0 : height(), width(), height());

    QPropertyAnimation *animation = new QPropertyAnimation(this, "pos");
    animation->setDuration(300);
    animation->setStartValue(QPoint(0, hide ? 0 : this->height()));
    animation->setEndValue(QPoint(0, hide ? this->height() : 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    layerIsVisible = !hide;
}

void ModalOverlay::closeClicked()
{
    showHide(true);
    userClosed = true;
}
