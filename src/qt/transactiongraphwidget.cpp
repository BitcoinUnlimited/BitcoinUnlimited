// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactiongraphwidget.h"
#include "clientmodel.h"
#include "txmempool.h" // for TX_RATE_RESOLUTION_MILLIS

#include <QColor>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

/* Minutes to milliseconds conversion factor */
static const long MINUTES_TO_MILLIS = 60 * 1000;
/* Maximum sample window of 1 day, in milliseconds */
static const long MAXIMUM_SAMPLE_WINDOW_MILLIS = 24 * 60 * MINUTES_TO_MILLIS;
/* Sample rate, derived from the signal frequency used to update the TPS label on the debug ui */
static const long SAMPLE_RATE_MILLIS = TX_RATE_RESOLUTION_MILLIS;
/* Keep no more than this many samples in memory (older samples will be purged) */
static const long MAXIMUM_SAMPLES_TO_KEEP = MAXIMUM_SAMPLE_WINDOW_MILLIS / SAMPLE_RATE_MILLIS;

/* Always display at least 1 tps range on the y-axis */
static const float MINIMUM_DISPLAY_YVALUE = 1.0f;

/* Maximum redraw frequency */
static const long MAXIMUM_REDRAW_RATE_MILLIS = 500;
/* Minimum redraw frequency */
static const long MINIMUM_REDRAW_RATE_MILLIS = 5000;

static const int XMARGIN = 10;
static const int YMARGIN = 10;

TransactionGraphWidget::TransactionGraphWidget(QWidget *parent)
    : QWidget(parent), nMinutes(1440), nRedrawRateMillis(SAMPLE_RATE_MILLIS), fDisplayMax(MINIMUM_DISPLAY_YVALUE),
      fInstantaneousTpsPeak_Runtime(0.0f), fInstantaneousTpsPeak_Sampled(0.0f), fInstantaneousTpsPeak_Displayed(0.0f),
      fInstantaneousTpsAverage_Runtime(0.0f), fInstantaneousTpsAverage_Sampled(0.0f),
      fInstantaneousTpsAverage_Displayed(0.0f), fSmoothedTpsPeak_Runtime(0.0f), fSmoothedTpsPeak_Sampled(0.0f),
      fSmoothedTpsPeak_Displayed(0.0f), fSmoothedTpsAverage_Runtime(0.0f), fSmoothedTpsAverage_Sampled(0.0f),
      fSmoothedTpsAverage_Displayed(0.0f), nTotalSamplesRuntime(0), vInstantaneousSamples(), vSmoothedSamples(),
      clientModel(0)
{
}

size_t TransactionGraphWidget::getSamplesInDisplayWindow() const
{
    return (nMinutes * MINUTES_TO_MILLIS) / SAMPLE_RATE_MILLIS;
}

void TransactionGraphWidget::setClientModel(ClientModel *model)
{
    clientModel = model;
    if (model)
    {
        connect(model, SIGNAL(transactionsPerSecondChanged(double, double, double)), this,
            SLOT(setTransactionsPerSecond(double, double, double)));
    }
}

void TransactionGraphWidget::paintPath(QPainterPath &avgPath, QPainterPath &peakPath)
{
    // This shouldn't be possible as the only place that sets fDisplayMax ensures it is at least MINIMUM_DISPLAY_YVALUE
    // This is just protection agasinst future changes potentially allowing an invalid value
    assert(fDisplayMax > 0.0f);

    int h = height() - YMARGIN * 2, w = width() - XMARGIN * 2;
    int x = XMARGIN + w, yAvg, yPeak;
    int samplesInWindow = getSamplesInDisplayWindow();
    int sampleCount = samplesInWindow;
    if (sampleCount > vInstantaneousSamples.size())
        sampleCount = vInstantaneousSamples.size();

    if (sampleCount > 0 && samplesInWindow > 0)
    {
        // Computes the maximum and average of all sample points which fall under the same pixel
        avgPath.moveTo(x, YMARGIN + h);
        peakPath.moveTo(x, YMARGIN + h);
        int lastX = -1, countX = 0;
        float peakY = 0.0f, avgY = 0.0f;
        for (int i = 0; i < sampleCount; i++)
        {
            float valPeak = vInstantaneousSamples.at(i);
            float valAvg = vSmoothedSamples.at(i);
            x = XMARGIN + w - w * i / samplesInWindow;
            if (x == lastX)
            {
                // Update running average for this pixel
                countX++;
                avgY = avgY + ((valAvg - avgY) / countX);

                // Update maximum value for this pixel
                if (valPeak > peakY)
                    peakY = valPeak;

                continue;
            }

            // first time through don't draw the line as it is priming the variables
            if (lastX != -1)
            {
                // draw the path for the last set of samples that map to a single pixel
                yAvg = YMARGIN + h - (int)(h * avgY / fDisplayMax);
                avgPath.lineTo(lastX, yAvg);
                yPeak = YMARGIN + h - (int)(h * peakY / fDisplayMax);
                peakPath.lineTo(lastX, yPeak);
            }

            // reset values for the new x pixel
            lastX = x;
            peakY = valPeak;
            avgY = valAvg;
            countX = 1;
        }

        // The last sample(s) won't be drawn inside the loop due to the way we draw the previous
        // x-pixel value when the next x-pixel is detected.  This is necessary to support aggregation
        // of multiple samples that map to the same x-pixel.  So we need to draw the last sample(s) here
        yAvg = YMARGIN + h - (int)(h * avgY / fDisplayMax);
        avgPath.lineTo(x, yAvg);
        yPeak = YMARGIN + h - (int)(h * peakY / fDisplayMax);
        peakPath.lineTo(x, yPeak);

        // close the figure to the bottom of the graph
        avgPath.lineTo(x, YMARGIN + h);
        peakPath.lineTo(x, YMARGIN + h);
    }
}

void TransactionGraphWidget::paintEvent(QPaintEvent *)
{
    // This shouldn't be possible as the only place that sets fDisplayMax ensures it is at least MINIMUM_DISPLAY_YVALUE
    // This is just protection agasinst future changes potentially allowing an invalid value
    assert(fDisplayMax > 0.0f);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    QColor axisCol(Qt::gray);
    int h = height() - YMARGIN * 2;
    painter.setPen(axisCol);
    painter.drawLine(XMARGIN, YMARGIN + h, width() - XMARGIN, YMARGIN + h);

    // decide what order of magnitude we are
    int base = floor(log10(fDisplayMax));
    float val = pow(10.0f, base);

    const QString units = tr("tps");
    const float yMarginText = 2.0;

    // draw major-axis lines
    painter.setPen(axisCol);
    for (float y = val; y < fDisplayMax; y += val)
    {
        // draw label text for all major-axis lines
        painter.drawText(XMARGIN, YMARGIN + h - h * y / fDisplayMax - yMarginText,
            QString("%1 %2").arg(QString().setNum(y, 'f', 2)).arg(units));

        int yy = YMARGIN + h - h * y / fDisplayMax;
        painter.drawLine(XMARGIN, yy, width() - XMARGIN, yy);
    }

    // if we drew 3 or fewer lines, break them up at the next lower order of magnitude
    if (fDisplayMax / val <= 3.0f)
    {
        axisCol = axisCol.darker();
        val = pow(10.0f, base - 1);
        painter.setPen(axisCol);
        int count = 1;
        for (float y = val; y < fDisplayMax; y += val, count++)
        {
            // don't overwrite lines drawn above
            if (count % 10 == 0)
                continue;

            // draw label text for the middle minor-axis line
            if (count % 5 == 0)
                painter.drawText(XMARGIN, YMARGIN + h - h * y / fDisplayMax - yMarginText,
                    QString("%1 %2").arg(QString().setNum(y, 'f', 2)).arg(units));

            int yy = YMARGIN + h - h * y / fDisplayMax;
            painter.drawLine(XMARGIN, yy, width() - XMARGIN, yy);
        }
    }

    if (!vInstantaneousSamples.empty())
    {
        // first draw max in the sample aggregation
        QPainterPath pAvg, pMax;
        paintPath(pAvg, pMax);

        // draw the peak
        painter.fillPath(pMax, QColor(255, 255, 0, 128));
        painter.setPen(Qt::yellow);
        painter.drawPath(pMax);

        // draw the average
        painter.fillPath(pAvg, QColor(0, 255, 0, 128));
        painter.setPen(Qt::green);
        painter.drawPath(pAvg);
    }
}

/*
  Computes the arithmetic mean based on the current mean at n-1, adding a new sample
  NOTE: newSampleCount includes the new sample to be added
*/
double AddToArithmeticMean(double currentMean, size_t newSampleCount, double newSample)
{
    // newSampleCount MUST include the current sample being added, so 0 is not valid
    assert(newSampleCount > 0);

    return currentMean + ((newSample - currentMean) / newSampleCount);
}

/*
  Computes the arithmetic mean based on the current mean at n, subtracting one sample
  This implementation is only valid for sample sets where sample values are guaranteed to be >= 0.0
  NOTE: currentSampleCount includes the sample to be removed
*/
double SubtractFromArithmeticMean(double currentMean, size_t currentSampleCount, double removingSample)
{
    // NOTE: This check is only valid for sample sets where all values are guaranteed to be >= 0.0
    if (currentSampleCount <= 1)
        return 0.0;

    return ((currentMean * currentSampleCount) - removingSample) / (currentSampleCount - 1);
}

void TransactionGraphWidget::setTransactionsPerSecond(double nTxPerSec,
    double nInstantaneousTxPerSec,
    double nPeakTxPerSec)
{
    nTotalSamplesRuntime++;

    // Add new instantaneous sample
    vInstantaneousSamples.push_front((float)nInstantaneousTxPerSec);

    // Update instantaneous peak for the total runtime
    // NOTE: This peak may no longer be in the sample set if it was purged for being too old
    if (nInstantaneousTxPerSec > fInstantaneousTpsPeak_Runtime)
        fInstantaneousTpsPeak_Runtime = nInstantaneousTxPerSec;

    // Adjust instantaneous mean for total runtime to include new sample
    fInstantaneousTpsAverage_Runtime =
        AddToArithmeticMean(fInstantaneousTpsAverage_Runtime, nTotalSamplesRuntime, nInstantaneousTxPerSec);

    // Adjust instantaneous mean for total sample set to include new sample
    fInstantaneousTpsAverage_Sampled =
        AddToArithmeticMean(fInstantaneousTpsAverage_Sampled, vInstantaneousSamples.count(), nInstantaneousTxPerSec);

    // NOTE: Instantaneous mean for displayed samples is handled in updateTransactionsPerSecondLabelValues()

    // Purge instantaneous sample(s) that have moved beyond the sample set size limit
    while (vInstantaneousSamples.size() > MAXIMUM_SAMPLES_TO_KEEP)
    {
        float fRemoving = vInstantaneousSamples.last();
        // Adjust instantaneous mean for total sample set to exclude the sample being purged
        fInstantaneousTpsAverage_Sampled =
            SubtractFromArithmeticMean(fInstantaneousTpsAverage_Sampled, vInstantaneousSamples.size(), fRemoving);

        vInstantaneousSamples.pop_back();
    }


    // Add new smoothed sample
    vSmoothedSamples.push_front((float)nTxPerSec);

    // Update smoothed peak for the total runtime
    // NOTE: This peak may no longer be in the sample set if it was purged for being too old
    if (nTxPerSec > fSmoothedTpsPeak_Runtime)
        fSmoothedTpsPeak_Runtime = nTxPerSec;

    // Adjust smoothed mean for total runtime to include new sample
    fSmoothedTpsAverage_Runtime = AddToArithmeticMean(fSmoothedTpsAverage_Runtime, nTotalSamplesRuntime, nTxPerSec);

    // Adjust smoothed mean for total sample set to include new sample
    fSmoothedTpsAverage_Sampled = AddToArithmeticMean(fSmoothedTpsAverage_Sampled, vSmoothedSamples.count(), nTxPerSec);

    // NOTE: Smoothed mean for displayed samples is handled in updateTransactionsPerSecondLabelValues()

    // Purge smoothed sample(s) that have moved beyond the sample set size limit
    while (vSmoothedSamples.size() > MAXIMUM_SAMPLES_TO_KEEP)
    {
        float fRemoving = vSmoothedSamples.last();
        // Adjust smoothed mean for total sample set to exclude the sample being purged
        fSmoothedTpsAverage_Sampled =
            SubtractFromArithmeticMean(fSmoothedTpsAverage_Sampled, vSmoothedSamples.size(), fRemoving);

        vSmoothedSamples.pop_back();
    }

    // Update the TPS values matching the current display window
    updateTransactionsPerSecondLabelValues();

    // Limit redraw requests
    static uint64_t nLastRedrawTime = (uint64_t)GetTimeMillis();
    uint64_t nCurrentTime = (uint64_t)GetTimeMillis();
    if (nCurrentTime >= nLastRedrawTime + nRedrawRateMillis)
    {
        update();
        nLastRedrawTime = nCurrentTime;
    }
}

void TransactionGraphWidget::updateTransactionsPerSecondLabelValues()
{
    size_t numSamples = getSamplesInDisplayWindow();

    // Recompute the instantaneous peak for the current sample set and the average for the display window
    // Runtime peak is computed in the add sample method because that tracks the peak even beyond the maintained sample
    // set
    float tSumAll = 0.0f, tSumDisplay = 0.0f;
    size_t countAll = vSmoothedSamples.size();
    size_t countDisplay = countAll < numSamples ? countAll : numSamples;
    float tMaxSamples = 0.0f, tMaxDisplay = 0.0f;
    for (size_t i = 0; i < (size_t)vInstantaneousSamples.size(); i++)
    {
        float f = vInstantaneousSamples.at(i);
        tSumAll += f;
        if (i <= numSamples)
            tSumDisplay += f;

        if (f > tMaxSamples)
        {
            tMaxSamples = f;
            if (i <= numSamples)
                tMaxDisplay = f;
        }
    }
    fInstantaneousTpsPeak_Sampled = tMaxSamples;
    fInstantaneousTpsPeak_Displayed = tMaxDisplay;
    if (countDisplay > 0)
        fInstantaneousTpsAverage_Displayed = tSumDisplay / countDisplay;
    else
        fInstantaneousTpsAverage_Displayed = 0.0f;

    // Adjust the y-axis scaling factor based on the highest peak currently visible
    float tmax = fInstantaneousTpsPeak_Displayed;
    if (tmax < MINIMUM_DISPLAY_YVALUE)
        tmax = MINIMUM_DISPLAY_YVALUE;
    fDisplayMax = tmax;


    // Recompute the smoothed peak for the current sample set and the average for the display window
    // Runtime peak is computed in the add sample method because that tracks the peak even beyond the maintained sample
    // set
    tSumAll = 0.0f;
    tSumDisplay = 0.0f;
    tMaxSamples = 0.0f;
    tMaxDisplay = 0.0f;
    for (size_t i = 0; i < (size_t)vSmoothedSamples.size(); i++)
    {
        float f = vSmoothedSamples.at(i);
        tSumAll += f;
        if (i <= numSamples)
            tSumDisplay += f;

        if (f > tMaxSamples)
        {
            tMaxSamples = f;
            if (i <= numSamples)
                tMaxDisplay = f;
        }
    }
    fSmoothedTpsPeak_Sampled = tMaxSamples;
    fSmoothedTpsPeak_Displayed = tMaxDisplay;
    if (countDisplay > 0)
        fSmoothedTpsAverage_Displayed = tSumDisplay / countDisplay;
    else
        fSmoothedTpsAverage_Displayed = 0.0f;
}

void TransactionGraphWidget::setTpsGraphRangeMins(int mins)
{
    // input value must be >= 1
    assert(mins > 0);

    nMinutes = mins;

    if (mins < 60)
        displayWindowLabelText = QString::number(mins) + "-Minutes";
    else if (mins == 60)
        displayWindowLabelText = "1-Hour";
    else
        displayWindowLabelText = QString::number(mins / 60.0f, 'g', 4) + "-Hours";

    // Update the redraw frequency at a rate of 1 second per 30 minutes worth of sample data displayed
    int rateMillis = nMinutes * 1000 / 30;

    // Ensure we don't exceed the redraw rate limits
    if (rateMillis < MAXIMUM_REDRAW_RATE_MILLIS)
        rateMillis = MAXIMUM_REDRAW_RATE_MILLIS;
    else if (rateMillis > MINIMUM_REDRAW_RATE_MILLIS)
        rateMillis = MINIMUM_REDRAW_RATE_MILLIS;

    nRedrawRateMillis = rateMillis;

    // Lastly update the transaction rate statistics values as changing the display window also
    // changes some of these values
    updateTransactionsPerSecondLabelValues();
}
