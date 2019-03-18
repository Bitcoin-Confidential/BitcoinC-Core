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
#include <qt/sendcoinsentry.h>
#include <qt/sendcoinsdialog.h>


#include <chainparams.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <wallet/coincontrol.h>
#include <ui_interface.h>
#include <txmempool.h>
#include <policy/fees.h>
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

StakingDialog::StakingDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StakingDialog),
    clientModel(0),
    model(0),
    fNewRecipientAllowed(true),
    fFeeMinimized(true),
    platformStyle(_platformStyle),
    coinControlDialog(nullptr)
{
    ui->setupUi(this);

    if (!_platformStyle->getImagesOnButtons()) {
        ui->clearButton->setIcon(QIcon());
        ui->sendButton->setIcon(QIcon());
    } else {
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->sendButton->setIcon(_platformStyle->SingleColorIcon(":/icons/send"));
    }

    GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);

    addEntry();

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // Coin Control
    connect(ui->pushButtonCoinControl, SIGNAL(clicked()), this, SLOT(coinControlButtonClicked()));
    connect(ui->checkBoxCoinControlChange, SIGNAL(stateChanged(int)), this, SLOT(coinControlChangeChecked(int)));
    connect(ui->lineEditCoinControlChange, SIGNAL(textEdited(const QString &)), this, SLOT(coinControlChangeEdited(const QString &)));

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardBytes()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardChange()));

    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

    // init transaction fee section
    QSettings settings;
    if (!settings.contains("fFeeSectionMinimized"))
        settings.setValue("fFeeSectionMinimized", true);
    if (!settings.contains("nFeeRadio") && settings.contains("nTransactionFee") && settings.value("nTransactionFee").toLongLong() > 0) // compatibility
        settings.setValue("nFeeRadio", 1); // custom
    if (!settings.contains("nFeeRadio"))
        settings.setValue("nFeeRadio", 0); // recommended
    if (!settings.contains("nSmartFeeSliderPosition"))
        settings.setValue("nSmartFeeSliderPosition", 0);
    if (!settings.contains("nTransactionFee"))
        settings.setValue("nTransactionFee", (qint64)DEFAULT_PAY_TX_FEE);
    if (!settings.contains("fPayOnlyMinFee"))
        settings.setValue("fPayOnlyMinFee", false);
    ui->groupFee->setId(ui->radioSmartFee, 0);
    ui->groupFee->setId(ui->radioCustomFee, 1);
    ui->groupFee->button((int)std::max(0, std::min(1, settings.value("nFeeRadio").toInt())))->setChecked(true);
    ui->customFee->setValue(settings.value("nTransactionFee").toLongLong());
    ui->checkBoxMinimumFee->setChecked(settings.value("fPayOnlyMinFee").toBool());
    minimizeFeeSection(settings.value("fFeeSectionMinimized").toBool());

    modeSelection.addButton(ui->radioOverview, 0);
    modeSelection.addButton(ui->radioSpending, 1);
    modeSelection.addButton(ui->radioStaking, 2);
    modeSelection.addButton(ui->radioColdStaking, 3);

    connect(&modeSelection, SIGNAL(buttonClicked(int)), this, SLOT(modeChanged(int)));

    modeChanged(0);
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

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }
}

void StakingDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {

        for(int i = 0; i < ui->entries->count(); ++i)
        {
            SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
            if(entry)
            {
                entry->setModel(_model);
            }
        }

        interfaces::WalletBalances balances = _model->wallet().getBalances();
        setBalance(balances);
        connect(_model, SIGNAL(balanceChanged(interfaces::WalletBalances)), this, SLOT(setBalance(interfaces::WalletBalances)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Coin Control
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(_model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(coinControlFeatureChanged(bool)));
        ui->frameCoinControl->setVisible(_model->getOptionsModel()->getCoinControlFeatures() && modeSelection.checkedId() > 0);
        coinControlUpdateLabels();

        // fee section
        for (const int n : confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(GUIUtil::formatNiceTimeOffset(n*Params().GetConsensus().nPowTargetSpacing)).arg(n));
        }
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(updateSmartFeeLabel()));
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(coinControlUpdateLabels()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->optInRBF, SIGNAL(stateChanged(int)), this, SLOT(updateSmartFeeLabel()));
        connect(ui->optInRBF, SIGNAL(stateChanged(int)), this, SLOT(coinControlUpdateLabels()));
        ui->customFee->setSingleStep(model->wallet().getRequiredFee(1000));
        updateFeeSectionControls();
        updateMinFeeLabel();
        updateSmartFeeLabel();

        // set default rbf checkbox state
        ui->optInRBF->setCheckState(Qt::Checked);

        // set the smartfee-sliders default value (wallets default conf.target or last stored value)
        QSettings settings;
        if (settings.value("nSmartFeeSliderPosition").toInt() != 0) {
            // migrate nSmartFeeSliderPosition to nConfTarget
            // nConfTarget is available since 0.15 (replaced nSmartFeeSliderPosition)
            int nConfirmTarget = 25 - settings.value("nSmartFeeSliderPosition").toInt(); // 25 == old slider range
            settings.setValue("nConfTarget", nConfirmTarget);
            settings.remove("nSmartFeeSliderPosition");
        }
        if (settings.value("nConfTarget").toInt() == 0)
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(model->wallet().getConfirmTarget()));
        else
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(settings.value("nConfTarget").toInt()));

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
}

StakingDialog::~StakingDialog()
{
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    settings.setValue("nTransactionFee", (qint64)ui->customFee->value());
    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());

    delete ui;
}

void StakingDialog::on_sendButton_clicked()
{
    if(!model || !model->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            if(entry->validate(model->node()))
            {
                recipients.append(entry->getValue());
            }
            else
            {
                valid = false;
            }
        }
    }

    if(!valid || recipients.isEmpty())
    {
        return;
    }

    fNewRecipientAllowed = false;
    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        fNewRecipientAllowed = true;
        return;
    }


    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients);
    WalletModel::SendCoinsReturn prepareStatus;

    // Always use a CCoinControl instance, use the CoinControlDialog instance if CoinControl has been enabled
    CCoinControl ctrl;
    if (model->getOptionsModel()->getCoinControlFeatures())
        ctrl = *CoinControlDialog::coinControl(GetCoinControlFlag());

    updateCoinControlState(ctrl);

    prepareStatus = model->prepareTransaction(currentTransaction, ctrl);
    if (prepareStatus.status != WalletModel::OK)
    {
        // process prepareStatus and on error generate message shown to user
        processSendCoinsReturn(prepareStatus.status, BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), currentTransaction.getTransactionFee()));

        fNewRecipientAllowed = true;
        return;
    };


    QString sCommand = "sendtypeto ";

    // TODO: Translations?
    QString sTypeFrom = GetFrom();
    QString sTypeTo = GetTo();

    sCommand += sTypeFrom.toLower() + " ";
    sCommand += sTypeTo.toLower();

    sCommand += " [";

    int nRecipient = 0;
    for (const auto &rcp : currentTransaction.getRecipients()) {
        if (nRecipient > 0)
            sCommand += ",";

        if (rcp.m_coldstake) {
            QString build_script = "buildscript {\"recipe\":\"ifcoinstake\",\"addrstake\":\""
                + rcp.stake_address + "\",\"addrspend\":\"" + rcp.spend_address + "\"}";
            UniValue rv;
            if (!model->tryCallRpc(build_script, rv)) {
                return;
            }

            sCommand += "{\"address\":\"script\"";
            sCommand += ",\"script\":\"" + QString::fromStdString(rv["hex"].get_str()) + "\"";
        } else {
            sCommand += "{\"address\":\"" + rcp.address + "\"";
        }
        sCommand += ",\"amount\":"
            + BitcoinUnits::format(BitcoinUnits::BTC, rcp.amount, false, BitcoinUnits::separatorNever);

        if (rcp.fSubtractFeeFromAmount)
            sCommand += ",\"subfee\":true";

        if (!rcp.narration.isEmpty())
            sCommand += ",\"narr\":\""+rcp.narration+"\"";
        sCommand += "}";

        nRecipient++;
    }

    int nRingSize = 4;
    int nMaxInputs = 32;

    sCommand += "] \"\" \"\" "+QString::number(nRingSize)+" "+QString::number(nMaxInputs);

    QString sCoinControl;
    sCoinControl += " {";
    sCoinControl += "\"replaceable\":" + QString::fromUtf8((ui->optInRBF->isChecked() ? "true" : "false"));

    if (ctrl.m_feerate) {
        sCoinControl += ",\"feeRate\":" + QString::fromStdString(ctrl.m_feerate->ToString(false));
    } else {
        std::string sFeeMode;
        if (StringFromFeeMode(ctrl.m_fee_mode, sFeeMode))
            sCoinControl += ",\"estimate_mode\":\"" + QString::fromStdString(sFeeMode) +"\"";
        if (ctrl.m_confirm_target)
            sCoinControl += ",\"conf_target\":" + QString::number(*ctrl.m_confirm_target);
    }

    if (!boost::get<CNoDestination>(&ctrl.destChange)) {
        CBitcoinAddress addrChange(ctrl.destChange);
        sCoinControl += ",\"changeaddress\":\""+QString::fromStdString(addrChange.ToString())+"\"";
    }

    if (ctrl.NumSelected() > 0)  {
        sCoinControl += ",\"inputs\":[";
        bool fNeedCommaInputs = false;
        for (const auto &op : ctrl.setSelected) {
            sCoinControl += fNeedCommaInputs ? ",{" : "{";
            sCoinControl += "\"tx\":\"" + QString::fromStdString(op.hash.ToString()) + "\"";
            sCoinControl += ",\"n\":" + QString::number(op.n);
            sCoinControl += "}";
            fNeedCommaInputs = true;
        }
        sCoinControl += "]";
    }
    sCoinControl += "} ";

    UniValue rv;
    QString sGetFeeCommand = sCommand + " true" + sCoinControl;
    if (!model->tryCallRpc(sGetFeeCommand, rv)) {
        return;
    }

    double rFee = rv["fee"].get_real();

    bool fSubbedFee = rv["outputs_fee"].size() > 0 ? true : false;

    size_t nBytes = rv["bytes"].get_int64();
    bool fNeedHWDevice = rv["need_hwdevice"].get_bool();

    CAmount txFee = rFee * COIN;

    // Format confirmation message
    QStringList formatted;
    for (const auto &rcp : currentTransaction.getRecipients())
    {
        // generate bold amount string
        CAmount nValue = rcp.amount;

        const UniValue &uv = rv["outputs_fee"][rcp.address.toStdString().c_str()];
        if (uv.isNum())
            nValue = uv.get_int64();

        // generate bold amount string with wallet name in case of multiwallet
        QString amount = "<b>" + BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), nValue);
        if (model->isMultiwallet()) {
            amount.append(" <u>"+tr("from wallet %1").arg(GUIUtil::HtmlEscape(model->getWalletName()))+"</u> ");
        }
        amount.append("</b>");
        // generate monospace address string
        QString address;
        if (rcp.m_coldstake) {
            address = "<span style='font-family: monospace;'>Spend: " + rcp.spend_address;
            address.append("<br/>Stake: " + rcp.stake_address);
            address.append("</span>");
        } else {
            address = "<span style='font-family: monospace;'>" + rcp.address;
            address.append("</span>");
        }

        QString recipientElement;
        recipientElement = "<br />";

        if (!rcp.paymentRequest.IsInitialized()) // normal payment
        {
            if(rcp.label.length() > 0) // label with address
            {
                recipientElement.append(tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.label)));
                recipientElement.append(QString(" (%1)").arg(address));
            }
            else // just address
            {
                recipientElement.append(tr("%1 to %2").arg(amount, address));
            }
        }
        else if(!rcp.authenticatedMerchant.isEmpty()) // authenticated payment request
        {
            recipientElement.append(tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.authenticatedMerchant)));
        }
        else // unauthenticated payment request
        {
            recipientElement.append(tr("%1 to %2").arg(amount, address));
        }

        formatted.append(recipientElement);
    }

    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><span style='font-size:10pt;'>");
    questionString.append(tr("Please, review your transaction."));
    questionString.append("</span><br /><b>"+sTypeFrom+ "</b> to <b>" +sTypeTo+"</b><br />%1");

    if(txFee > 0)
    {
        // append fee string if a fee is required
        questionString.append("<hr /><b>");
        questionString.append(tr("Estimated Transaction fee"));
        questionString.append("</b>");

        // append transaction size
        //questionString.append(" (" + QString::number((double)currentTransaction.getTransactionSize() / 1000) + " kB): ");
        questionString.append(" (" + QString::number((double)nBytes / 1000) + " kB): ");

        // append transaction fee value
        questionString.append("<span style='color:#aa0000; font-weight:bold;'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span><br />");

        if (fSubbedFee)
            questionString.append(tr("removed for transaction fee"));
        else
            questionString.append(tr("added as transaction fee"));

        // append RBF message according to transaction's signalling
        questionString.append("<br /><span style='font-size:10pt; font-weight:normal;'>");
        if (ui->optInRBF->isChecked()) {
            questionString.append(tr("You can increase the fee later (signals Replace-By-Fee, BIP-125)."));
        } else {
            questionString.append(tr("Not signalling Replace-By-Fee, BIP-125."));
        }
        questionString.append("</span>");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");

    CAmount totalAmount = currentTransaction.getTotalTransactionAmount();
    if (!fSubbedFee)
        totalAmount += txFee;

    QStringList alternativeUnits;
    for (BitcoinUnits::Unit u : BitcoinUnits::availableUnits())
    {
        if(u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(BitcoinUnits::formatHtmlWithUnit(u, totalAmount));
    }
    questionString.append(QString("<b>%1</b>: <b>%2</b>").arg(tr("Total Amount"))
        .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount)));
    questionString.append(QString("<br /><span style='font-size:10pt; font-weight:normal;'>(=%1)</span>")
        .arg(alternativeUnits.join(" " + tr("or") + " ")));

    if (fNeedHWDevice)
    {
        questionString.append("<hr /><span><b>");
        questionString.append(tr("Your hardware device must be connected to sign this txn."));
        questionString.append("</b></span>");
    }

    SendConfirmationDialog confirmationDialog(tr("Confirm send coins"),
        questionString.arg(formatted.join("<br />")), SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = static_cast<QMessageBox::StandardButton>(confirmationDialog.result());

    if(retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }


    WalletModel::SendCoinsReturn sendStatus = WalletModel::OK;

    sCommand += " false";
    sCommand += sCoinControl;


    uint256 hashSent;
    if (!model->tryCallRpc(sCommand, rv)) {
        sendStatus = WalletModel::TransactionCreationFailed;
    } else {
        hashSent.SetHex(rv.get_str());
    }

    // Update Addressbook
    for (const auto &rcp : currentTransaction.getRecipients()) {
        if (rcp.m_coldstake) {
            continue;
        }
        sCommand = "manageaddressbook newsend ";
        sCommand += rcp.address;
        QString strLabel = rcp.label;
        sCommand += strLabel.isEmpty() ? " \"\"" : (" \"" + strLabel + "\"");
        sCommand += " send";

        model->tryCallRpc(sCommand, rv);
    }

    processSendCoinsReturn(sendStatus);

    if (sendStatus.status == WalletModel::OK) {
        accept();
        CoinControlDialog::coinControl(GetCoinControlFlag())->UnSelectAll();
        coinControlUpdateLabels();
        //Q_EMIT coinsSent(currentTransaction.getWtx()->get().GetHash());
        Q_EMIT coinsSent(hashSent);
    }
    fNewRecipientAllowed = true;
}

void StakingDialog::clear()
{
    // Clear coin control settings
    CoinControlDialog::coinControl(GetCoinControlFlag())->UnSelectAll();
    ui->checkBoxCoinControlChange->setChecked(false);
    ui->lineEditCoinControlChange->clear();
    coinControlUpdateLabels();

    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }

    updateTabsAndLabels();
}

void StakingDialog::reject()
{
    clear();
    addEntry();
}

void StakingDialog::accept()
{
    clear();
    addEntry();
}

SendCoinsEntry *StakingDialog::addEntry()
{
    SendCoinsEntry *entry = new SendCoinsEntry(platformStyle, this, GetCoinControlFlag() < CoinControlDialog::CONVERT_TO_STAKING );
    entry->hideDeleteButton();
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));
    connect(entry, SIGNAL(useAvailableBalance(SendCoinsEntry*)), this, SLOT(useAvailableBalance(SendCoinsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    connect(entry, SIGNAL(subtractFeeFromAmountChanged()), this, SLOT(coinControlUpdateLabels()));

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());

    updateTabsAndLabels();
    return entry;
}

SendCoinsEntry *StakingDialog::addEntryCS()
{
    if (ui->entries->count() == 1) {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if (entry->isClear() && !entry->m_coldstake) {
            ui->entries->takeAt(0)->widget()->deleteLater();
        }
    }

    SendCoinsEntry *entry = new SendCoinsEntry(platformStyle, this, false, true);
    entry->hideDeleteButton();
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));
    connect(entry, SIGNAL(useAvailableBalance(SendCoinsEntry*)), this, SLOT(useAvailableBalance(SendCoinsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    connect(entry, SIGNAL(subtractFeeFromAmountChanged()), this, SLOT(coinControlUpdateLabels()));

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());

    updateTabsAndLabels();
    return entry;
}

void StakingDialog::updateTabsAndLabels()
{
    setupTabChain(0);
    coinControlUpdateLabels();
}

void StakingDialog::removeEntry(SendCoinsEntry* entry)
{
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1)
        addEntry();

    entry->deleteLater();

    updateTabsAndLabels();
}

QWidget *StakingDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->sendButton);
    QWidget::setTabOrder(ui->sendButton, ui->clearButton);
    return ui->clearButton;
}

void StakingDialog::setAddress(const QString &address)
{
    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setAddress(address);
}

void StakingDialog::pasteEntry(const SendCoinsRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setValue(rv);
    updateTabsAndLabels();
}

bool StakingDialog::handlePaymentRequest(const SendCoinsRecipient &rv)
{
    // Just paste the entry, all pre-checks
    // are done in paymentserver.cpp.
    pasteEntry(rv);
    return true;
}

void StakingDialog::setBalance(const interfaces::WalletBalances& balances)
{
    if(model && model->getOptionsModel())
    {
        //ui->labelBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balances.balance));
        QString sBalance = BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balances.balance);

        if (balances.balanceBlind > 0)
            sBalance += "\n" + BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balances.balanceBlind) + " B";
        if (balances.balanceAnon > 0)
            sBalance += "\n" + BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balances.balanceAnon) + " A";
        ui->labelBalance->setText(sBalance);
    }
}

void StakingDialog::updateDisplayUnit()
{
    setBalance(model->wallet().getBalances());
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void StakingDialog::processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn, const QString &msgArg)
{
    QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // This comment is specific to StakingDialog usage of WalletModel::SendCoinsReturn.
    // WalletModel::TransactionCommitFailed is used only in WalletModel::sendCoins()
    // all others are used only in WalletModel::prepareTransaction()
    switch(sendCoinsReturn.status)
    {
    case WalletModel::InvalidAddress:
        msgParams.first = tr("The recipient address is not valid. Please recheck.");
        break;
    case WalletModel::InvalidAmount:
        msgParams.first = tr("The amount to pay must be larger than 0.");
        break;
    case WalletModel::AmountExceedsBalance:
        msgParams.first = tr("The amount exceeds your balance.");
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        msgParams.first = tr("The total exceeds your balance when the %1 transaction fee is included.").arg(msgArg);
        break;
    case WalletModel::DuplicateAddress:
        msgParams.first = tr("Duplicate address found: addresses should only be used once each.");
        break;
    case WalletModel::TransactionCreationFailed:
        msgParams.first = tr("Transaction creation failed!");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::TransactionCommitFailed:
        msgParams.first = tr("The transaction was rejected with the following reason: %1").arg(sendCoinsReturn.reasonCommitFailed);
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::AbsurdFee:
        msgParams.first = tr("A fee higher than %1 is considered an absurdly high fee.").arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), model->node().getMaxTxFee()));
        break;
    case WalletModel::PaymentRequestExpired:
        msgParams.first = tr("Payment request expired.");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    // included to prevent a compiler warning.
    case WalletModel::OK:
    default:
        return;
    }

    Q_EMIT message(tr("Send Coins"), msgParams.first, msgParams.second);
}

void StakingDialog::minimizeFeeSection(bool fMinimize)
{
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee  ->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void StakingDialog::on_buttonChooseFee_clicked()
{
    minimizeFeeSection(false);
}

void StakingDialog::on_buttonMinimizeFee_clicked()
{
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
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

void StakingDialog::useAvailableBalance(SendCoinsEntry* entry)
{
    // Get CCoinControl instance if CoinControl is enabled or create a new one.
    CCoinControl coin_control;
    if (model->getOptionsModel()->getCoinControlFeatures()) {
        coin_control = *CoinControlDialog::coinControl(GetCoinControlFlag());
    }

    QString sTypeFrom = GetFrom();
    // Calculate available amount to send.

    CAmount amount =
        sTypeFrom == "anon" ? model->wallet().getAvailableAnonBalance(coin_control) :
        sTypeFrom == "blind" ? model->wallet().getAvailableBlindBalance(coin_control) :
        model->wallet().getAvailableBalance(coin_control);

    for (int i = 0; i < ui->entries->count(); ++i) {
        SendCoinsEntry* e = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if (e && !e->isHidden() && e != entry) {
            amount -= e->getValue().amount;
        }
    }

    if (amount > 0) {
      entry->checkSubtractFeeFromAmount();
      entry->setAmount(amount);
    } else {
      entry->setAmount(0);
    }
}

void StakingDialog::setMinimumFee()
{
    ui->customFee->setValue(model->wallet().getRequiredFee(1000));
}

void StakingDialog::updateFeeSectionControls()
{
    ui->confTargetSelector      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee           ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee2          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee3          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelFeeEstimation      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->checkBoxMinimumFee      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelMinFeeWarning      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelCustomPerKilobyte  ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
    ui->customFee               ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
}

void StakingDialog::updateFeeMinimizedLabel()
{
    if(!model || !model->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        ui->labelFeeMinimized->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) + "/kB");
    }
}

void StakingDialog::updateMinFeeLabel()
{
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
            BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), model->wallet().getRequiredFee(1000)) + "/kB")
        );
}

void StakingDialog::updateCoinControlState(CCoinControl& ctrl)
{
    if (ui->radioCustomFee->isChecked()) {
        ctrl.m_feerate = CFeeRate(ui->customFee->value());
    } else {
        ctrl.m_feerate.reset();
    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    ctrl.m_confirm_target = getConfTargetForIndex(ui->confTargetSelector->currentIndex());
    ctrl.m_signal_bip125_rbf = ui->optInRBF->isChecked();
}

CoinControlDialog::ControlModes StakingDialog::GetCoinControlFlag()
{
    switch(modeSelection.checkedId()){
    case 0:
        return CoinControlDialog::INVALID;
    case 1:
        return CoinControlDialog::CONVERT_TO_SPENDING;
    case 2:
        return CoinControlDialog::CONVERT_TO_STAKING;
    case 3:
        return CoinControlDialog::CONVERT_TO_COLD_STAKE;
    }

    return CoinControlDialog::INVALID;
}

QString StakingDialog::GetFrom()
{
    switch(GetCoinControlFlag()){
    case CoinControlDialog::CONVERT_TO_SPENDING:
    case CoinControlDialog::CONVERT_TO_COLD_STAKE:
        return "part";
    case CoinControlDialog::CONVERT_TO_STAKING:
        return "anon";
    case CoinControlDialog::SPENDING:
    case CoinControlDialog::INVALID:
        return "invalid";
    }

    return "error";
}

QString StakingDialog::GetTo()
{
    switch(GetCoinControlFlag()){
    case CoinControlDialog::CONVERT_TO_SPENDING:
        return "anon";
    case CoinControlDialog::CONVERT_TO_STAKING:
    case CoinControlDialog::CONVERT_TO_COLD_STAKE:
        return "part";
    case CoinControlDialog::SPENDING:
    case CoinControlDialog::INVALID:
        return "invalid";
    }

    return "error";
}

void StakingDialog::updateSmartFeeLabel()
{
    if(!model || !model->getOptionsModel())
        return;
    CCoinControl coin_control;
    updateCoinControlState(coin_control);
    coin_control.m_feerate.reset(); // Explicitly use only fee estimation rate for smart fee labels
    int returned_target;
    FeeReason reason;
    CFeeRate feeRate = CFeeRate(model->wallet().getMinimumFee(1000, coin_control, &returned_target, &reason));

    ui->labelSmartFee->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), feeRate.GetFeePerK()) + "/kB");

    if (reason == FeeReason::FALLBACK) {
        ui->labelSmartFee2->show(); // (Smart fee not initialized yet. This usually takes a few blocks...)
        ui->labelFeeEstimation->setText("");
        ui->fallbackFeeWarningLabel->setVisible(true);
        int lightness = ui->fallbackFeeWarningLabel->palette().color(QPalette::WindowText).lightness();
        QColor warning_colour(255 - (lightness / 5), 176 - (lightness / 3), 48 - (lightness / 14));
        ui->fallbackFeeWarningLabel->setStyleSheet("QLabel { color: " + warning_colour.name() + "; }");
        ui->fallbackFeeWarningLabel->setIndent(QFontMetrics(ui->fallbackFeeWarningLabel->font()).width("x"));
    }
    else
    {
        ui->labelSmartFee2->hide();
        ui->labelFeeEstimation->setText(tr("Estimated to begin confirmation within %n block(s).", "", returned_target));
        ui->fallbackFeeWarningLabel->setVisible(false);
    }

    updateFeeMinimizedLabel();
}

void StakingDialog::modeChanged(int nNewMode)
{
    clear();

    switch(nNewMode){
    case OVERVIEW: // Used for overview

        ui->sendWidget->hide();
        ui->frameFee->hide();
        ui->scrollArea->hide();
        ui->frameStakingInfo->show();
        ui->frameHotStakingInfo->show();
        ui->frameColdStakingInfo->show();

        break;
    case TO_SPENDING:

        ui->sendWidget->show();
        ui->frameFee->show();
        ui->scrollArea->show();
        ui->frameStakingInfo->hide();
        ui->frameHotStakingInfo->hide();
        ui->frameColdStakingInfo->hide();

        addEntry();
        break;
    case TO_STAKING:

        ui->sendWidget->show();
        ui->frameFee->show();
        ui->scrollArea->show();
        ui->frameStakingInfo->hide();
        ui->frameHotStakingInfo->hide();
        ui->frameColdStakingInfo->hide();

        addEntry();
        break;
    case TO_COLD_STAKING:

        ui->sendWidget->show();
        ui->frameFee->show();
        ui->scrollArea->show();
        ui->frameStakingInfo->hide();
        ui->frameHotStakingInfo->hide();
        ui->frameColdStakingInfo->hide();

        addEntryCS();
        break;
    }

    if( model ){
        ui->frameCoinControl->setVisible(model->getOptionsModel()->getCoinControlFeatures() && nNewMode > 0);
    }

    updateTabsAndLabels();
}

// Coin Control: copy label "Quantity" to clipboard
void StakingDialog::coinControlClipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void StakingDialog::coinControlClipboardAmount()
{
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void StakingDialog::coinControlClipboardFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "After fee" to clipboard
void StakingDialog::coinControlClipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Bytes" to clipboard
void StakingDialog::coinControlClipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Dust" to clipboard
void StakingDialog::coinControlClipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void StakingDialog::coinControlClipboardChange()
{
    GUIUtil::setClipboard(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

void StakingDialog::cbxTypeFromChanged(int index)
{
    if (model && model->getOptionsModel()->getCoinControlFeatures())
        CoinControlDialog::coinControl(GetCoinControlFlag())->nCoinType = index+1;
};

// Coin Control: settings menu - coin control enabled/disabled by user
void StakingDialog::coinControlFeatureChanged(bool checked)
{
    ui->frameCoinControl->setVisible(checked);

    if (!checked && model) // coin control features disabled
        CoinControlDialog::coinControl(GetCoinControlFlag())->SetNull();

    coinControlUpdateLabels();
}

// Coin Control: button inputs -> show actual coin control dialog
void StakingDialog::coinControlButtonClicked()
{
    if( coinControlDialog ){
        delete coinControlDialog;
    }

    coinControlDialog = new CoinControlDialog(platformStyle, GetCoinControlFlag());
    coinControlDialog->setModel(model);
    coinControlDialog->exec();
    coinControlUpdateLabels();
}

// Coin Control: checkbox custom change address
void StakingDialog::coinControlChangeChecked(int state)
{
    if (state == Qt::Unchecked)
    {
        CoinControlDialog::coinControl(GetCoinControlFlag())->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->clear();
    }
    else
        // use this to re-validate an already entered address
        coinControlChangeEdited(ui->lineEditCoinControlChange->text());

    ui->lineEditCoinControlChange->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void StakingDialog::coinControlChangeEdited(const QString& text)
{
    if (model && model->getAddressTableModel())
    {
        // Default to no change address until verified
        CoinControlDialog::coinControl(GetCoinControlFlag())->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:red;}");

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelCoinControlChangeLabel->setText("");
        }
        else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelCoinControlChangeLabel->setText(tr("Warning: Invalid Bitcoin Confidentail address"));
        }
        else // Valid address
        {
            //if (!model->wallet().isSpendable(dest)) {
            if (!model->wallet().ownDestination(dest)) // Unknown change address
            {
                ui->labelCoinControlChangeLabel->setText(tr("Warning: Unknown change address"));

                // confirmation dialog
                QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm custom change address"), tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

                if(btnRetVal == QMessageBox::Yes)
                    CoinControlDialog::coinControl(GetCoinControlFlag())->destChange = dest;
                else
                {
                    ui->lineEditCoinControlChange->setText("");
                    ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");
                    ui->labelCoinControlChangeLabel->setText("");
                }
            }
            else // Known change address
            {
                ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");

                // Query label
                QString associatedLabel = model->getAddressTableModel()->labelForAddress(text);
                if (!associatedLabel.isEmpty())
                    ui->labelCoinControlChangeLabel->setText(associatedLabel);
                else
                    ui->labelCoinControlChangeLabel->setText(tr("(no label)"));

                CoinControlDialog::coinControl(GetCoinControlFlag())->destChange = dest;
            }
        }
    }
}

// Coin Control: update labels
void StakingDialog::coinControlUpdateLabels()
{
    if (!model || !model->getOptionsModel())
        return;

    updateCoinControlState(*CoinControlDialog::coinControl(GetCoinControlFlag()));

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    CoinControlDialog::fSubtractFeeFromAmount = false;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry && !entry->isHidden())
        {
            SendCoinsRecipient rcp = entry->getValue();
            CoinControlDialog::payAmounts.append(rcp.amount);
            if (rcp.fSubtractFeeFromAmount)
                CoinControlDialog::fSubtractFeeFromAmount = true;
        }
    }

    if (CoinControlDialog::coinControl(GetCoinControlFlag())->HasSelected())
    {
        // actual coin control calculation
        if( coinControlDialog ){
            coinControlDialog->updateLabels(model, this);
        }

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}
