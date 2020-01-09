// Copyright (c) 2011-2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "unlimiteddialog.h"
#include "unlimitedmodel.h"

#include "bitcoinunits.h"
#include "guiutil.h"

#include "main.h" // for DEFAULT_SCRIPTCHECK_THREADS and MAX_SCRIPTCHECK_THREADS
#include "net.h" // for access to the network traffic shapers
#include "netbase.h"
#include "txdb.h" // for -dbcache defaults

#ifdef ENABLE_WALLET
#include "wallet/wallet.h" // for CWallet::minTxFee
#endif

#include <boost/lexical_cast.hpp>
#include <limits>
#include <thread>

#include <QDataWidgetMapper>
#include <QDir>
#include <QIntValidator>
#include <QLocale>
#include <QMessageBox>
#include <QTimer>

inline int64_t bwEdit2Slider(int64_t x) { return sqrt(x * 100); }
inline int64_t bwSlider2Edit(int64_t x) { return x * x / 100; }
UnlimitedDialog::UnlimitedDialog(QWidget *parent, UnlimitedModel *mdl)
    : QDialog(parent), model(mdl), burstValidator(0, 100000000, this), sendAveValidator(0, 100000000, this),
      recvAveValidator(0, 100000000, this)
{
    ui.setupUi(this);
    sendAveValidator.initialize(ui.sendBurstEdit, ui.errorText);
    recvAveValidator.initialize(ui.recvBurstEdit, ui.errorText);
    // ui.maxMinedBlock->setRange(0, 0xffffffffUL);
    mapper.setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
    mapper.setOrientation(Qt::Vertical);
    setMapper();

    connect(ui.okButton, SIGNAL(clicked(bool)), this, SLOT(on_okButton_clicked()));

    int64_t max, ave;
    sendShaper.get(&max, &ave);
    auto longMax = std::numeric_limits<int64_t>::max();
    bool enabled = (ave != longMax);
    ui.sendShapingEnable->setChecked(enabled);
    // The slider is just for convenience so setting their ranges to what is commonly chosen
    ui.sendBurstSlider->setRange(0, 1000);
    ui.sendAveSlider->setRange(0, 1000);
    ui.recvBurstSlider->setRange(0, 1000);
    ui.recvAveSlider->setRange(0, 1000);

    ui.sendBurstEdit->setValidator(&burstValidator);
    ui.recvBurstEdit->setValidator(&burstValidator);
    ui.sendAveEdit->setValidator(&sendAveValidator);
    ui.recvAveEdit->setValidator(&recvAveValidator);

    connect(ui.sendShapingEnable, SIGNAL(clicked(bool)), this, SLOT(shapingEnableChanged(bool)));
    connect(ui.recvShapingEnable, SIGNAL(clicked(bool)), this, SLOT(shapingEnableChanged(bool)));

    connect(ui.sendBurstSlider, SIGNAL(sliderMoved(int)), this, SLOT(shapingSliderChanged()));
    connect(ui.sendAveSlider, SIGNAL(sliderMoved(int)), this, SLOT(shapingSliderChanged()));
    connect(ui.recvBurstSlider, SIGNAL(sliderMoved(int)), this, SLOT(shapingSliderChanged()));
    connect(ui.recvAveSlider, SIGNAL(sliderMoved(int)), this, SLOT(shapingSliderChanged()));

    connect(ui.recvAveEdit, SIGNAL(textChanged(const QString &)), this, SLOT(shapingAveEditFinished()));
    connect(ui.sendAveEdit, SIGNAL(textChanged(const QString &)), this, SLOT(shapingAveEditFinished()));
    connect(ui.recvBurstEdit, SIGNAL(textChanged(const QString &)), this, SLOT(shapingMaxEditFinished()));
    connect(ui.sendBurstEdit, SIGNAL(textChanged(const QString &)), this, SLOT(shapingMaxEditFinished()));

    if (enabled)
    {
        ui.sendBurstEdit->setText(QString(boost::lexical_cast<std::string>(max / 1024).c_str()));
        ui.sendAveEdit->setText(QString(boost::lexical_cast<std::string>(ave / 1024).c_str()));
        ui.sendBurstSlider->setValue(bwEdit2Slider(max / 1024));
        ui.sendAveSlider->setValue(bwEdit2Slider(ave / 1024));
    }
    else
    {
        ui.sendBurstEdit->setText("");
        ui.sendAveEdit->setText("");
    }

    receiveShaper.get(&max, &ave);
    enabled = (ave != std::numeric_limits<int64_t>::max());
    ui.recvShapingEnable->setChecked(enabled);
    if (enabled)
    {
        ui.recvBurstEdit->setText(QString(boost::lexical_cast<std::string>(max / 1024).c_str()));
        ui.recvAveEdit->setText(QString(boost::lexical_cast<std::string>(ave / 1024).c_str()));
        ui.recvBurstSlider->setValue(bwEdit2Slider(max / 1024));
        ui.recvAveSlider->setValue(bwEdit2Slider(ave / 1024));
    }
    else
    {
        ui.recvBurstEdit->setText("");
        ui.recvAveEdit->setText("");
    }
    shapingEnableChanged(false);

    // Block Size text field validators
    ui.miningMaxBlock->setValidator(new QIntValidator(0, INT_MAX, this));
}


UnlimitedDialog::~UnlimitedDialog() {}
void UnlimitedDialog::setMapper()
{
    mapper.setModel(model);

    /* Network */
    mapper.addMapping(ui.sendShapingEnable, UnlimitedModel::UseSendShaping);
    mapper.addMapping(ui.sendBurstEdit, UnlimitedModel::SendBurst);
    mapper.addMapping(ui.sendAveEdit, UnlimitedModel::SendAve);
    mapper.addMapping(ui.recvShapingEnable, UnlimitedModel::UseReceiveShaping);
    mapper.addMapping(ui.recvBurstEdit, UnlimitedModel::ReceiveBurst);
    mapper.addMapping(ui.recvAveEdit, UnlimitedModel::ReceiveAve);

    /* blocksize */
    mapper.addMapping(ui.miningMaxBlock, UnlimitedModel::MaxGeneratedBlock);
    connect(ui.miningMaxBlock, SIGNAL(textChanged(const QString &)), this, SLOT(validateBlockSize()));

    mapper.toFirst();
}

void UnlimitedDialog::setOkButtonState(bool fState) { ui.okButton->setEnabled(fState); }
void UnlimitedDialog::on_resetButton_clicked()
{
    if (model)
    {
        // confirmation dialog
        QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm options reset"),
            tr("This is a global reset of all settings!") + "<br>" +
                tr("Client restart required to activate changes.") + "<br><br>" +
                tr("Client will be shut down. Do you want to proceed?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

        if (btnRetVal == QMessageBox::Cancel)
            return;

        /* reset all options and close GUI */
        model->Reset();
        QApplication::quit();
    }
}

void UnlimitedDialog::on_okButton_clicked()
{
    if (ui.sendShapingEnable->isChecked() && !(shapingAveEditFinished() && shapingMaxEditFinished()))
    {
        reject();
        return;
    }

    if (ui.recvShapingEnable->isChecked() && !(shapingAveEditFinished() && shapingMaxEditFinished()))
    {
        reject();
        return;
    }

    if (!mapper.submit())
    {
        assert(0);
    }

    accept();
}

void UnlimitedDialog::on_cancelButton_clicked()
{
    mapper.revert();
    reject();
}

void UnlimitedDialog::validateBlockSize()
{
    ui.statusLabel->setStyleSheet("QLabel { color: red; }");

    uint64_t mmb = ui.miningMaxBlock->text().toInt();
    uint64_t ebs = excessiveBlockSize;

    if (!MiningAndExcessiveBlockValidatorRule(ebs, mmb))
    {
        ui.statusLabel->setText(tr("Mined block size cannot be larger then excessive block size!"));
        ui.miningMaxBlock->setStyleSheet("QLineEdit {  background-color: red; }");
        ui.okButton->setEnabled(false);
    }
    else
    {
        ui.statusLabel->clear();
        ui.miningMaxBlock->setStyleSheet("");
        ui.okButton->setEnabled(true);
    }
}

bool UnlimitedDialog::shapingAveEditFinished(void)
{
    bool ok, ok2 = false;

    if (ui.sendShapingEnable->isChecked())
    {
        // if the use left the  max and avg edit empty warn and block further processing.
        if (ui.sendBurstEdit->text().isEmpty() || ui.sendAveEdit->text().isEmpty())
        {
            setOkButtonState(false);
            ui.errorText->setStyleSheet("QLabel { color: red; }");
            ui.errorText->setText(tr("Upstream traffic shaping parameters can't be blank"));
            return false;
        }

        // If the user adjusted the average to be higher than the max, then auto-bump the max up to = the average
        int maxVal = ui.sendBurstEdit->text().toInt(&ok);
        int aveVal = ui.sendAveEdit->text().toInt(&ok2);


        if (ok && ok2)
        {
            if (maxVal == 0 || aveVal == 0)
            {
                setOkButtonState(false);
                ui.errorText->setStyleSheet("QLabel { color: red; }");
                ui.errorText->setText(tr("Traffic shaping parameters have to be greater than 0."));
                return false;
            }
            else
            {
                setOkButtonState(true);
                ui.errorText->clear();
                ui.sendAveSlider->setValue(bwEdit2Slider(aveVal));
                if (maxVal < aveVal)
                {
                    ui.sendBurstEdit->setText(ui.sendAveEdit->text());
                    ui.sendBurstSlider->setValue(bwEdit2Slider(aveVal));
                }
            }
        }
    }

    if (ui.recvShapingEnable->isChecked())
    {
        // if the use left the  max and avg edit empty warn and block further processing.
        if (ui.recvBurstEdit->text().isEmpty() || ui.recvAveEdit->text().isEmpty())
        {
            setOkButtonState(false);
            ui.errorText->setStyleSheet("QLabel { color: red; }");
            ui.errorText->setText(tr("Upstream traffic shaping parameters can't be blank"));
            return false;
        }

        int maxVal = ui.recvBurstEdit->text().toInt(&ok);
        int aveVal = ui.recvAveEdit->text().toInt(&ok2);
        if (ok && ok2)
        {
            if (maxVal == 0 || aveVal == 0)
            {
                setOkButtonState(false);
                ui.errorText->setStyleSheet("QLabel { color: red; }");
                ui.errorText->setText(tr("Traffic shaping parameters have to be greater than 0."));
                return false;
            }
            else
            {
                setOkButtonState(true);
                ui.errorText->clear();
                ui.recvAveSlider->setValue(bwEdit2Slider(aveVal));
                if (maxVal < aveVal)
                {
                    ui.recvBurstEdit->setText(ui.recvAveEdit->text());
                    ui.recvBurstSlider->setValue(bwEdit2Slider(aveVal));
                }
            }
        }
    }
    return true;
}

bool UnlimitedDialog::shapingMaxEditFinished(void)
{
    bool ok, ok2 = false;

    if (ui.sendShapingEnable->isChecked())
    {
        // if the use left the  max and avg edit empty warn and block further processing.
        if (ui.sendBurstEdit->text().isEmpty() || ui.sendAveEdit->text().isEmpty())
        {
            setOkButtonState(false);
            ui.errorText->setStyleSheet("QLabel { color: red; }");
            ui.errorText->setText(tr("Upstream traffic shaping parameters can't be blank"));
            return false;
        }

        // If the user adjusted the max to be lower than the average, then move the average down
        int maxVal = ui.sendBurstEdit->text().toInt(&ok);
        int aveVal = ui.sendAveEdit->text().toInt(&ok2);
        if (ok && ok2)
        {
            if (maxVal == 0 || aveVal == 0)
            {
                setOkButtonState(false);
                ui.errorText->setStyleSheet("QLabel { color: red; }");
                ui.errorText->setText(tr("Traffic shaping parameters have to be greater than 0."));
                return false;
            }
            else
            {
                setOkButtonState(true);
                ui.errorText->clear();
                ui.sendBurstSlider->setValue(bwEdit2Slider(maxVal)); // Move the slider based on the edit box change
                // If the max was changed to be lower than the average, bump the average down to the maximum, because
                // having
                // an ave > the max makes no sense.
                if (maxVal < aveVal)
                {
                    // I use the string text here just so I don't have to convert back from int to string
                    ui.sendAveEdit->setText(ui.sendBurstEdit->text());
                    ui.sendAveSlider->setValue(bwEdit2Slider(maxVal));
                }
            }
        }
    }


    if (ui.recvShapingEnable->isChecked())
    {
        // if the use left the  max and avg edit empty warn and block further processing.
        if (ui.recvBurstEdit->text().isEmpty() || ui.recvAveEdit->text().isEmpty())
        {
            setOkButtonState(false);
            ui.errorText->setStyleSheet("QLabel { color: red; }");
            ui.errorText->setText(tr("Upstream traffic shaping parameters can't be blank"));
            return false;
        }

        int maxVal = ui.recvBurstEdit->text().toInt(&ok);
        int aveVal = ui.recvAveEdit->text().toInt(&ok2);
        if (ok && ok2)
        {
            if (maxVal == 0 || aveVal == 0)
            {
                setOkButtonState(false);
                ui.errorText->setStyleSheet("QLabel { color: red; }");
                ui.errorText->setText(tr("Traffic shaping parameters have to be greater than 0."));
                return false;
            }
            else
            {
                setOkButtonState(true);
                ui.errorText->clear();
                ui.recvBurstSlider->setValue(bwEdit2Slider(maxVal)); // Move the slider based on the edit box change
                if (maxVal < aveVal)
                {
                    // I use the string text here just so I don't have to convert back from int to string
                    ui.recvAveEdit->setText(ui.recvBurstEdit->text());
                    ui.recvAveSlider->setValue(bwEdit2Slider(maxVal));
                }
            }
        }
    }
    return true;
}

void UnlimitedDialog::shapingEnableChanged(bool val)
{
    bool enabled = ui.sendShapingEnable->isChecked();

    ui.sendBurstSlider->setEnabled(enabled);
    ui.sendAveSlider->setEnabled(enabled);
    ui.sendBurstEdit->setEnabled(enabled);
    ui.sendAveEdit->setEnabled(enabled);
    if (enabled)
    {
        ui.sendBurstEdit->setFocus();
    }
    else
    {
        ui.sendBurstEdit->setText("");
        ui.sendAveEdit->setText("");
        ui.okButton->setEnabled(true);
    }

    enabled = ui.recvShapingEnable->isChecked();
    ui.recvBurstSlider->setEnabled(enabled);
    ui.recvAveSlider->setEnabled(enabled);
    ui.recvBurstEdit->setEnabled(enabled);
    ui.recvAveEdit->setEnabled(enabled);
    if (enabled)
    {
        ui.recvBurstEdit->setFocus();
    }
    else
    {
        ui.recvBurstEdit->setText("");
        ui.recvAveEdit->setText("");
        ui.okButton->setEnabled(true);
    }
}

void UnlimitedDialog::shapingSliderChanged(void)
{
    // When the sliders change, I want to update the edit box.  Rather then have the pain of making a separate function
    // for every slider, I just set them all whenever one changes.
    int64_t sval;
    int64_t val;
    int64_t cur;

    if (ui.sendShapingEnable->isChecked())
    {
        sval = ui.sendBurstSlider->value();
        val = bwSlider2Edit(sval); // Transform the slider linear position into a bandwidth in Kb
        cur = ui.sendBurstEdit->text().toLongLong();

        // The slider is imprecise compared to the edit box.  So we only want to change the edit box if the slider's
        // change is larger than its imprecision.
        if (bwEdit2Slider(cur) != sval)
        {
            ui.sendBurstEdit->setText(QString::number(val));
            int64_t other = ui.sendAveEdit->text().toLongLong();
            if (other > val) // Set average to burst if its greater
            {
                ui.sendAveEdit->setText(QString::number(val));
                ui.sendAveSlider->setValue(bwEdit2Slider(val));
            }
        }

        sval = ui.sendAveSlider->value();
        val = bwSlider2Edit(sval); // Transform the slider linear position into a bandwidth
        cur = ui.sendAveEdit->text().toLongLong();
        if (bwEdit2Slider(cur) != sval)
        {
            ui.sendAveEdit->setText(QString(boost::lexical_cast<std::string>(val).c_str()));
            int64_t burst = ui.sendBurstEdit->text().toLongLong();
            if (burst < val) // Set burst to average if it is less
            {
                ui.sendBurstEdit->setText(QString::number(val));
                ui.sendBurstSlider->setValue(bwEdit2Slider(val));
            }
        }
    }

    if (ui.recvShapingEnable->isChecked())
    {
        sval = ui.recvBurstSlider->value();
        val = bwSlider2Edit(sval); // Transform the slider linear position into a bandwidth
        cur = ui.recvBurstEdit->text().toLongLong();
        if (bwEdit2Slider(cur) != sval)
        {
            ui.recvBurstEdit->setText(QString(boost::lexical_cast<std::string>(val).c_str()));
            int64_t other = ui.recvAveEdit->text().toLongLong();
            if (other > val) // Set average to burst if its greater
            {
                ui.recvAveEdit->setText(QString::number(val));
                ui.recvAveSlider->setValue(bwEdit2Slider(val));
            }
        }

        sval = ui.recvAveSlider->value();
        val = bwSlider2Edit(sval); // Transform the slider linear position into a bandwidth
        cur = ui.recvAveEdit->text().toLongLong();
        if (bwEdit2Slider(cur) != sval)
        {
            ui.recvAveEdit->setText(QString(boost::lexical_cast<std::string>(val).c_str()));
            int64_t burst = ui.recvBurstEdit->text().toLongLong();
            if (burst < val) // Set burst to average if it is less
            {
                ui.recvBurstEdit->setText(QString::number(val));
                ui.recvBurstSlider->setValue(bwEdit2Slider(val));
            }
        }
    }
}


QValidator::State LessThanValidator::validate(QString &input, int &pos) const
{
    QValidator::State ret = QIntValidator::validate(input, pos);
    bool clearError = true;
    if (ret == QValidator::Acceptable)
    {
        if (other)
        {
            bool ok, ok2 = false;
            // if the use left the  max and avg edit empty warn and block further processing.
            if (input.isEmpty() || other->text().isEmpty())
            {
                clearError = false;
                if (errorDisplay)
                {
                    errorDisplay->setStyleSheet("QLabel { color: red; }");
                    errorDisplay->setText(tr("Upstream traffic shaping parameters can't be blank"));
                }
            }
            int otherVal = other->text().toInt(&ok); // try to convert to an int
            int myVal = input.toInt(&ok2);
            if (ok && ok2)
            {
                if (myVal == 0 || otherVal == 0)
                {
                    clearError = false;
                    if (errorDisplay)
                    {
                        errorDisplay->setStyleSheet("QLabel { color: red; }");
                        errorDisplay->setText("Traffic shaping parameters have to be greater than zero");
                    }
                }
                if (myVal > otherVal)
                {
                    clearError = false;
                    if (errorDisplay)
                    {
                        errorDisplay->setStyleSheet("QLabel { color: red; }");
                        errorDisplay->setText("Average must be less than or equal Maximum");
                    }
                }
            }
        }
    }
    if (clearError && errorDisplay)
        errorDisplay->setText("");
    return ret;
}
