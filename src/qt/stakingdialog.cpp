// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/stakingdialog.h>
#include <qt/forms/ui_stakingdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/coincontroldialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/receivecoinsdialog.h>
#include <qt/sendcoinsentry.h>
#include <qt/sendcoinsdialog.h>

#include <chainparams.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <wallet/coincontrol.h>
#include <ui_interface.h>
#include <txmempool.h>
#include <policy/fees.h>
#include <shutdown.h>
#include <wallet/fees.h>
#include <wallet/wallet.h>
#include <wallet/hdwallet.h>

#include <univalue.h>
#include <utilmoneystr.h>

#include <QFontMetrics>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QApplication>
#include <QInputDialog>

#define STAKING_UI_UPDATE_MS 5000

extern CAmount AmountFromValue(const UniValue& value);

void AddThousandsSpaces(QString &input)
{
    QChar thin_sp(THIN_SP_CP);

    int seperatorIdx = input.indexOf('.');
    if( seperatorIdx == -1){
        seperatorIdx = input.indexOf(',');
    }

    int q_size = seperatorIdx != -1 ? seperatorIdx : input.size();

    for (int i = 3; i < q_size; i += 3)
        input.insert(q_size - i, thin_sp);
}

void StakingStatusUpdate(QLabel *label, bool fEnabled, bool fActive)
{
    QString strColor, strText;

    if( fEnabled && fActive ){
        strText = "ENABLED";
        strColor = "#2f6b16";
    }else if( fEnabled && !fActive ){
        strText = "INACTIVE";
        strColor = "#d6660a";
    }else{
        strText = "DISABLED";
        strColor = "#9b3209";
    }

    label->setText(strText);
    label->setStyleSheet(QString("QLabel { color : %1 }").arg(strColor));
}

StakingDialog::StakingDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StakingDialog),
    clientModel(0),
    model(0),
    platformStyle(_platformStyle),
    addressPage(nullptr),
    toStealth(nullptr),
    toStake(nullptr),
    activateCold(nullptr)
{
    ui->setupUi(this);

    ui->progressColdStaking->setTextVisible(true);

    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(modeChanged(int)));

    connect(&updateStakingTimer, SIGNAL(timeout()), this, SLOT(updateStakingUI()));
    updateStakingTimer.start(STAKING_UI_UPDATE_MS);
}

void StakingDialog::updateStakingUI()
{
    if( ShutdownRequested() ){
        updateStakingTimer.stop();
        return;
    }

    if( !model ){
        return;
    }

    bool fLocked = model->wallet().isLocked();

    int nDisplayUnit = BitcoinUnits::BTC;
    if (model && model->getOptionsModel())
        nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    bool fAutomatedColdStake;
    QString change_spend, change_stake;
    if( getChangeSettings(change_spend, change_stake) && change_stake != ""){
        fAutomatedColdStake = true;
        ui->btnChangeColdStakingAddress->setText("Disable");
    }else{
        fAutomatedColdStake = false;
        ui->btnChangeColdStakingAddress->setText("Enable");
    }

    ui->lblColdStakingAddress->setText(change_stake);

    ui->lblHotStakingReserve->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, model->wallet().getReserveBalance()));

    ui->lblCurrentHeight->setText(QString::number(chainActive.Height()));
    ui->lblTotalSupply->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, chainActive.Tip()->nMoneySupply));

    UniValue rv;
    QString sCommand = QString("getblockreward %1").arg(chainActive.Tip()->nHeight);
    if (model->tryCallRpc(sCommand, rv)) {

        if (rv["stakereward"].isNum()) {
            ui->lblBlockReward->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, AmountFromValue(rv["stakereward"])));
        }else{
            ui->lblBlockReward->setText("Failed to get reward");
        }

        if (rv["blockreward"].isNum()) {
            ui->lblStakingReward->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, AmountFromValue(rv["blockreward"])));
        }else{
            ui->lblStakingReward->setText("Failed to get reward");
        }
    }

    sCommand = "getcoldstakinginfo";
    if (model->tryCallRpc(sCommand, rv)) {

        bool fColdStakingEnabled = false, fColdStakingActive = false;

        if (rv["enabled"].isBool()) {
            fColdStakingEnabled = rv["enabled"].get_bool();
        }

        if (rv["percent_in_coldstakeable_script"].isNum()) {
            ui->progressColdStaking->setValue(rv["percent_in_coldstakeable_script"].get_real());
        }

        if (rv["coin_in_stakeable_script"].isNum()) {
            ui->lblHotStakingAmount->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, AmountFromValue(rv["coin_in_stakeable_script"])));
        }

        if (rv["coin_in_coldstakeable_script"].isNum()) {
            fColdStakingActive = rv["coin_in_coldstakeable_script"].get_real() > 0;
            fColdStakingEnabled = fColdStakingActive ? true : fColdStakingEnabled;
            ui->lblColdStakingAmount->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, AmountFromValue(rv["coin_in_coldstakeable_script"])));
        }

        bool fShowColdStakingElements = fColdStakingEnabled || fColdStakingActive;

        ui->lblColdStakingAddress->setVisible(fAutomatedColdStake);
        ui->lblColdStakingAddressLabel->setVisible(fAutomatedColdStake);

        ui->coldStakingLowerLine->setVisible(fShowColdStakingElements);
        ui->lblColdStakingAmount->setVisible(fShowColdStakingElements);
        ui->progressColdStaking->setVisible(fShowColdStakingElements);
        ui->lblColdStakingAmountLabel->setVisible(fShowColdStakingElements);
        ui->lblColdStakingPercentLabel->setVisible(fShowColdStakingElements);

        StakingStatusUpdate(ui->lblColdStakingEnabled, fColdStakingEnabled, fColdStakingActive);
    }

    sCommand = "getstakinginfo";
    if (model->tryCallRpc(sCommand, rv)) {

        // Network info

        if (rv["percentyearreward"].isNum()) {
            ui->lblAnnualStakingReward->hide(); //  <- TBD, remove me
            ui->lblAnnualStakingRewardLabel->hide(); //  <- TBD, remove me
            ui->lblMyAnnualStakingReward->hide(); //  <- TBD, remove me
            ui->lblMyAnnualStakingRewardLabel->hide(); //  <- TBD, remove me
            ui->lblAnnualStakingReward->setText(QString::fromStdString(strprintf("%.02f%%", rv["percentyearreward"].get_real())));
        }

        if (rv["difficulty"].isNum()) {
            ui->lblStakingDiff->setText(QString::fromStdString(strprintf("%.08f", rv["difficulty"].get_real())));
        }

        if (rv["netstakeweight"].isNum()) {
            QString strNetWeight = QString("%1").arg(static_cast<int64_t>(rv["netstakeweight"].get_real()));
            AddThousandsSpaces(strNetWeight);
            ui->lblStakingNetWeight->setText(strNetWeight);
        }

        // Local info

        bool fHotStakingEnabled = false, fHotStakingActive = false;
        int64_t nWeight;

        if (rv["enabled"].isBool()) {
            fHotStakingEnabled = rv["enabled"].get_bool();
        }

        if (rv["staking"].isBool()) {
            fHotStakingActive = rv["staking"].get_bool();
        }

        if (rv["weight"].isNum()) {
            nWeight = rv["weight"].get_int64();
            ui->lblHotStakingWalletWeight->setText(BitcoinUnits::format(BitcoinUnits::BTC, nWeight));
        }

        if ( (rv["errors"].isStr() && rv["errors"].get_str() != "") || (!fHotStakingActive && !nWeight) || fLocked ) {

            ui->lblHotStakingErrorLabel->show();
            ui->lblHotStakingError->show();

            QString strError = QString::fromStdString(rv["errors"].get_str());

            if( fLocked ){
                strError = "Your wallet is locked. To start staking unlock the wallet for staking only. To do the unlock you can click on the lock icon in the bottom bar.";
            }else if( strError == "" ){
                strError = "No suitable staking outputs";
            }

            ui->lblHotStakingError->setText(strError);
        }else{
            ui->lblHotStakingErrorLabel->hide();
            ui->lblHotStakingError->hide();
        }

        if (rv["expectedtime"].isNum()) {
            ui->lblHotStakingExpectedTime->setText(GUIUtil::formatNiceTimeOffset(rv["expectedtime"].get_int64()));
        }

        bool fShowHotStakingElements = fLocked ? false : fHotStakingEnabled || fHotStakingActive;

        ui->lblHotStakingAmountLabel->setVisible(fShowHotStakingElements);
        ui->lblHotStakingWalletWeightLabel->setVisible(fShowHotStakingElements);
        ui->lblHotStakingExpectedTimeLabel->setVisible(fShowHotStakingElements);

        ui->lblHotStakingAmount->setVisible(fShowHotStakingElements);
        ui->lblHotStakingWalletWeight->setVisible(fShowHotStakingElements);
        ui->lblHotStakingExpectedTime->setVisible(fShowHotStakingElements);

        StakingStatusUpdate(ui->lblHotStakingEnabled, fHotStakingEnabled, fHotStakingActive);
    }
}

bool StakingDialog::getChangeSettings(QString &change_spend, QString &change_stake)
{
    UniValue rv;
    QString sCommand = "walletsettings changeaddress";
    if (model->tryCallRpc(sCommand, rv)) {
        if (rv["changeaddress"].isObject()
            && rv["changeaddress"]["address_standard"].isStr()) {
            change_spend = QString::fromStdString(rv["changeaddress"]["address_standard"].get_str());
        }
        if (rv["changeaddress"].isObject()
            && rv["changeaddress"]["coldstakingaddress"].isStr()) {
            change_stake = QString::fromStdString(rv["changeaddress"]["coldstakingaddress"].get_str());
        }
        return true;
    }
    return false;
}

void StakingDialog::on_btnChangeReserveBalance_clicked()
{
    bool ok;
    double nNewReserveBalance = QInputDialog::getDouble(this, "Change Reserve Balance" ,
                                                                "New Reserve Balance in BC:",
                                                                0, 0, MAX_MONEY / COIN, 8, &ok);

    if( ok ){
        model->setReserveBalance(nNewReserveBalance * COIN);
    }

    updateStakingUI();
}

void StakingDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if( toStealth ){
        toStealth->setClientModel(_clientModel);
    }

    if( toStake ){
        toStake->setClientModel(_clientModel);
    }

    if( activateCold ){
        activateCold->setClientModel(_clientModel);
    }

}

void StakingDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        updateStakingUI();

        if( addressPage ){
            addressPage->setModel(_model);
        }

        if( toStealth ){
            toStealth->setModel(_model);
        }

        if( toStake ){
            toStake->setModel(_model);
        }

        if( activateCold ){
            activateCold->setModel(_model);
        }

    }
}

void StakingDialog::setPages(ReceiveCoinsDialog *addressPage, SendCoinsDialog *toStealth, SendCoinsDialog *toStake, SendCoinsDialog *activateCold )
{
    if( addressPage && toStealth && toStake && activateCold ){

        this->addressPage = addressPage;
        this->toStealth = toStealth;
        this->toStake = toStake;
        this->activateCold = activateCold;

        ui->tabWidget->insertTab(1, addressPage, "Stake Addresses");
        ui->tabWidget->insertTab(2, toStealth, "Convert to Spending");
        ui->tabWidget->insertTab(3, toStake, "Convert to Staking");
        ui->tabWidget->insertTab(4, activateCold, "Activate ColdStaking");
    }

}

StakingDialog::~StakingDialog()
{
    delete ui;
    delete addressPage;
    delete toStealth;
    delete toStake;
    delete activateCold;
}

void StakingDialog::on_btnChangeColdStakingAddress_clicked()
{

    if( ui->btnChangeColdStakingAddress->text() == "Enable" ){
        bool ok;
        QString newColdStakeChangeAddress = QInputDialog::getText(this, tr("Set Cold Staking Address"),
                                                                  tr("Enter an external cold staking address:"), QLineEdit::Normal,
                                                                  "", &ok);
        if (ok && !newColdStakeChangeAddress.isEmpty()){
            QString sCommand;

            if (newColdStakeChangeAddress != m_coldStakeChangeAddress) {
                QString change_spend, change_stake;
                getChangeSettings(change_spend, m_coldStakeChangeAddress);

                sCommand = "walletsettings changeaddress {";
                if (!change_spend.isEmpty()) {
                    sCommand += "\"address_standard\":\""+change_spend+"\"";
                }
                if (!newColdStakeChangeAddress.isEmpty()) {
                    if (!change_spend.isEmpty()) {
                        sCommand += ",";
                    }
                    sCommand += "\"coldstakingaddress\":\""+newColdStakeChangeAddress+"\"";
                }
                sCommand += "}";
            }

            if (!sCommand.isEmpty()) {
                UniValue rv;
                if (!model->tryCallRpc(sCommand, rv)) {
                    return;
                }
            }
        }
    }else{
        QString change_spend, change_stake;
        getChangeSettings(change_spend, change_stake);

        QString sCommandRemove = "walletsettings changeaddress {}";

        UniValue rv;
        if (!model->tryCallRpc(sCommandRemove, rv)) {
            return;
        }

        QString sCommand = "walletsettings changeaddress {";
        if (!change_spend.isEmpty()) {
            sCommand += "\"address_standard\":\""+change_spend+"\"";
        }
        sCommand += "}";

        if (!change_spend.isEmpty() && !model->tryCallRpc(sCommand, rv)) {
            return;
        }
    }

    updateStakingUI();
}

void StakingDialog::modeChanged(int nNewMode)
{

    if( nNewMode == StakingDialogPages::OVERVIEW ){
        updateStakingUI();
        updateStakingTimer.start(STAKING_UI_UPDATE_MS);
    }else{
        updateStakingTimer.stop();
    }
}
