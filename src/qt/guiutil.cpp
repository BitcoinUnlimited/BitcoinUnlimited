// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "guiutil.h"

#include "bitcoinaddressvalidator.h"
#include "bitcoinunits.h"
#include "qvalidatedlineedit.h"
#include "walletmodel.h"

#include "cashaddr.h"
#include "config.h"
#include "dstencode.h"
#include "fs.h"
#include "init.h"
#include "main.h" // For minRelayTxFee
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "protocol.h"
#include "script/script.h"
#include "script/standard.h"
#include "util.h"
#include "utilstrencodings.h"

#ifdef WIN32
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501
#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501
#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "shellapi.h"
#include "shlobj.h"
#include "shlwapi.h"
#endif

#include <boost/scoped_array.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFont>
#include <QLineEdit>
#include <QSettings>
#include <QTextDocument> // for Qt::mightBeRichText
#include <QThread>
#include <QUrlQuery>

#if QT_VERSION >= 0x050200
#include <QFontDatabase>
#endif

static fs::detail::utf8_codecvt_facet utf8;

#if defined(Q_OS_MAC)
extern double NSAppKitVersionNumber;
#if !defined(NSAppKitVersionNumber10_8)
#define NSAppKitVersionNumber10_8 1187
#endif
#if !defined(NSAppKitVersionNumber10_9)
#define NSAppKitVersionNumber10_9 1265
#endif
#endif

namespace GUIUtil
{
QString dateTimeStr(const QDateTime &date)
{
    return date.date().toString(Qt::SystemLocaleShortDate) + QString(" ") + date.toString("hh:mm");
}

QString dateTimeStr(qint64 nTime) { return dateTimeStr(QDateTime::fromTime_t((qint32)nTime)); }
QFont fixedPitchFont()
{
#if QT_VERSION >= 0x050200
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
#else
    QFont font("Monospace");
    font.setStyleHint(QFont::Monospace);
    return font;
#endif
}

static std::string MakeAddrInvalid(std::string addr)
{
    if (addr.size() < 2)
    {
        return "";
    }

    // Checksum is at the end of the address. Swapping chars to make it invalid.
    std::swap(addr[addr.size() - 1], addr[addr.size() - 2]);
    if (!IsValidDestinationString(addr))
    {
        return addr;
    }
    return "";
}

std::string DummyAddress(const CChainParams &params, const Config &cfg)
{
    // Just some dummy data to generate an convincing random-looking (but
    // consistent) address
    static const std::vector<uint8_t> dummydata = {0xeb, 0x15, 0x23, 0x1d, 0xfc, 0xeb, 0x60, 0x92, 0x58, 0x86, 0xb6,
        0x7d, 0x06, 0x52, 0x99, 0x92, 0x59, 0x15, 0xae, 0xb1};

    const CTxDestination dstKey = CKeyID(uint160(dummydata));
    return MakeAddrInvalid(EncodeDestination(dstKey, params, cfg));
}

void setupAddressWidget(QValidatedLineEdit *widget, QWidget *parent)
{
    parent->setFocusProxy(widget);

    widget->setFont(fixedPitchFont());
    const CChainParams &params = Params();
    // We don't want translators to use own addresses in translations
    // and this is the only place, where this address is supplied.
    widget->setPlaceholderText(QObject::tr("Enter a Bitcoin address (e.g. %1)")
                                   .arg(QString::fromStdString(DummyAddress(params, GetConfig()))));
    widget->setValidator(new BitcoinAddressEntryValidator(params.CashAddrPrefix(), parent));
    widget->setCheckValidator(new BitcoinAddressCheckValidator(parent));
}

void setupAmountWidget(QLineEdit *widget, QWidget *parent)
{
    QDoubleValidator *amountValidator = new QDoubleValidator(parent);
    amountValidator->setDecimals(8);
    amountValidator->setBottom(0.0);
    widget->setValidator(amountValidator);
    widget->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

QString bitcoinURIScheme(const CChainParams &params, bool useCashAddr)
{
    if (!useCashAddr)
    {
        return "bitcoincash";
    }
    return QString::fromStdString(params.CashAddrPrefix());
}

QString bitcoinURIScheme(const Config &cfg)
{
    return bitcoinURIScheme(cfg.GetChainParams(), cfg.UseCashAddrEncoding());
}

static bool IsCashAddrEncoded(const QUrl &uri)
{
    const std::string addr = (uri.scheme() + ":" + uri.path()).toStdString();
    auto decoded = cashaddr::Decode(addr, "");
    return !decoded.first.empty();
}

bool parseBitcoinURI(const QString &scheme, const QUrl &uri, SendCoinsRecipient *out)
{
    // return if URI has wrong scheme.
    if (!uri.isValid() || uri.scheme() != scheme)
    {
        return false;
    }

    SendCoinsRecipient rv;
    if (IsCashAddrEncoded(uri))
    {
        rv.address = uri.scheme() + ":" + uri.path();
    }
    else
    {
        // strip out uri scheme for base58 encoded addresses
        rv.address = uri.path();
    }
    // Trim any following forward slash which may have been added by the OS
    if (rv.address.endsWith("/"))
    {
        rv.address.truncate(rv.address.length() - 1);
    }
    rv.amount = 0;

    QUrlQuery uriQuery(uri);
    QList<QPair<QString, QString> > items = uriQuery.queryItems();
    for (QList<QPair<QString, QString> >::iterator i = items.begin(); i != items.end(); i++)
    {
        bool fShouldReturnFalse = false;
        if (i->first.startsWith("req-"))
        {
            i->first.remove(0, 4);
            fShouldReturnFalse = true;
        }

        if (i->first == "label")
        {
            rv.label = i->second;
            fShouldReturnFalse = false;
        }
        if (i->first == "message")
        {
            rv.message = i->second;
            fShouldReturnFalse = false;
        }
        else if (i->first == "amount")
        {
            if (!i->second.isEmpty())
            {
                if (!BitcoinUnits::parse(BitcoinUnits::BCH, i->second, &rv.amount))
                {
                    return false;
                }
            }
            fShouldReturnFalse = false;
        }

        if (fShouldReturnFalse)
            return false;
    }
    if (out)
    {
        *out = rv;
    }
    return true;
}

bool parseBitcoinURI(const QString &scheme, QString uri, SendCoinsRecipient *out)
{
    //
    //    Cannot handle this later, because bitcoincash://
    //    will cause Qt to see the part after // as host,
    //    which will lower-case it (and thus invalidate the address).
    if (uri.startsWith(scheme + "://", Qt::CaseInsensitive))
    {
        uri.replace(0, scheme.length() + 3, scheme + ":");
    }
    QUrl uriInstance(uri);
    return parseBitcoinURI(scheme, uriInstance, out);
}

QString formatBitcoinURI(const Config &cfg, const SendCoinsRecipient &info)
{
    QString ret = info.address;
    if (!cfg.UseCashAddrEncoding())
    {
        // prefix address with uri scheme for base58 encoded addresses.
        ret = (bitcoinURIScheme(cfg) + ":%1").arg(ret);
    }
    int paramCount = 0;

    if (info.amount)
    {
        ret += QString("?amount=%1")
                   .arg(BitcoinUnits::format(BitcoinUnits::BCH, info.amount, false, BitcoinUnits::separatorNever));
        paramCount++;
    }

    if (!info.label.isEmpty())
    {
        QString lbl(QUrl::toPercentEncoding(info.label));
        ret += QString("%1label=%2").arg(paramCount == 0 ? "?" : "&").arg(lbl);
        paramCount++;
    }

    if (!info.message.isEmpty())
    {
        QString msg(QUrl::toPercentEncoding(info.message));
        ;
        ret += QString("%1message=%2").arg(paramCount == 0 ? "?" : "&").arg(msg);
        paramCount++;
    }
    // TODO Unsure whether it helps to include the Freeze message (info.freezeLockTime) in the URL ?

    return ret;
}

bool isDust(const QString &address, const CAmount &amount)
{
    CTxDestination dest = DecodeDestination(address.toStdString());
    CScript script = GetScriptForDestination(dest);
    CTxOut txOut(amount, script);
    return txOut.IsDust();
}

QString HtmlEscape(const QString &str, bool fMultiLine)
{
    QString escaped = str.toHtmlEscaped();
    if (fMultiLine)
    {
        escaped = escaped.replace("\n", "<br>\n");
    }
    return escaped;
}

QString HtmlEscape(const std::string &str, bool fMultiLine)
{
    return HtmlEscape(QString::fromStdString(str), fMultiLine);
}

void copyEntryData(QAbstractItemView *view, int column, int role)
{
    if (!view || !view->selectionModel())
        return;
    QModelIndexList selection = view->selectionModel()->selectedRows(column);

    if (!selection.isEmpty())
    {
        // Copy first item
        setClipboard(selection.at(0).data(role).toString());
    }
}

QString getEntryData(QAbstractItemView *view, int column, int role)
{
    if (!view || !view->selectionModel())
        return QString();
    QModelIndexList selection = view->selectionModel()->selectedRows(column);

    if (!selection.isEmpty())
    {
        // Return first item
        return (selection.at(0).data(role).toString());
    }
    return QString();
}

QString getSaveFileName(QWidget *parent,
    const QString &caption,
    const QString &dir,
    const QString &filter,
    QString *selectedSuffixOut)
{
    QString selectedFilter;
    QString myDir;
    if (dir.isEmpty()) // Default to user documents location
    {
        myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    else
    {
        myDir = dir;
    }
    /* Directly convert path to native OS path separators */
    QString result =
        QDir::toNativeSeparators(QFileDialog::getSaveFileName(parent, caption, myDir, filter, &selectedFilter));

    /* Extract first suffix from filter pattern "Description (*.foo)" or "Description (*.foo *.bar ...) */
    QRegExp filter_re(".* \\(\\*\\.(.*)[ \\)]");
    QString selectedSuffix;
    if (filter_re.exactMatch(selectedFilter))
    {
        selectedSuffix = filter_re.cap(1);
    }

    /* Add suffix if needed */
    QFileInfo info(result);
    if (!result.isEmpty())
    {
        if (info.suffix().isEmpty() && !selectedSuffix.isEmpty())
        {
            /* No suffix specified, add selected suffix */
            if (!result.endsWith("."))
                result.append(".");
            result.append(selectedSuffix);
        }
    }

    /* Return selected suffix if asked to */
    if (selectedSuffixOut)
    {
        *selectedSuffixOut = selectedSuffix;
    }
    return result;
}

QString getOpenFileName(QWidget *parent,
    const QString &caption,
    const QString &dir,
    const QString &filter,
    QString *selectedSuffixOut)
{
    QString selectedFilter;
    QString myDir;
    if (dir.isEmpty()) // Default to user documents location
    {
        myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    else
    {
        myDir = dir;
    }
    /* Directly convert path to native OS path separators */
    QString result =
        QDir::toNativeSeparators(QFileDialog::getOpenFileName(parent, caption, myDir, filter, &selectedFilter));

    if (selectedSuffixOut)
    {
        /* Extract first suffix from filter pattern "Description (*.foo)" or "Description (*.foo *.bar ...) */
        QRegExp filter_re(".* \\(\\*\\.(.*)[ \\)]");
        QString selectedSuffix;
        if (filter_re.exactMatch(selectedFilter))
        {
            selectedSuffix = filter_re.cap(1);
        }
        *selectedSuffixOut = selectedSuffix;
    }
    return result;
}

Qt::ConnectionType blockingGUIThreadConnection()
{
    if (QThread::currentThread() != qApp->thread())
    {
        return Qt::BlockingQueuedConnection;
    }
    else
    {
        return Qt::DirectConnection;
    }
}

bool checkPoint(const QPoint &p, const QWidget *w)
{
    QWidget *atW = QApplication::widgetAt(w->mapToGlobal(p));
    if (!atW)
        return false;
    return atW->topLevelWidget() == w;
}

bool isObscured(QWidget *w)
{
    return !(checkPoint(QPoint(0, 0), w) && checkPoint(QPoint(w->width() - 1, 0), w) &&
             checkPoint(QPoint(0, w->height() - 1), w) && checkPoint(QPoint(w->width() - 1, w->height() - 1), w) &&
             checkPoint(QPoint(w->width() / 2, w->height() / 2), w));
}

void openDebugLogfile()
{
    fs::path pathDebug = GetDataDir() / "debug.log";

    /* Open debug.log with the associated application */
    if (fs::exists(pathDebug))
        QDesktopServices::openUrl(QUrl::fromLocalFile(boostPathToQString(pathDebug)));
}

void SubstituteFonts(const QString &language)
{
#if defined(Q_OS_MAC)
// Background:
// OSX's default font changed in 10.9 and Qt is unable to find it with its
// usual fallback methods when building against the 10.7 sdk or lower.
// The 10.8 SDK added a function to let it find the correct fallback font.
// If this fallback is not properly loaded, some characters may fail to
// render correctly.
//
// The same thing happened with 10.10. .Helvetica Neue DeskInterface is now default.
//
// Solution: If building with the 10.7 SDK or lower and the user's platform
// is 10.9 or higher at runtime, substitute the correct font. This needs to
// happen before the QApplication is created.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_8
    if (floor(NSAppKitVersionNumber) > NSAppKitVersionNumber10_8)
    {
        if (floor(NSAppKitVersionNumber) <= NSAppKitVersionNumber10_9)
            /* On a 10.9 - 10.9.x system */
            QFont::insertSubstitution(".Lucida Grande UI", "Lucida Grande");
        else
        {
            /* 10.10 or later system */
            if (language == "zh_CN" || language == "zh_TW" || language == "zh_HK") // traditional or simplified Chinese
                QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Heiti SC");
            else if (language == "ja") // Japanesee
                QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Songti SC");
            else
                QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Lucida Grande");
        }
    }
#endif
#endif
}

ToolTipToRichTextFilter::ToolTipToRichTextFilter(int _size_threshold, QObject *parent)
    : QObject(parent), size_threshold(_size_threshold)
{
}

bool ToolTipToRichTextFilter::eventFilter(QObject *obj, QEvent *evt)
{
    if (evt->type() == QEvent::ToolTipChange)
    {
        QWidget *widget = static_cast<QWidget *>(obj);
        QString tooltip = widget->toolTip();
        if (tooltip.size() > size_threshold && !tooltip.startsWith("<qt") && !Qt::mightBeRichText(tooltip))
        {
            // Envelop with <qt></qt> to make sure Qt detects this as rich text
            // Escape the current message as HTML and replace \n by <br>
            tooltip = "<qt>" + HtmlEscape(tooltip, true) + "</qt>";
            widget->setToolTip(tooltip);
            return true;
        }
    }
    return QObject::eventFilter(obj, evt);
}

void TableViewLastColumnResizingFixer::connectViewHeadersSignals()
{
    connect(tableView->horizontalHeader(), SIGNAL(sectionResized(int, int, int)), this,
        SLOT(on_sectionResized(int, int, int)));
    connect(tableView->horizontalHeader(), SIGNAL(geometriesChanged()), this, SLOT(on_geometriesChanged()));
}

// We need to disconnect these while handling the resize events, otherwise we can enter infinite loops.
void TableViewLastColumnResizingFixer::disconnectViewHeadersSignals()
{
    disconnect(tableView->horizontalHeader(), SIGNAL(sectionResized(int, int, int)), this,
        SLOT(on_sectionResized(int, int, int)));
    disconnect(tableView->horizontalHeader(), SIGNAL(geometriesChanged()), this, SLOT(on_geometriesChanged()));
}

// Setup the resize mode, handles compatibility for Qt5 and below as the method signatures changed.
// Refactored here for readability.
void TableViewLastColumnResizingFixer::setViewHeaderResizeMode(int logicalIndex, QHeaderView::ResizeMode resizeMode)
{
    tableView->horizontalHeader()->setSectionResizeMode(logicalIndex, resizeMode);
}

void TableViewLastColumnResizingFixer::resizeColumn(int nColumnIndex, int width)
{
    tableView->setColumnWidth(nColumnIndex, width);
    tableView->horizontalHeader()->resizeSection(nColumnIndex, width);
}

int TableViewLastColumnResizingFixer::getColumnsWidth()
{
    int nColumnsWidthSum = 0;
    for (int i = 0; i < columnCount; i++)
    {
        nColumnsWidthSum += tableView->horizontalHeader()->sectionSize(i);
    }
    return nColumnsWidthSum;
}

int TableViewLastColumnResizingFixer::getAvailableWidthForColumn(int column)
{
    int nResult = lastColumnMinimumWidth;
    int nTableWidth = tableView->horizontalHeader()->width();

    if (nTableWidth > 0)
    {
        int nOtherColsWidth = getColumnsWidth() - tableView->horizontalHeader()->sectionSize(column);
        nResult = std::max(nResult, nTableWidth - nOtherColsWidth);
    }

    return nResult;
}

// Make sure we don't make the columns wider than the tables viewport width.
void TableViewLastColumnResizingFixer::adjustTableColumnsWidth()
{
    disconnectViewHeadersSignals();
    resizeColumn(lastColumnIndex, getAvailableWidthForColumn(lastColumnIndex));
    connectViewHeadersSignals();

    int nTableWidth = tableView->horizontalHeader()->width();
    int nColsWidth = getColumnsWidth();
    if (nColsWidth > nTableWidth)
    {
        resizeColumn(secondToLastColumnIndex, getAvailableWidthForColumn(secondToLastColumnIndex));
    }
}

// Make column use all the space available, useful during window resizing.
void TableViewLastColumnResizingFixer::stretchColumnWidth(int column)
{
    disconnectViewHeadersSignals();
    resizeColumn(column, getAvailableWidthForColumn(column));
    connectViewHeadersSignals();
}

// When a section is resized this is a slot-proxy for ajustAmountColumnWidth().
void TableViewLastColumnResizingFixer::on_sectionResized(int logicalIndex, int oldSize, int newSize)
{
    adjustTableColumnsWidth();
    int remainingWidth = getAvailableWidthForColumn(logicalIndex);
    if (newSize > remainingWidth)
    {
        resizeColumn(logicalIndex, remainingWidth);
    }
}

// When the tabless geometry is ready, we manually perform the stretch of the "Message" column,
// as the "Stretch" resize mode does not allow for interactive resizing.
void TableViewLastColumnResizingFixer::on_geometriesChanged()
{
    if ((getColumnsWidth() - this->tableView->horizontalHeader()->width()) != 0)
    {
        disconnectViewHeadersSignals();
        resizeColumn(secondToLastColumnIndex, getAvailableWidthForColumn(secondToLastColumnIndex));
        connectViewHeadersSignals();
    }
}

/**
 * Initializes all internal variables and prepares the
 * the resize modes of the last 2 columns of the table and
 */
TableViewLastColumnResizingFixer::TableViewLastColumnResizingFixer(QTableView *table,
    int lastColMinimumWidth,
    int allColsMinimumWidth,
    QObject *parent)
    : QObject(parent), tableView(table), lastColumnMinimumWidth(lastColMinimumWidth),
      allColumnsMinimumWidth(allColsMinimumWidth)
{
    columnCount = tableView->horizontalHeader()->count();
    lastColumnIndex = columnCount - 1;
    secondToLastColumnIndex = columnCount - 2;
    tableView->horizontalHeader()->setMinimumSectionSize(allColumnsMinimumWidth);
    setViewHeaderResizeMode(secondToLastColumnIndex, QHeaderView::Interactive);
    setViewHeaderResizeMode(lastColumnIndex, QHeaderView::Interactive);
}

#ifdef WIN32
fs::path static StartupShortcutPath()
{
    std::string chain = ChainNameFromCommandLine();
    if (chain == CBaseChainParams::MAIN)
        return GetSpecialFolderPath(CSIDL_STARTUP) / "Bitcoin.lnk";
    if (chain == CBaseChainParams::TESTNET) // Remove this special case when CBaseChainParams::TESTNET = "testnet4"
        return GetSpecialFolderPath(CSIDL_STARTUP) / "Bitcoin (testnet).lnk";
    return GetSpecialFolderPath(CSIDL_STARTUP) / strprintf("Bitcoin (%s).lnk", chain);
}

bool GetStartOnSystemStartup()
{
    // check for Bitcoin*.lnk
    return fs::exists(StartupShortcutPath());
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    // If the shortcut exists already, remove it for updating
    fs::remove(StartupShortcutPath());

    if (fAutoStart)
    {
        CoInitialize(NULL);

        // Get a pointer to the IShellLink interface.
        IShellLink *psl = NULL;
        HRESULT hres = CoCreateInstance(
            CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, reinterpret_cast<void **>(&psl));

        if (SUCCEEDED(hres))
        {
            // Get the current executable path
            TCHAR pszExePath[MAX_PATH];
            GetModuleFileName(NULL, pszExePath, sizeof(pszExePath));

            // Start client minimized
            QString strArgs = "-min";
            // Set -testnet /-regtest options
            strArgs += QString::fromStdString(
                strprintf(" -testnet=%d -regtest=%d", GetBoolArg("-testnet", false), GetBoolArg("-regtest", false)));

#ifdef UNICODE
            boost::scoped_array<TCHAR> args(new TCHAR[strArgs.length() + 1]);
            // Convert the QString to TCHAR*
            strArgs.toWCharArray(args.get());
            // Add missing '\0'-termination to string
            args[strArgs.length()] = '\0';
#endif

            // Set the path to the shortcut target
            psl->SetPath(pszExePath);
            PathRemoveFileSpec(pszExePath);
            psl->SetWorkingDirectory(pszExePath);
            psl->SetShowCmd(SW_SHOWMINNOACTIVE);
#ifndef UNICODE
            psl->SetArguments(strArgs.toStdString().c_str());
#else
            psl->SetArguments(args.get());
#endif

            // Query IShellLink for the IPersistFile interface for
            // saving the shortcut in persistent storage.
            IPersistFile *ppf = NULL;
            hres = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&ppf));
            if (SUCCEEDED(hres))
            {
                WCHAR pwsz[MAX_PATH];
                // Ensure that the string is ANSI.
                MultiByteToWideChar(CP_ACP, 0, StartupShortcutPath().string().c_str(), -1, pwsz, MAX_PATH);
                // Save the link by calling IPersistFile::Save.
                hres = ppf->Save(pwsz, TRUE);
                ppf->Release();
                psl->Release();
                CoUninitialize();
                return true;
            }
            psl->Release();
        }
        CoUninitialize();
        return false;
    }
    return true;
}
#elif defined(Q_OS_LINUX)

// Follow the Desktop Application Autostart Spec:
// http://standards.freedesktop.org/autostart-spec/autostart-spec-latest.html

fs::path static GetAutostartDir()
{
    char *pszConfigHome = getenv("XDG_CONFIG_HOME");
    if (pszConfigHome)
        return fs::path(pszConfigHome) / "autostart";
    char *pszHome = getenv("HOME");
    if (pszHome)
        return fs::path(pszHome) / ".config" / "autostart";
    return fs::path();
}

fs::path static GetAutostartFilePath()
{
    std::string chain = ChainNameFromCommandLine();
    if (chain == CBaseChainParams::MAIN)
        return GetAutostartDir() / "bitcoin.desktop";
    return GetAutostartDir() / strprintf("bitcoin-%s.lnk", chain);
}

bool GetStartOnSystemStartup()
{
    fs::ifstream optionFile(GetAutostartFilePath());
    if (!optionFile.good())
        return false;
    // Scan through file for "Hidden=true":
    std::string line;
    while (!optionFile.eof())
    {
        getline(optionFile, line);
        if (line.find("Hidden") != std::string::npos && line.find("true") != std::string::npos)
            return false;
    }
    optionFile.close();

    return true;
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    if (!fAutoStart)
        fs::remove(GetAutostartFilePath());
    else
    {
        char pszExePath[MAX_PATH + 1];
        memset(pszExePath, 0, sizeof(pszExePath));
        if (readlink("/proc/self/exe", pszExePath, sizeof(pszExePath) - 1) == -1)
            return false;

        fs::create_directories(GetAutostartDir());

        fs::ofstream optionFile(GetAutostartFilePath(), std::ios_base::out | std::ios_base::trunc);
        if (!optionFile.good())
            return false;
        std::string chain = ChainNameFromCommandLine();
        // Write a bitcoin.desktop file to the autostart directory:
        optionFile << "[Desktop Entry]\n";
        optionFile << "Type=Application\n";
        if (chain == CBaseChainParams::MAIN)
            optionFile << "Name=Bitcoin\n";
        else
            optionFile << strprintf("Name=Bitcoin (%s)\n", chain);
        optionFile << "Exec=" << pszExePath << strprintf(" -min -testnet=%d -regtest=%d\n",
                                                   GetBoolArg("-testnet", false), GetBoolArg("-regtest", false));
        optionFile << "Terminal=false\n";
        optionFile << "Hidden=false\n";
        optionFile.close();
    }
    return true;
}


#elif defined(Q_OS_MAC)
// based on: https://github.com/Mozketo/LaunchAtLoginController/blob/master/LaunchAtLoginController.m

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

// NB: caller must release returned ref if it's not NULL
LSSharedFileListItemRef findStartupItemInList(LSSharedFileListRef list, CFURLRef findUrl);
LSSharedFileListItemRef findStartupItemInList(LSSharedFileListRef list, CFURLRef findUrl)
{
    LSSharedFileListItemRef foundItem = NULL;

    // loop through the list of startup items and try to find the app
    CFArrayRef listSnapshot = LSSharedFileListCopySnapshot(list, NULL);
    for (int i = 0; !foundItem && i < CFArrayGetCount(listSnapshot); ++i)
    {
        LSSharedFileListItemRef item = (LSSharedFileListItemRef)CFArrayGetValueAtIndex(listSnapshot, i);
        UInt32 resolutionFlags = kLSSharedFileListNoUserInteraction | kLSSharedFileListDoNotMountVolumes;
        CFURLRef currentItemURL = NULL;

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 10100
        if (&LSSharedFileListItemCopyResolvedURL)
            currentItemURL = LSSharedFileListItemCopyResolvedURL(item, resolutionFlags, NULL);
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED < 10100
        else
            LSSharedFileListItemResolve(item, resolutionFlags, &currentItemURL, NULL);
#endif
#else
        LSSharedFileListItemResolve(item, resolutionFlags, &currentItemURL, NULL);
#endif

        if (currentItemURL && CFEqual(currentItemURL, findUrl))
        {
            // found
            CFRetain(foundItem = item);
        }
        if (currentItemURL)
        {
            CFRelease(currentItemURL);
        }
    }
    CFRelease(listSnapshot);
    return foundItem;
}

bool GetStartOnSystemStartup()
{
    CFURLRef bitcoinAppUrl = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    LSSharedFileListRef loginItems = LSSharedFileListCreate(NULL, kLSSharedFileListSessionLoginItems, NULL);
    LSSharedFileListItemRef foundItem = findStartupItemInList(loginItems, bitcoinAppUrl);
    // findStartupItemInList retains the item it returned, need to release
    if (foundItem)
        CFRelease(foundItem);
    CFRelease(loginItems);
    CFRelease(bitcoinAppUrl);
    return !!foundItem;
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    CFURLRef bitcoinAppUrl = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    LSSharedFileListRef loginItems = LSSharedFileListCreate(NULL, kLSSharedFileListSessionLoginItems, NULL);
    LSSharedFileListItemRef foundItem = findStartupItemInList(loginItems, bitcoinAppUrl);

    if (fAutoStart && !foundItem)
    {
        // add bitcoin app to startup item list
        LSSharedFileListInsertItemURL(
            loginItems, kLSSharedFileListItemBeforeFirst, NULL, NULL, bitcoinAppUrl, NULL, NULL);
    }
    else if (!fAutoStart && foundItem)
    {
        // remove item
        LSSharedFileListItemRemove(loginItems, foundItem);
    }
    // findStartupItemInList retains the item it returned, need to release
    if (foundItem)
        CFRelease(foundItem);
    CFRelease(loginItems);
    CFRelease(bitcoinAppUrl);
    return true;
}
#else

bool GetStartOnSystemStartup() { return false; }
bool SetStartOnSystemStartup(bool fAutoStart) { return false; }

#endif

void saveWindowGeometry(const QString &strSetting, QWidget *parent)
{
    QSettings settings;
    settings.setValue(strSetting + "Pos", parent->pos());
    settings.setValue(strSetting + "Size", parent->size());
}

void restoreWindowGeometry(const QString &strSetting, const QSize &defaultSize, QWidget *parent)
{
    QSettings settings;
    QPoint pos = settings.value(strSetting + "Pos").toPoint();
    QSize size = settings.value(strSetting + "Size", defaultSize).toSize();
    QRect screen = QApplication::desktop()->screenGeometry();
    QPoint posCenter(abs((screen.width() - size.width()) / 2), abs((screen.height() - size.height()) / 2));

    if (!pos.x() && !pos.y())
    {
        QRect _screen = QApplication::desktop()->screenGeometry();
        pos.setX((_screen.width() - size.width()) / 2);
        pos.setY((_screen.height() - size.height()) / 2);
    }

    parent->resize(size);
    parent->move(pos);

    if (QApplication::desktop()->screenNumber(parent) == -1)
        parent->move(posCenter);
}

void setClipboard(const QString &str)
{
    QApplication::clipboard()->setText(str, QClipboard::Clipboard);
    QApplication::clipboard()->setText(str, QClipboard::Selection);
}

fs::path qstringToBoostPath(const QString &path) { return fs::path(path.toStdString(), utf8); }
QString boostPathToQString(const fs::path &path) { return QString::fromStdString(path.string(utf8)); }
QString formatDurationStr(int secs)
{
    QStringList strList;
    int days = secs / 86400;
    int hours = (secs % 86400) / 3600;
    int mins = (secs % 3600) / 60;
    int seconds = secs % 60;

    if (days)
        strList.append(QString(QObject::tr("%1 d")).arg(days));
    if (hours)
        strList.append(QString(QObject::tr("%1 h")).arg(hours));
    if (mins)
        strList.append(QString(QObject::tr("%1 m")).arg(mins));
    if (seconds || (!days && !hours && !mins))
        strList.append(QString(QObject::tr("%1 s")).arg(seconds));

    return strList.join(" ");
}

QString formatServicesStr(quint64 mask, const QStringList &additionalServices)
{
    QStringList strList;

    // Scan and process until we reach the highest set bit
    for (int i = 0; i < 32; i++)
    {
        uint64_t check = 1ULL << i;
        if (check > mask)
            break;

        if (mask & check)
        {
            switch (check)
            {
            case NODE_NETWORK:
                strList.append("NETWORK");
                break;
            case NODE_GETUTXO:
                strList.append("GETUTXO");
                break;
            case NODE_BLOOM:
                strList.append("BLOOM");
                break;
            case NODE_WITNESS:
                strList.append("WITNESS");
                break;
            case NODE_XTHIN:
                strList.append("XTHIN");
                break;
            case NODE_BITCOIN_CASH:
                strList.append("CASH");
                break;
            case NODE_GRAPHENE:
                strList.append("GRAPH");
                break;
            case NODE_WEAKBLOCKS:
                strList.append("WB");
                break;
            case NODE_CF:
                strList.append("CF");
                break;
            default:
                strList.append(QString("%1[%2]").arg("UNKNOWN").arg(check));
            }
        }
    }

    // Adds in additional services not denoted by nServices bits
    if (additionalServices.size())
        strList.append(additionalServices);

    if (strList.size())
        return strList.join(", ");
    else
        return QObject::tr("None");
}

QString formatPingTime(double dPingTime)
{
    return dPingTime == 0 ? QObject::tr("N/A") :
                            QString(QObject::tr("%1 ms")).arg(QString::number((int)(dPingTime * 1000), 10));
}

QString formatTimeOffset(int64_t nTimeOffset)
{
    return QString(QObject::tr("%1 s")).arg(QString::number((int)nTimeOffset, 10));
}

QString uriPrefix() { return "bitcoincash"; }
} // namespace GUIUtil
