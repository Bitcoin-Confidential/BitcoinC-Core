// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_STAKINGDIALOG_H
#define BITCOIN_QT_STAKINGDIALOG_H

#include <qt/walletmodel.h>
#include <qt/coincontroldialog.h>

#include <QButtonGroup>
#include <QDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>

class ClientModel;
class PlatformStyle;
class SendCoinsEntry;
class SendCoinsRecipient;
class UniValue;

namespace Ui {
    class StakingDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;

QT_END_NAMESPACE

/** Dialog for sending bitcoins */
class StakingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StakingDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~StakingDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *model);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setAddress(const QString &address);
    void pasteEntry(const SendCoinsRecipient &rv);
    bool handlePaymentRequest(const SendCoinsRecipient &recipient);

public Q_SLOTS:
    void clear();
    void reject();
    void accept();
    SendCoinsEntry *addEntry();
    SendCoinsEntry *addEntryCS();
    void updateTabsAndLabels();
    void setBalance(const interfaces::WalletBalances& balances);

Q_SIGNALS:
    void coinsSent(const uint256& txid);

private:

    enum{
        OVERVIEW,
        TO_SPENDING,
        TO_STAKING,
        TO_COLD_STAKING
    };

    Ui::StakingDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;
    bool fNewRecipientAllowed;
    bool fFeeMinimized;
    const PlatformStyle *platformStyle;
    CoinControlDialog * coinControlDialog;

    QButtonGroup modeSelection;

    QString m_coldStakeChangeAddress;

    // Process WalletModel::SendCoinsReturn and generate a pair consisting
    // of a message and message flags for use in Q_EMIT message().
    // Additional parameter msgArg can be used via .arg(msgArg).
    void processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn, const QString &msgArg = QString());
    void minimizeFeeSection(bool fMinimize);
    void updateFeeMinimizedLabel();

    // Update the passed in CCoinControl with state from the GUI
    void updateCoinControlState(CCoinControl& ctrl);

    CoinControlDialog::ControlModes GetCoinControlFlag();

    bool getChangeSettings(QString &change_spend, QString &change_stake);

    QString GetFrom();
    QString GetTo();

private Q_SLOTS:
    void on_sendButton_clicked();
    void on_buttonChooseFee_clicked();
    void on_buttonMinimizeFee_clicked();
    void on_btnChangeColdStakingAddress_clicked();
    void removeEntry(SendCoinsEntry* entry);
    void useAvailableBalance(SendCoinsEntry* entry);
    void updateDisplayUnit();
    void coinControlFeatureChanged(bool);
    void coinControlButtonClicked();
    void coinControlChangeChecked(int);
    void coinControlChangeEdited(const QString &);
    void coinControlUpdateLabels();
    void coinControlClipboardQuantity();
    void coinControlClipboardAmount();
    void coinControlClipboardFee();
    void coinControlClipboardAfterFee();
    void coinControlClipboardBytes();
    void coinControlClipboardLowOutput();
    void coinControlClipboardChange();
    void cbxTypeFromChanged(int);
    void setMinimumFee();
    void updateFeeSectionControls();
    void updateMinFeeLabel();
    void updateSmartFeeLabel();
    void modeChanged(int nNewMode);

Q_SIGNALS:
    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);
};


#endif // BITCOIN_QT_STAKINGDIALOG_H
