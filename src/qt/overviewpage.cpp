// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>

#include <QAbstractItemDelegate>
#include <QHBoxLayout>
#include <QPainter>

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

Q_DECLARE_METATYPE(interfaces::WalletBalances)

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::BTC),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        painter->setFont(QFont("Lato"));

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->BitcoinCColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        QString typeOut = index.data(TransactionTableModel::TxOutTypeRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if (!confirmed) {
            foreground = COLOR_UNCONFIRMED;
        } else if(amount < 0) {
            foreground = COLOR_NEGATIVE;
        } else if (amount > 0) {
            foreground = COLOR_POSITIVE;
        } else {
            foreground = option.palette.color(QPalette::Text);
        }

        painter->setPen(foreground);

        QString amountText;

        if( typeOut == "BC" && amount == 0 ){
            amountText = tr("Unlock to show");
        }else{
            amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);

            if(!confirmed)
            {
                amountText = QString("[") + amountText + QString("]");
            }
        }

        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);

    m_balances.balanceSpending = -1;

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));

    const QSize requiredSize(90,90);
    QPixmap pixmap = QIcon(":/icons/bitcoin").pixmap(requiredSize);
    QLabel *lblLogo = new QLabel();
    lblLogo->setPixmap(pixmap);
    ((QHBoxLayout*)ui->widgetLogo->layout())->insertWidget(1,lblLogo);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const interfaces::WalletBalances& balances)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    m_balances = balances;

    if (walletModel->privateKeysDisabled()) {
        // Watch balances
        ui->labelWatchSpending->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchSpending, false, BitcoinUnits::separatorAlways));
        ui->labelWatchUnconfirmedSpending->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchSpendingUnconf, false, BitcoinUnits::separatorAlways));
        ui->labelWatchLockedSpending->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchSpendingLocked, false, BitcoinUnits::separatorAlways));

        ui->labelWatchStaking->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchStaking, false, BitcoinUnits::separatorAlways));
        ui->labelWatchUnconfirmedStaking->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchStakingUnconf, false, BitcoinUnits::separatorAlways));
        ui->labelWatchLockedStaking->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchStakingLocked, false, BitcoinUnits::separatorAlways));

        ui->lblWatchTotal->setText(BitcoinUnits::formatWithUnit(unit,
              balances.balanceWatchSpending + balances.balanceWatchSpendingUnconf + balances.balanceWatchSpendingLocked
            + balances.balanceWatchStaking + balances.balanceWatchStakingUnconf + balances.balanceWatchStakingLocked, false, BitcoinUnits::separatorAlways));
    } else {
        // Owned balances
        ui->labelSpending->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceSpending, false, BitcoinUnits::separatorAlways));
        ui->labelUnconfirmedSpending->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceSpendingUnconf, false, BitcoinUnits::separatorAlways));
        ui->labelLockedSpending->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceSpendingLocked, false, BitcoinUnits::separatorAlways));

        ui->labelStaking->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceStaking, false, BitcoinUnits::separatorAlways));
        ui->labelUnconfirmedStaking->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceStakingUnconf, false, BitcoinUnits::separatorAlways));
        ui->labelLockedStaking->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceStakingLocked, false, BitcoinUnits::separatorAlways));

        ui->lblTotalBalance->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceSpending + balances.balanceSpendingUnconf + balances.balanceSpendingLocked
            + balances.balanceStaking + balances.balanceStakingUnconf + balances.balanceStakingLocked , false, BitcoinUnits::separatorAlways));

        // Watch balances
        ui->labelWatchSpending->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchSpending, false, BitcoinUnits::separatorAlways));
        ui->labelWatchUnconfirmedSpending->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchSpendingUnconf, false, BitcoinUnits::separatorAlways));
        ui->labelWatchLockedSpending->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchSpendingLocked, false, BitcoinUnits::separatorAlways));

        ui->labelWatchStaking->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchStaking, false, BitcoinUnits::separatorAlways));
        ui->labelWatchUnconfirmedStaking->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchStakingUnconf, false, BitcoinUnits::separatorAlways));
        ui->labelWatchLockedStaking->setText(BitcoinUnits::formatWithUnit(unit, balances.balanceWatchStakingLocked, false, BitcoinUnits::separatorAlways));

        ui->lblWatchTotal->setText(BitcoinUnits::formatWithUnit(unit,
              balances.balanceWatchSpending + balances.balanceWatchSpendingUnconf + balances.balanceWatchSpendingLocked
            + balances.balanceWatchStaking + balances.balanceWatchStakingUnconf + balances.balanceWatchStakingLocked, false, BitcoinUnits::separatorAlways));

    }

    bool fWatchVisible = ui->labelSpendable->isVisible();

    // Show pending on if there is something pending
    ui->labelUnconfirmedSpendingText->setVisible(balances.balanceSpendingUnconf > 0 || balances.balanceWatchSpendingUnconf > 0 || fWatchVisible);
    ui->labelUnconfirmedSpending->setVisible(balances.balanceSpendingUnconf > 0 || balances.balanceWatchSpendingUnconf > 0 || fWatchVisible);

    ui->labelLockedSpendingText->setVisible(balances.balanceSpendingLocked > 0 || balances.balanceWatchSpendingLocked > 0 || fWatchVisible);
    ui->labelLockedSpending->setVisible(balances.balanceSpendingLocked > 0 || balances.balanceWatchSpendingLocked > 0 || fWatchVisible);

    ui->labelUnconfirmedStakingText->setVisible(balances.balanceStakingUnconf > 0 || balances.balanceWatchStakingUnconf > 0 || fWatchVisible);
    ui->labelUnconfirmedStaking->setVisible(balances.balanceStakingUnconf > 0 || balances.balanceWatchStakingUnconf > 0 || fWatchVisible);

    ui->labelLockedStakingText->setVisible(balances.balanceStakingLocked > 0 || balances.balanceWatchStakingLocked > 0 || fWatchVisible);
    ui->labelLockedStaking->setVisible(balances.balanceStakingLocked > 0 || balances.balanceWatchStakingLocked > 0 || fWatchVisible);
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label

    ui->labelWatchSpending->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchUnconfirmedSpending->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchLockedSpending->setVisible(showWatchOnly);   // show watch-only pending balance

    ui->labelWatchStaking->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchUnconfirmedStaking->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchLockedStaking->setVisible(showWatchOnly);   // show watch-only pending balance

    ui->lblWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if( showWatchOnly ){
        ui->labelUnconfirmedSpendingText->setVisible(true);
        ui->labelUnconfirmedSpending->setVisible(true);

        ui->labelLockedSpendingText->setVisible(true);
        ui->labelLockedSpending->setVisible(true);

        ui->labelUnconfirmedStakingText->setVisible(true);
        ui->labelUnconfirmedStaking->setVisible(true);

        ui->labelLockedStakingText->setVisible(true);
        ui->labelLockedStaking->setVisible(true);
    }
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        interfaces::Wallet& wallet = model->wallet();
        interfaces::WalletBalances balances = wallet.getBalances();
        setBalance(balances);
        connect(model, SIGNAL(balanceChanged(interfaces::WalletBalances)), this, SLOT(setBalance(interfaces::WalletBalances)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        bool fHaveWatchOnly = balances.balanceWatchSpending || balances.balanceWatchSpendingUnconf || balances.balanceWatchSpendingLocked ||
                              balances.balanceWatchStaking || balances.balanceWatchStakingUnconf || balances.balanceWatchStakingLocked;

        updateWatchOnlyLabels(wallet.haveWatchOnly() || fHaveWatchOnly);
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if (m_balances.balanceSpending != -1) {
            setBalance(m_balances);
        }

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelTransactionsStatus->setVisible(fShow);
}
