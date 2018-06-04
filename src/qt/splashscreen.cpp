// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "splashscreen.h"

#include "networkstyle.h"

#include "clientversion.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"
#include "version.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>
#include <QRadialGradient>

SplashScreen::SplashScreen(Qt::WindowFlags f, const NetworkStyle *networkStyle) : QWidget(0, f), curAlignment(0)
{
    int TEXTX = 260;
    int VERY = 150;
    // int COPYRIGHTY = 180;
    int NETY = 250;

    // set reference point, paddings
    float fontFactor = 1.0;
    float devicePixelRatio = 1.0;
#if QT_VERSION > 0x050100
    devicePixelRatio = ((QGuiApplication *)QCoreApplication::instance())->devicePixelRatio();
#endif

    // define text to place
    QString titleText = tr("Bitcoin Unlimited Bitcoin Cash");
    // create a bitmap according to device pixelratio
    QPixmap splash(":/images/splash");
    QSize splashPixSize = splash.size();
    QSize splashSize(splashPixSize.width() * devicePixelRatio, splashPixSize.height() * devicePixelRatio);
    QString versionText = QString("Version %1").arg(QString::fromStdString(FormatFullVersion()));
    QString titleAddText = networkStyle->getTitleAddText();

    QString font = QApplication::font().toString();
    pixmap = QPixmap(splashSize);

#if QT_VERSION > 0x050100
    // change to HiDPI if it makes sense
    pixmap.setDevicePixelRatio(devicePixelRatio);
#endif

    QPainter pixPaint(&pixmap);
    pixPaint.setPen(QColor(100, 100, 100));

    // draw a slightly radial gradient
    QRadialGradient gradient(QPoint(0, 0), splashSize.width() / devicePixelRatio);
    gradient.setColorAt(0, Qt::white);
    gradient.setColorAt(1, QColor(220, 220, 220));
    QRect rGradient(QPoint(0, 0), splashSize);
    pixPaint.fillRect(rGradient, gradient);

    pixPaint.drawPixmap(QRect(QPoint(0, 0), splashSize), splash);

    pixPaint.setFont(QFont(font, 16 * fontFactor));
    // QFontMetrics fm = pixPaint.fontMetrics();
    // int versionTextWidth = fm.width(versionText);
    pixPaint.drawText(TEXTX * devicePixelRatio, VERY * devicePixelRatio, versionText);

    // draw additional text if special network
    if (!titleAddText.isEmpty())
    {
        pixPaint.setFont(QFont(font, 40 * fontFactor));
        pixPaint.setPen(QColor(200, 0, 0));
        // fm = pixPaint.fontMetrics();
        // versionTextWidth = fm.width(versionText);
        pixPaint.drawText(TEXTX * devicePixelRatio, NETY * devicePixelRatio, titleAddText);
    }

#if 0 // I don't think we need copyright on the splash screen but leaving this here for later
    QString copyrightText =
        QString::fromUtf8(CopyrightHolders(strprintf("\xc2\xA9 %u-%u ", 2009, COPYRIGHT_YEAR)).c_str());

    // draw copyright stuff
    {
        pixPaint.setFont(QFont(font, 10 * fontFactor));
        pixPaint.setPen(QColor(100, 100, 100));
        const int x = TEXTX;
        const int y = COPYRIGHTY;
        QRect copyrightRect(x, y, pixmap.width() - x - paddingRight, pixmap.height() - y);
        pixPaint.drawText(copyrightRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, copyrightText);
    }
#endif

    pixPaint.end();

    // Set window title
    setWindowTitle(titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width() / devicePixelRatio, pixmap.size().height() / devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QApplication::desktop()->screenGeometry().center() - r.center());

    subscribeToCoreSignals();
}

SplashScreen::~SplashScreen() { unsubscribeFromCoreSignals(); }
void SplashScreen::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);
    hide();
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage", Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)), Q_ARG(int, Qt::AlignBottom | Qt::AlignHCenter),
        Q_ARG(QColor, QColor(55, 55, 55)));
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress)
{
    InitMessage(splash, title + strprintf("%d", nProgress) + "%");
}

#ifdef ENABLE_WALLET
static void ConnectWallet(SplashScreen *splash, CWallet *wallet)
{
    wallet->ShowProgress.connect(boost::bind(ShowProgress, splash, _1, _2));
}
#endif

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, _1));
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.connect(boost::bind(ConnectWallet, this, _1));
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, _1));
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
#endif
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -5);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    StartShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
