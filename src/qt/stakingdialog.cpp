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

#include <QFontMetrics>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QApplication>
#include <QInputDialog>

#define STAKING_UI_UPDATE_MS 5000

StakingDialog::StakingDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StakingDialog),
    clientModel(0),
    model(0),
    platformStyle(_platformStyle),
    addressPage(nullptr),
    conversionPage(nullptr)
{
    ui->setupUi(this);

    modeSelection.addButton(ui->radioOverview, 0);
    modeSelection.addButton(ui->radioAddresses, 1);
    modeSelection.addButton(ui->radioSpending, 2);
    modeSelection.addButton(ui->radioStaking, 3);
    modeSelection.addButton(ui->radioColdStaking, 4);

    connect(&modeSelection, SIGNAL(buttonClicked(int)), this, SLOT(modeChanged(int)));

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

    ui->lblStakingWallet->setText(QString::fromStdString(model->wallet().getWalletName()));

    QString change_spend;
    getChangeSettings(change_spend, m_coldStakeChangeAddress);
    ui->lblColdStakingAddress->setText(m_coldStakeChangeAddress);

    UniValue rv;
    QString sCommand = "getcoldstakinginfo";
    if (model->tryCallRpc(sCommand, rv)) {

        bool fColdStakingEnabled = false;

        if (rv["enabled"].isBool()) {
            fColdStakingEnabled = rv["enabled"].get_bool();

            if( fColdStakingEnabled ){
               ui->lblColdStakingEnabled->setText("ENABLED");

               if (rv["percent_in_coldstakeable_script"].isNum()) {
                   ui->lblColdStakingPercent->setText(QString::fromStdString(strprintf("%.02f", rv["percent_in_coldstakeable_script"].get_real())));
               }

               if (rv["coin_in_coldstakeable_script"].isNum()) {
                   ui->lblColdStakingCoinInScript->setText(QString::fromStdString(strprintf("%.02f", rv["coin_in_coldstakeable_script"].get_real())));
               }

            }else{
                ui->lblColdStakingEnabled->setText("DISABLED");
            }

        }

        ui->lblColdStakingAddress->setVisible(fColdStakingEnabled);
        ui->lblColdStakingPercent->setVisible(fColdStakingEnabled);
        ui->lblColdStakingCoinInScript->setVisible(fColdStakingEnabled);

        ui->lblColdStakingAddressLabel->setVisible(fColdStakingEnabled);
        ui->lblColdStakingPercentLabel->setVisible(fColdStakingEnabled);
        ui->lblColdStakingCoinInScriptLabel->setVisible(fColdStakingEnabled);

    }

    sCommand = "getstakinginfo";
    if (model->tryCallRpc(sCommand, rv)) {

        bool fHotStaking = false;

        if (rv["enabled"].isBool()) {
            fHotStaking = rv["enabled"].get_bool();

            if( fHotStaking ){
                ui->lblHotStakingEnabled->setText("ENABLED");

                if (rv["staking"].isBool()) {
                    ui->lblHotStakingActive->setText(rv["staking"].get_bool() ? "True" : "False");
                }

                if (rv["errors"].isStr() && rv["errors"].get_str() != "") {

                    ui->lblHotStakingErrorLabel->show();
                    ui->lblHotStakingError->show();

                    ui->lblHotStakingError->setText(QString::fromStdString(rv["errors"].get_str()));
                }else{
                    ui->lblHotStakingErrorLabel->hide();
                    ui->lblHotStakingError->hide();
                }

                if (rv["weight"].isNum()) {
                    ui->lblHotStakingWalletWeight->setText(QString::fromStdString(strprintf("%d", rv["weight"].get_int64())));
                }

                if (rv["expectedtime"].isNum()) {
                    ui->lblHotStakingExpectedTime->setText(QString::fromStdString(strprintf("%d", rv["expectedtime"].get_int64())));
                }

            }else{
                ui->lblHotStakingEnabled->setText("DISABLED");

            }

            ui->lblHotStakingActiveLabel->setVisible(fHotStaking);
            ui->lblHotStakingErrorLabel->setVisible(fHotStaking);
            ui->lblHotStakingWalletWeightLabel->setVisible(fHotStaking);
            ui->lblHotStakingExpectedTimeLabel->setVisible(fHotStaking);

            ui->lblHotStakingActive->setVisible(fHotStaking);
            ui->lblHotStakingError->setVisible(fHotStaking);
            ui->lblHotStakingWalletWeight->setVisible(fHotStaking);
            ui->lblHotStakingExpectedTime->setVisible(fHotStaking);

        }

        if (rv["percentyearreward"].isNum()) {
            ui->lblStakingReward->setText(QString::fromStdString(strprintf("%.02f%%", rv["percentyearreward"].get_real())));
        }

        if (rv["difficulty"].isNum()) {
            ui->lblStakingDiff->setText(QString::fromStdString(strprintf("%.02f", rv["difficulty"].get_real())));
        }

        if (rv["netstakeweight"].isNum()) {
            ui->lblStakingNetWeight->setText(QString::fromStdString(strprintf("%d", rv["netstakeweight"].get_int64())));
        }

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

void StakingDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if( conversionPage ){
        conversionPage->setClientModel(_clientModel);
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

        if( conversionPage ){
            conversionPage->setModel(_model);
        }
    }
}

void StakingDialog::setPages(ReceiveCoinsDialog *addressPage, SendCoinsDialog *conversionPage)
{
    if( addressPage && conversionPage ){

        this->addressPage = addressPage;
        this->conversionPage = conversionPage;

        ui->stackedWidget->insertWidget(1, addressPage);
        ui->stackedWidget->insertWidget(2, conversionPage);
    }

}

StakingDialog::~StakingDialog()
{
    delete ui;
    delete addressPage;
    delete conversionPage;
}

void StakingDialog::on_btnChangeColdStakingAddress_clicked()
{
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
}


void StakingDialog::modeChanged(int nNewMode)
{
    StakingDialogPages nNewIndex;

    switch(nNewMode){
    case OVERVIEW:
        updateStakingTimer.start();
        nNewIndex = StakingDialogPages::OVERVIEW_PAGE;
        break;
    case ADDRESSES:
        nNewIndex = StakingDialogPages::ADDRESSES_PAGE;
        break;
    case TO_SPENDING:
        conversionPage->setMode(CoinControlDialog::CONVERT_TO_SPENDING);
        nNewIndex = StakingDialogPages::CONVERSION_PAGE;
        break;
    case TO_STAKING:
        conversionPage->setMode(CoinControlDialog::CONVERT_TO_STAKING);
        nNewIndex = StakingDialogPages::CONVERSION_PAGE;
        break;
    case TO_COLD_STAKING:
        conversionPage->setMode(CoinControlDialog::CONVERT_TO_COLD_STAKE);
        nNewIndex = StakingDialogPages::CONVERSION_PAGE;
        break;
    }

    if( nNewIndex == StakingDialogPages::OVERVIEW_PAGE ){
        updateStakingUI();
        updateStakingTimer.start(STAKING_UI_UPDATE_MS);
    }else{
        updateStakingTimer.stop();
    }

    ui->stackedWidget->setCurrentIndex(nNewIndex);
}
