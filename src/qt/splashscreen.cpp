// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/splashscreen.h>
#include <qt/guiutil.h>
#include <qt/networkstyle.h>

#include <clientversion.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <util.h>
#include <ui_interface.h>
#include <version.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>
#include <QRadialGradient>

#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

SplashScreen::SplashScreen(interfaces::Node& node, Qt::WindowFlags f, const NetworkStyle *networkStyle) :
    QWidget(0, f), curAlignment(0), m_node(node)
{
    // set reference point, paddings
    int paddingTop        = 60;
    fontFactor            = 1.0;
    devicePixelRatio      = 1.0;
#if QT_VERSION > 0x050100
	devicePixelRatio = static_cast<QGuiApplication*>(QCoreApplication::instance())->devicePixelRatio();
#endif

    this->setStyleSheet(GUIUtil::loadStyleSheet());

    // define text to place
    QString titleText       = "Bitcoin Confidential";
    QString versionText     = QString("%1").arg(QString::fromStdString(FormatFullVersion()));
    QString titleAddText    = networkStyle->getTitleAddText();

    QString font =   "Lato";

    // create a bitmap according to device pixelratio
    QSize splashSize(380*devicePixelRatio,480*devicePixelRatio);
    pixmap = QPixmap(splashSize);

#if QT_VERSION > 0x050100
    // change to HiDPI if it makes sense
    pixmap.setDevicePixelRatio(devicePixelRatio);
#endif

    QPainter pixPaint(&pixmap);
    pixPaint.setPen(QColor(244,244,244));

    pixPaint.fillRect(QRect(0,0,splashSize.width()/devicePixelRatio,splashSize.height()/devicePixelRatio), QColor(35,35,35));

    // draw the bitcoin icon, expected size of PNG: 1024x1024
    QRect rectIcon(QPoint(30,105), QSize(320,320));

    const QSize requiredSize(1024,1024);
    QPixmap icon(networkStyle->getAppIcon().pixmap(requiredSize));

    pixPaint.drawPixmap(rectIcon, icon);

    // TITLE START

    // check font size and drawing with
    QFont titleFont = QFont(font, 33*fontFactor,1000);
    titleFont.setWeight(QFont::Bold);

    pixPaint.setFont(titleFont);
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth = fm.width(titleText.toUpper());
    if (titleTextWidth > 320) {
        fontFactor = fontFactor * 320 / titleTextWidth;
    }

    titleFont = QFont(font, 33*fontFactor, 1000);
    titleFont.setWeight(QFont::Bold);

    pixPaint.setFont(titleFont);
    fm = pixPaint.fontMetrics();
    titleTextWidth  = fm.width(titleText.toUpper());
    int titleTextHeight = fm.height();
    pixPaint.drawText(((pixmap.width()/devicePixelRatio)-titleTextWidth)/2,paddingTop,titleText.toUpper());

    // TITLE END

    // VERSION START
    pixPaint.setFont(QFont(font, 25*fontFactor));
    pixPaint.setPen(QColor(114,114,114));
    // if the version string is too long, reduce size
    fm = pixPaint.fontMetrics();
    int versionTextWidth  = fm.width(versionText);
    pixPaint.drawText(((pixmap.width()/devicePixelRatio)-versionTextWidth)/2, paddingTop + titleTextHeight + 5,versionText);

    // VERSION END

    // SPECIAL NET START

    // draw additional text if special network
    if(!titleAddText.isEmpty()) {
        QFont boldFont = QFont(font, 13*fontFactor);
        boldFont.setWeight(QFont::Bold);
        pixPaint.setFont(boldFont);
        fm = pixPaint.fontMetrics();
        int titleAddTextWidth  = fm.width(titleAddText);
        pixPaint.drawText(pixmap.width()/devicePixelRatio-titleAddTextWidth-10,15,titleAddText);
    }

    // SPECIAL NET END

    pixPaint.end();

    // Set window title
    setWindowTitle(titleText + " Core " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width()/devicePixelRatio,pixmap.size().height()/devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QApplication::desktop()->screenGeometry().center() - r.center());

    subscribeToCoreSignals();
    installEventFilter(this);
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

bool SplashScreen::eventFilter(QObject * obj, QEvent * ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if(keyEvent->text()[0] == 'q') {
            m_node.startShutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);

    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized())
        showNormal();
    hide();
    deleteLater(); // No more need for this
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(244,244,244)));
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible)
{
    InitMessage(splash, title + std::string("\n") +
            (resume_possible ? _("(press q to shutdown and continue later)")
                                : _("press q to shutdown")) +
            strprintf("\n%d", nProgress) + "%");
}
#ifdef ENABLE_WALLET
void SplashScreen::ConnectWallet(std::unique_ptr<interfaces::Wallet> wallet)
{
    m_connected_wallet_handlers.emplace_back(wallet->handleShowProgress(boost::bind(ShowProgress, this, _1, _2, false)));
    m_connected_wallets.emplace_back(std::move(wallet));
}
#endif

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    m_handler_init_message = m_node.handleInitMessage(boost::bind(InitMessage, this, _1));
    m_handler_show_progress = m_node.handleShowProgress(boost::bind(ShowProgress, this, _1, _2, _3));
#ifdef ENABLE_WALLET
    m_handler_load_wallet = m_node.handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) { ConnectWallet(std::move(wallet)); });
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
    for (auto& handler : m_connected_wallet_handlers) {
        handler->disconnect();
    }
    m_connected_wallet_handlers.clear();
    m_connected_wallets.clear();
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
    QRect r = rect().adjusted(5, 5, -10, -10);
    painter.setPen(curColor);
    painter.setFont(QFont("Lato", 13));
    float fontFactor = 1.0;
    QFontMetrics fm = painter.fontMetrics();
    int textWidth = fm.width(curMessage.toUpper());
    if (textWidth > 320) {
        fontFactor = fontFactor * 320 / textWidth;
    }

    painter.setFont(QFont("Lato", 13 * fontFactor));

    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    m_node.startShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
