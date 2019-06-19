// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/stakingdialog.h>
#include <qt/forms/ui_stakingdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/addressbookpage.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/coincontroldialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/sendcoinsentry.h>
#include <qt/sendcoinsdialog.h>
#include <qt/guiconstants.h>

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
#include <pos/miner.h>

#include <univalue.h>
#include <utilmoneystr.h>

#include <QFontMetrics>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QApplication>
#include <QInputDialog>

#define STAKING_UI_UPDATE_MS 10000

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

void StakingStatusUpdate(QLabel *label, bool fEnabled, bool fActive, bool fEnabledInSettings = true)
{
    QString strColor, strText;

    if( fEnabled && fActive ){
        strText = "ENABLED";
        strColor = "#9CD181";
    }else if( fEnabled && !fActive ){
        strText = "INACTIVE";
        strColor = "#F9B94A";
    }else{
        if( !fEnabledInSettings ){
            strText = "STOPPED";
        }else{
            strText = "DISABLED";
        }
        strColor = "#E1755A";
    }

    label->setText(strText);

    label->setStyleSheet(QString("#%1{\
                                    text-transform: uppercase;\
                                    font-family: \"Montserrat\";\
                                    font-size: 16px;\
                                    font-style: bold;\
                                    color : %2; \
                                 }").arg(label->objectName()).arg(strColor));
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

    ui->btnChangeStakingStatus->setVisible(gArgs.GetBoolArg("-staking", true));

    tabButtons.addButton(ui->btnStakingStatus, 0);
    tabButtons.addButton(ui->btnStakingRewards, 1);
    tabButtons.addButton(ui->btnStakingAddresses, 2);
    tabButtons.addButton(ui->btnStakingToSpending, 3);
    tabButtons.addButton(ui->btnStakingToStaking, 4);
    tabButtons.addButton(ui->btnStakingActivateColdStaking, 5);

    connect(&tabButtons, SIGNAL(buttonClicked(int)), this, SLOT(modeChanged(int)));
    connect(&tabButtons, SIGNAL(buttonClicked(int)), ui->stackedWidget, SLOT(setCurrentIndex(int)));

    connect(&updateStakingTimer, SIGNAL(timeout()), this, SLOT(updateStakingUI()));
    updateStakingTimer.start(STAKING_UI_UPDATE_MS);

    ui->stackedWidget->setCurrentIndex(0);
}

StakingDialog::~StakingDialog()
{
    updateStakingTimer.stop();
    delete ui;
    delete transactionPage;
    delete addressPage;
    delete toStealth;
    delete toStake;
    delete activateCold;
}

void StakingDialog::updateStakingUI(bool fForce)
{
    if( ShutdownRequested() ){
        updateStakingTimer.stop();
        return;
    }

    if( !model ){
        return;
    }

    QString strHeight = QString::number(chainActive.Height());
    AddThousandsSpaces(strHeight);
    ui->lblCurrentHeight->setText(strHeight);

    if( QWidget::sender() == clientModel && IsInitialBlockDownload() && !fForce ){
        return;
    }

    bool fLocked = model->wallet().isLocked();

    int nDisplayUnit = BitcoinUnits::BTC;
    if (model && model->getOptionsModel())
        nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    bool fStakingEnabled = false;
    UniValue rv;
    QString sCommand = QString("walletsettings stakingstatus");
    if (model->tryCallRpc(sCommand, rv)) {

        if(rv.isObject()){
            fStakingEnabled = rv["enabled"].getBool();
            if( fStakingEnabled ){
                ui->btnChangeStakingStatus->setText("Stop");
            }else{
                ui->btnChangeStakingStatus->setText("Start");
            }
        }
    }

    ui->lblTotalSupply->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, chainActive.Tip()->nMoneySupply));

    sCommand = QString("getblockreward %1").arg(chainActive.Tip()->nHeight);
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

    sCommand = "getstakinginfo";
    if (model->tryCallRpc(sCommand, rv)) {

        // Network info

        if (rv["percentyearreward"].isNum()) {
            ui->lblAnnualStakingReward->hide(); //  <- TBD, remove me
            ui->lblAnnualText->hide(); //  <- TBD, remove me
            ui->lblMyAnnualStakingReward->hide(); //  <- TBD, remove me
            ui->lblAnnualRewardText->hide(); //  <- TBD, remove me
            ui->lblAnnualStakingReward->setText(QString::fromStdString(strprintf("%.02f%%", rv["percentyearreward"].get_real())));
        }

        if (rv["difficulty"].isNum()) {
            double dDiff = rv["difficulty"].get_real();
            std::string strFormat = dDiff > 1 ? dDiff > 100000 ? "%.00f" : "%.04f" : "%.08f";
            QString strDiff = QString::fromStdString(strprintf(strFormat, dDiff));
            AddThousandsSpaces(strDiff);
            ui->lblStakingDiff->setText(strDiff);
        }

        if (rv["netstakeweight"].isNum()) {
            QString strNetWeight = QString::fromStdString(strprintf("%0.00f", rv["netstakeweight"].get_real()));
            AddThousandsSpaces(strNetWeight);
            ui->lblStakingNetWeight->setText(strNetWeight);
        }

        // Local info

        bool fHotStakingEnabled = false, fHotStakingActive = false;
        CAmount nWeight = 0;
        CAmount nAmountInStakableScript = 0;

        if (rv["enabled"].isBool()) {
            fHotStakingEnabled = rv["enabled"].get_bool();
        }

        if (rv["staking"]["active"].isBool()) {
            fHotStakingActive = rv["staking"]["active"].get_bool();
        }

        if (rv["amount_in_stakeable_script"].isNum()) {
            nAmountInStakableScript = AmountFromValue(rv["amount_in_stakeable_script"]);
            ui->lblHotStakingAmount->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nAmountInStakableScript));
        }

        if (rv["weight"].isNum()) {
            nWeight = rv["weight"].get_int64();
            QString strWeight = QString::fromStdString(strprintf("%d", nWeight / COIN));
            AddThousandsSpaces(strWeight);
            ui->lblHotStakingWalletWeight->setText(strWeight);
        }

        if (rv["estimated_rewardfrequency"].isNum()) {
//            ui->lblHotStakingExpectedTime->setText(GUIUtil::formatNiceTimeOffset(rv["estimated_rewardfrequency"].get_int64()));
            ui->lblHotStakingExpectedTime->setText(QString("%1 to %2").arg(GUIUtil::formatNiceTimeOffset(rv["estimated_rewardfrequency"].get_int64()/3)).arg(GUIUtil::formatNiceTimeOffset(rv["estimated_rewardfrequency"].get_int64()*3)));
        }

        ui->lblHotStakingError->hide();

        QString strNotStakingReason;

        if( !fHotStakingActive && !nWeight ){
            if( fHotStakingEnabled && !nAmountInStakableScript ){
                strNotStakingReason = tr("No stakable funds available. Use the \"Convert to staking\" tab to convert funds for staking.");
            }else{
                strNotStakingReason = tr("No eligible staking outputs available. Staking funds need to have 225 confirmations to be eligible.");
            }
        }

        if( rv["staking"].get_obj().exists("reason") ){

            CHDWallet::eStakingState stakingState = static_cast<CHDWallet::eStakingState>(rv["staking"]["reason"].get_int64());
            switch (stakingState) {
                case CHDWallet::NOT_STAKING_INIT:
                    strNotStakingReason = tr("Staking not initialized yet. Wait a moment.");
                    break;
                case CHDWallet::NOT_STAKING_STOPPED:
                    strNotStakingReason = tr("Staking is stopped. Press the start button above to enable staking.");
                    break;
                case CHDWallet::NOT_STAKING_BALANCE:
                    strNotStakingReason = tr("No stakable funds available. Use the \"Convert to staking\" tab to convert funds for staking.");
                    break;
                case CHDWallet::NOT_STAKING_DEPTH:
                    strNotStakingReason = tr("No eligible staking outputs available. Staking funds need to have 225 confirmations to be eligible.");
                    break;
                case CHDWallet::NOT_STAKING_LOCKED:
                    if( fLocked ){
                        strNotStakingReason = tr("Wallet is locked. To start staking unlock the wallet for staking only. To unlock click the lock icon in the bottom right hand portion of the window.");
                    }
                    break;
                case CHDWallet::NOT_STAKING_NOT_SYCNED:
                    strNotStakingReason = tr("Wallet is not fully synced. To start staking make sure the wallet has connections to the network and wait until it catched up with the latest blocks.");
                    break;
                case CHDWallet::NOT_STAKING_DISABLED:
                    strNotStakingReason = tr("Staking is disabled. Set the config paramter \"staking=1\" in the bitcoinc.conf to enable it.");
                    break;
                default:
                    break;
            }

        }

        if( !strNotStakingReason.isEmpty() ){
            ui->lblHotStakingError->show();
            ui->lblHotStakingError->setText(strNotStakingReason);
        }

        if ( rv["errors"].isStr() && rv["errors"].get_str() != "" )  {

            QString strError = QString::fromStdString(rv["errors"].get_str());

            ui->lblHotStakingError->show();
            ui->lblHotStakingError->setText(strError);
        }

        bool fShowHotStakingElements = strNotStakingReason.isEmpty();

        ui->lblHotStakingAmountLabel->setVisible(fShowHotStakingElements);
        ui->lblHotStakingWalletWeightLabel->setVisible(fShowHotStakingElements);
        ui->lblHotStakingExpectedTimeLabel->setVisible(fShowHotStakingElements);

        ui->lblHotStakingAmount->setVisible(fShowHotStakingElements);
        ui->lblHotStakingWalletWeight->setVisible(fShowHotStakingElements);
        ui->lblHotStakingExpectedTime->setVisible(fShowHotStakingElements);

        StakingStatusUpdate(ui->lblHotStakingEnabled, fHotStakingEnabled, fHotStakingActive, fStakingEnabled);

        bool fColdStakingActive = false;
        std::string strColdStakeChange = "";
        bool fShowColdStakingWeight = false;
        CAmount nAmountInColdStakableScript = 0;

        UniValue objCold = rv["coldstaking"].get_obj();

        if (objCold["active"].isBool()) {
            fColdStakingActive = objCold["active"].get_bool();
        }

        if (objCold["amount_in_coldstakeable_script"].isNum()) {
            nAmountInColdStakableScript = AmountFromValue(objCold["amount_in_coldstakeable_script"]);
            ui->lblColdStakingAmount->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nAmountInColdStakableScript));
        }

        if (objCold["percent_in_coldstakeable_script"].isNum()) {
            ui->progressColdStaking->setValue(objCold["percent_in_coldstakeable_script"].get_real());
        }

        if (objCold["coldstake_change_address"].isStr()) {
            strColdStakeChange = objCold["coldstake_change_address"].get_str();
        }

        if (objCold["estimated_weight"].isNum()) {
            double dWeight = objCold["estimated_weight"].get_real();
            fShowColdStakingWeight = dWeight > 0;
            QString strWeight = QString::fromStdString(strprintf("%0.00f", dWeight));
            AddThousandsSpaces(strWeight);
            ui->lblColdStakingWeight->setText(strWeight);
        }

        if (objCold["estimated_rewardfrequency"].isNum()) {
//            ui->lblColdStakingFrequency->setText(GUIUtil::formatNiceTimeOffset(objCold["estimated_rewardfrequency"].get_int64()));
            ui->lblColdStakingFrequency->setText(QString("%1 to %2").arg(GUIUtil::formatNiceTimeOffset(objCold["estimated_rewardfrequency"].get_int64()/3)).arg(GUIUtil::formatNiceTimeOffset(objCold["estimated_rewardfrequency"].get_int64()*3)));
        }

        if ( nAmountInColdStakableScript && !fShowColdStakingWeight ) {
            ui->lblColdStakingError->show();
            ui->lblColdStakingError->setText(tr("No eligible coldstaking outputs available. Your coldstaking funds need to have 225 confirmations to be eligible."));
        }else{
            ui->lblColdStakingError->hide();
        }

        bool fAutomatedColdStake;

        if( strColdStakeChange != ""){
            fAutomatedColdStake = true;
            ui->btnChangeColdStakingAddress->setText("Disable");
        }else{
            fAutomatedColdStake = false;
            ui->btnChangeColdStakingAddress->setText("Enable");
        }

        ui->lblColdStakingAddress->setText(QString::fromStdString(strColdStakeChange));

        ui->lblColdStakingAddressLabel->setVisible(fAutomatedColdStake);
        ui->lblColdStakingAmountLabel->setVisible(fColdStakingActive);
        ui->lblColdStakingPercentLabel->setVisible(fColdStakingActive);
        ui->lblColdStakingWeightLabel->setVisible(fShowColdStakingWeight);
        ui->lblColdStakingFrequencyLabel->setVisible(fShowColdStakingWeight);

        ui->lblColdStakingAddress->setVisible(fAutomatedColdStake);
        ui->lblColdStakingAmount->setVisible(fColdStakingActive);
        ui->progressColdStaking->setVisible(fColdStakingActive);
        ui->lblColdStakingWeight->setVisible(fShowColdStakingWeight);
        ui->lblColdStakingFrequency->setVisible(fShowColdStakingWeight);

        StakingStatusUpdate(ui->lblColdStakingEnabled, fColdStakingActive, fColdStakingActive);
    }
}

void StakingDialog::updateStakingRewards(const QString& strCountVisible, const QString& strAmountVisible, const QString& strCount, const QString& strAmount)
{
    ui->lblBlocksSelected->setText(strCountVisible);
    ui->lblRewardSelected->setText(strAmountVisible);
    ui->lblBlocksTotal->setText(strCount);
    ui->lblRewardTotal->setText(strAmount);
}

void StakingDialog::updateEncryptionStatus()
{
    updateStakingUI(true);
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

void StakingDialog::on_btnChangeStakingStatus_clicked()
{
    UniValue rv;
    QString sCommand = QString("walletsettings stakingstatus");
    if (model->tryCallRpc(sCommand, rv)) {

        if(rv.isObject()){
            if( rv["enabled"].getBool() ){
                sCommand += " false";
            }else{
                sCommand += " true";
            }
        }
    }

    model->tryCallRpc(sCommand, rv);

    updateStakingUI();
}

void StakingDialog::setClientModel(ClientModel *_clientModel)
{
    if( !clientModel ){
        return;
    }

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

    connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateStakingUI()));
}

void StakingDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        updateStakingUI(true);

        if( addressPage ){
            addressPage->setModel(_model->getStakingAddressTableModel());
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

void StakingDialog::setPages(QWidget *transactionPage, AddressBookPage *addressPage, SendCoinsDialog *toStealth, SendCoinsDialog *toStake, SendCoinsDialog *activateCold )
{
    if( transactionPage && addressPage && toStealth && toStake && activateCold ){

        this->transactionPage = transactionPage;
        this->addressPage = addressPage;
        this->toStealth = toStealth;
        this->toStake = toStake;
        this->activateCold = activateCold;

        ui->rewardsTab->layout()->addWidget(transactionPage);
        ui->stackedWidget->insertWidget(2, addressPage);
        ui->stackedWidget->insertWidget(3, toStealth);
        ui->stackedWidget->insertWidget(4, toStake);
        ui->stackedWidget->insertWidget(5, activateCold);
    }

}

void StakingDialog::on_btnChangeColdStakingAddress_clicked()
{
    bool fEnabled = false;
    QString newColdStakeChangeAddress;

    if( ui->btnChangeColdStakingAddress->text() == "Enable" ){

        bool ok;
        newColdStakeChangeAddress = QInputDialog::getText(this, tr("Set Remote Staking Address"),
                                                                  tr("Enter the remote staking address:"), QLineEdit::Normal,
                                                                  "", &ok);
        if (ok && !newColdStakeChangeAddress.isEmpty()){
            QString sCommand;

            QString change_spend, change_stake;
            getChangeSettings(change_spend, change_stake);

            sCommand = "walletsettings changeaddress {";
            if (!change_spend.isEmpty()) {
                sCommand += "\"address_standard\":\""+change_spend+"\"";
            }

            if (!change_spend.isEmpty()) {
                sCommand += ",";
            }
            sCommand += "\"coldstakingaddress\":\""+newColdStakeChangeAddress+"\"}";

            fEnabled = true;

            if (!sCommand.isEmpty()) {
                UniValue rv;
                if (!model->tryCallRpc(sCommand, rv)) {
                    fEnabled = false;
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

    if( fEnabled ){
        UniValue rv;
        model->tryCallRpc("walletsettings stakingstatus true", rv);
    }

    updateStakingUI();
}

void StakingDialog::modeChanged(int nNewMode)
{
    updateStakingTimer.stop();

    switch(nNewMode){
    case OVERVIEW:
        updateStakingUI();
        updateStakingTimer.start(STAKING_UI_UPDATE_MS);
        break;
    default:
        break;
    }
}
