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
class ReceiveCoinsDialog;
class SendCoinsDialog;
class PlatformStyle;
class UniValue;

namespace Ui {
    class StakingDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;

QT_END_NAMESPACE

/** Dialog for staking management */
class StakingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StakingDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~StakingDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *model);

    void setPages( ReceiveCoinsDialog *addressPage, SendCoinsDialog *conversionPage);

private:

    enum{
        OVERVIEW,
        ADDRESSES,
        TO_SPENDING,
        TO_STAKING,
        TO_COLD_STAKING
    };

    enum StakingDialogPages{
        OVERVIEW_PAGE,
        ADDRESSES_PAGE,
        CONVERSION_PAGE
    };

    Ui::StakingDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;
    const PlatformStyle *platformStyle;
    QTimer updateStakingTimer;

    ReceiveCoinsDialog *addressPage;
    SendCoinsDialog *conversionPage;

    QString m_coldStakeChangeAddress;

    QButtonGroup modeSelection;

    bool getChangeSettings(QString &change_spend, QString &change_stake);

private Q_SLOTS:

    void on_btnChangeColdStakingAddress_clicked();
    void modeChanged(int nNewMode);
    void updateStakingUI();

Q_SIGNALS:
    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);
};


#endif // BITCOIN_QT_STAKINGDIALOG_H
