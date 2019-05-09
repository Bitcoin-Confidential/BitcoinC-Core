// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/sendcoinsdialog.h>
#include <qt/forms/ui_sendcoinsdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/coincontroldialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/sendcoinsentry.h>
#include <qt/sendcoinsdialog.h>

#include <anon.h>
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

static const std::array<int, 9> confTargets = { {2, 4, 6, 12, 24, 48, 144, 504, 1008} };
static int getConfTargetForIndex(int index) {
    if (index+1 > static_cast<int>(confTargets.size())) {
        return confTargets.back();
    }
    if (index < 0) {
        return confTargets[0];
    }
    return confTargets[index];
}
static int getIndexForConfTarget(int target) {
    for (unsigned int i = 0; i < confTargets.size(); i++) {
        if (confTargets[i] >= target) {
            return i;
        }
    }
    return confTargets.size() - 1;
}

SendCoinsDialog::SendCoinsDialog(const PlatformStyle *_platformStyle, bool fStakingDialog, bool fIsConvertToTab, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SendCoinsDialog),
    clientModel(0),
    model(0),
    fNewRecipientAllowed(true),
    fFeeMinimized(true),
    platformStyle(_platformStyle),
    coinControlDialog(nullptr),
    fStakingDialog(fStakingDialog),
    fIsConvertTab(fIsConvertToTab),
    nMode(CoinControlDialog::SPENDING)
{
    ui->setupUi(this);

    if( fStakingDialog ){
        ui->addButton->setIcon(_platformStyle->BitcoinCColorIcon(":/icons/add"));
        ui->addButton->setText(tr("Add Output"));
    }else{
        ui->addButton->setIcon(_platformStyle->BitcoinCColorIcon(":/icons/add_recipient"));
        ui->addButton->setText(tr("Add Recipient"));
    }

    ui->clearButton->setIcon(_platformStyle->BitcoinCColorIcon(":/icons/remove"));
    ui->sendButton->setIcon(_platformStyle->SingleColorIcon(":/icons/send"));

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

    ui->scrollAreaWidgetContents->installEventFilter(this);

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
}

bool SendCoinsDialog::eventFilter(QObject *o, QEvent *e)
{
    if(o == ui->scrollAreaWidgetContents && e->type() == QEvent::Resize)
        setMinimumWidth(ui->scrollAreaWidgetContents->minimumSizeHint().width() + ui->scrollArea->verticalScrollBar()->width());

    return false;
}

void SendCoinsDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }
}

void SendCoinsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        coinControlFeatureChanged(model->getOptionsModel()->getCoinControlFeatures());

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
        ui->optInRBF->setCheckState(Qt::Unchecked);

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

        if( !fStakingDialog ){
            ui->sendTabHeaderLabel->setText(GUIUtil::GetSpendingWaitInfo());
        }else if( fIsConvertTab ){
            ui->sendTabHeaderLabel->setText(GUIUtil::GetStakingWaitInfo());
        }else{
            ui->sendTabHeaderLabel->setText(tr("This tab allows manually activating Staking funds for ColdStaking. "
                                               "This will take longer than the \"Automated ColdStaking activation\"to start staking but wallet does not need to be online for the activation."));
        }

        clear();
    }
}

SendCoinsDialog::~SendCoinsDialog()
{
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    settings.setValue("nTransactionFee", (qint64)ui->customFee->value());
    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());

    delete ui;
}

void SendCoinsDialog::on_sendButton_clicked()
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

            CBitcoinAddress stakeAddress(rcp.stake_address.toStdString());

            if (model->wallet().ownDestination(stakeAddress.Get())) // Unknown change address
            {
                QMessageBox::critical(this, tr("Error Remote Staking Address"), tr("The remote stake address can't be part of this wallet."));
                return;
            }

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

    int nRingSize = DEFAULT_RING_SIZE;
    int nMaxInputs = DEFAULT_INPUTS_PER_SIG;

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
    if( !(sTypeFrom == "spending" && sTypeFrom == sTypeTo) ){
        questionString.append("</span><br /><b>"+sTypeFrom+ "</b> to <b>" +sTypeTo+"</b><br />%1");
    }else{
        questionString.append("</span><br />%1");
    }

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
        questionString.append("<span style='color:#E1755A;; font-weight:bold;'>");
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

    questionString.append(QString("<b>%1</b>: <b>%2</b>").arg(tr("Total Amount"))
        .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount)));

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

void SendCoinsDialog::on_addButton_clicked()
{
    if( GetCoinControlFlag() < CoinControlDialog::CONVERT_TO_COLD_STAKE)
        addEntry();
    else
        addEntryCS();
}

void SendCoinsDialog::clear()
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

    on_addButton_clicked();
}

void SendCoinsDialog::reject()
{
    clear();
}

void SendCoinsDialog::accept()
{
    clear();
}

SendCoinsEntry *SendCoinsDialog::addEntry()
{
    SendCoinsEntry *entry = new SendCoinsEntry(platformStyle, this, GetCoinControlFlag() < CoinControlDialog::CONVERT_TO_STAKING );
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));
    connect(entry, SIGNAL(useAvailableBalance(SendCoinsEntry*)), this, SLOT(useAvailableBalance(SendCoinsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    connect(entry, SIGNAL(subtractFeeFromAmountChanged()), this, SLOT(coinControlUpdateLabels()));

    if( GetCoinControlFlag() != CoinControlDialog::SPENDING ){
        entry->hideMessage();
    }
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

SendCoinsEntry *SendCoinsDialog::addEntryCS()
{
    SendCoinsEntry *entry = new SendCoinsEntry(platformStyle, this, false, true);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));
    connect(entry, SIGNAL(useAvailableBalance(SendCoinsEntry*)), this, SLOT(useAvailableBalance(SendCoinsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    connect(entry, SIGNAL(subtractFeeFromAmountChanged()), this, SLOT(coinControlUpdateLabels()));

    // Focus the field, so that entry can start immediately
    entry->hideMessage();
    entry->clear();
    entry->setFocus();

    UniValue rv;
    if (model->wallet().getBitcoinCWallet()->GetSetting("changeaddress", rv)) {
        if (rv.isObject()
            && rv["coldstakingaddress"].isStr()) {
            entry->setStakeAddress(QString::fromStdString(rv["coldstakingaddress"].get_str()));
        }
    }

    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());

    updateTabsAndLabels();
    return entry;
}

void SendCoinsDialog::updateTabsAndLabels()
{
    setupTabChain(0);
    coinControlUpdateLabels();
}

void SendCoinsDialog::removeEntry(SendCoinsEntry* entry)
{
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1){
        if( GetCoinControlFlag() < CoinControlDialog::CONVERT_TO_COLD_STAKE)
            addEntry();
        else
            addEntryCS();
    }

    entry->deleteLater();

    updateTabsAndLabels();
}

QWidget *SendCoinsDialog::setupTabChain(QWidget *prev)
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
    QWidget::setTabOrder(ui->clearButton, ui->addButton);
    return ui->addButton;
}

void SendCoinsDialog::setAddress(const QString &address)
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

void SendCoinsDialog::pasteEntry(const SendCoinsRecipient &rv)
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

bool SendCoinsDialog::handlePaymentRequest(const SendCoinsRecipient &rv)
{
    // Just paste the entry, all pre-checks
    // are done in paymentserver.cpp.
    pasteEntry(rv);
    return true;
}

void SendCoinsDialog::setBalance(const interfaces::WalletBalances& balances)
{
    if(model && model->getOptionsModel())
    {
        CAmount nBalance;

        if( GetCoinControlFlag() == CoinControlDialog::SPENDING || GetCoinControlFlag() == CoinControlDialog::CONVERT_TO_STAKING ){
            nBalance = balances.balanceSpending;
        }else{
            nBalance = balances.balanceStaking;
        }

        QString sBalance = BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), nBalance);

        ui->lblBalance->setText(sBalance);
    }
}

void SendCoinsDialog::updateDisplayUnit()
{
    setBalance(model->wallet().getBalances());
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void SendCoinsDialog::processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn, const QString &msgArg)
{
    QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // This comment is specific to SendCoinsDialog usage of WalletModel::SendCoinsReturn.
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

void SendCoinsDialog::minimizeFeeSection(bool fMinimize)
{
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee  ->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void SendCoinsDialog::on_buttonChooseFee_clicked()
{
    minimizeFeeSection(false);
}

void SendCoinsDialog::on_buttonMinimizeFee_clicked()
{
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void SendCoinsDialog::useAvailableBalance(SendCoinsEntry* entry)
{
    // Get CCoinControl instance if CoinControl is enabled or create a new one.
    CCoinControl coin_control;
    if (model->getOptionsModel()->getCoinControlFeatures()) {
        coin_control = *CoinControlDialog::coinControl(GetCoinControlFlag());
    }

    QString sTypeFrom = GetFrom();
    // Calculate available amount to send.

    CAmount amount =
        sTypeFrom == "spending" ? model->wallet().getAvailableAnonBalance(coin_control) :
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

void SendCoinsDialog::setMinimumFee()
{
    ui->customFee->setValue(model->wallet().getRequiredFee(1000));
}

void SendCoinsDialog::updateFeeSectionControls()
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

void SendCoinsDialog::updateFeeMinimizedLabel()
{
    if(!model || !model->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        ui->labelFeeMinimized->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) + "/kB");
    }
}

void SendCoinsDialog::updateMinFeeLabel()
{
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
            BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), model->wallet().getRequiredFee(1000)) + "/kB")
        );
}

void SendCoinsDialog::updateCoinControlState(CCoinControl& ctrl)
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

CoinControlDialog::ControlModes SendCoinsDialog::GetCoinControlFlag()
{
    return nMode;
}

QString SendCoinsDialog::GetFrom()
{
    switch(GetCoinControlFlag()){
    case CoinControlDialog::CONVERT_TO_SPENDING:
    case CoinControlDialog::CONVERT_TO_COLD_STAKE:
        return "staking";
    case CoinControlDialog::SPENDING:
    case CoinControlDialog::CONVERT_TO_STAKING:
        return "spending";
    }

    return "error";
}

QString SendCoinsDialog::GetTo()
{
    switch(GetCoinControlFlag()){
    case CoinControlDialog::SPENDING:
    case CoinControlDialog::CONVERT_TO_SPENDING:
        return "spending";
    case CoinControlDialog::CONVERT_TO_STAKING:
    case CoinControlDialog::CONVERT_TO_COLD_STAKE:
        return "staking";
    }

    return "error";
}

void SendCoinsDialog::updateSmartFeeLabel()
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

void SendCoinsDialog::setMode(CoinControlDialog::ControlModes nNewMode)
{
    nMode = nNewMode;

    switch(GetCoinControlFlag() ){
    case CoinControlDialog::SPENDING:
    case CoinControlDialog::CONVERT_TO_STAKING:
        GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);
        break;
    default:
        GUIUtil::setupStakeAddressWidget(ui->lineEditCoinControlChange, this);
    }

    updateTabsAndLabels();

    if( model ){
        interfaces::WalletBalances balances = model->wallet().getBalances();
        setBalance(balances);
    }


}

// Coin Control: copy label "Quantity" to clipboard
void SendCoinsDialog::coinControlClipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void SendCoinsDialog::coinControlClipboardAmount()
{
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void SendCoinsDialog::coinControlClipboardFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "After fee" to clipboard
void SendCoinsDialog::coinControlClipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Bytes" to clipboard
void SendCoinsDialog::coinControlClipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Dust" to clipboard
void SendCoinsDialog::coinControlClipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void SendCoinsDialog::coinControlClipboardChange()
{
    GUIUtil::setClipboard(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

void SendCoinsDialog::cbxTypeFromChanged(int index)
{
    if (model && model->getOptionsModel()->getCoinControlFeatures())
        CoinControlDialog::coinControl(GetCoinControlFlag())->nCoinType = index+1;
};

// Coin Control: settings menu - coin control enabled/disabled by user
void SendCoinsDialog::coinControlFeatureChanged(bool checked)
{
    ui->frameCoinControl->setVisible(checked);
    CoinControlDialog::coinControl(GetCoinControlFlag())->SetNull();
    coinControlUpdateLabels();
}

// Coin Control: button inputs -> show actual coin control dialog
void SendCoinsDialog::coinControlButtonClicked()
{
    if( !coinControlDialog ){
        coinControlDialog = new CoinControlDialog(platformStyle, GetCoinControlFlag());
        coinControlDialog->setModel(model);
    }
    coinControlDialog->updateView();
    coinControlDialog->exec();
    coinControlUpdateLabels();
}

// Coin Control: checkbox custom change address
void SendCoinsDialog::coinControlChangeChecked(int state)
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
void SendCoinsDialog::coinControlChangeEdited(const QString& text)
{
    if (model && model->getAddressTableModel())
    {
        // Default to no change address until verified
        CoinControlDialog::coinControl(GetCoinControlFlag())->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:#E1755A;}");

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelCoinControlChangeLabel->setText("");
        }
        else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelCoinControlChangeLabel->setText(tr("Warning: Invalid Bitcoin Confidential address"));
        }
        else // Valid address
        {
            //if (!model->wallet().isSpendable(dest)) {
            if (!model->wallet().ownDestination(dest)) // Unknown change address
            {
                // confirmation dialog
                QMessageBox::critical(this, tr("Error custom change address"), tr("The address you selected for change is not part of this wallet."));

                ui->lineEditCoinControlChange->setText("");
                ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");
                ui->labelCoinControlChangeLabel->setText("");
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
void SendCoinsDialog::coinControlUpdateLabels()
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

SendConfirmationDialog::SendConfirmationDialog(const QString &title, const QString &text, int _secDelay,
    QWidget *parent) :
    QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent), secDelay(_secDelay)
{
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, SIGNAL(timeout()), this, SLOT(countDown()));
}

int SendConfirmationDialog::exec()
{
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void SendConfirmationDialog::countDown()
{
    secDelay--;
    updateYesButton();

    if(secDelay <= 0)
    {
        countDownTimer.stop();
    }
}

void SendConfirmationDialog::updateYesButton()
{
    if(secDelay > 0)
    {
        yesButton->setEnabled(false);
        yesButton->setText(tr("Yes") + " (" + QString::number(secDelay) + ")");
    }
    else
    {
        yesButton->setEnabled(true);
        yesButton->setText(tr("Yes"));
    }
}
