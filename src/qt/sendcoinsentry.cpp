// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/sendcoinsentry.h>
#include <qt/forms/ui_sendcoinsentry.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>

#include <QApplication>
#include <QClipboard>

SendCoinsEntry::SendCoinsEntry(const PlatformStyle *_platformStyle, QWidget *parent, bool fSpending, bool fColdstake, bool fConvert) :
    QStackedWidget(parent),
    ui(new Ui::SendCoinsEntry),
    model(0),
    platformStyle(_platformStyle),
    fSpending(fSpending),
    fConvert(fConvert),
    fColdstake(fColdstake)
{
    ui->setupUi(this);

    if (fColdstake) {
        ui->addressBookButton_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/address-book"));
        ui->pasteButton_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/editpaste"));
        ui->addressBookButton2_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/address-book"));
        ui->pasteButton2_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/editpaste"));
        ui->deleteButton_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/remove"));

        setCurrentWidget(ui->SendCoins_cs);
        removeWidget(ui->SendCoins);
        removeWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        removeWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        removeWidget(ui->SendCoins_convert);

        // normal bitcoin address field
        GUIUtil::setupStakeAddressWidget(ui->stakeAddr, this, false, true);
        GUIUtil::setupStakeAddressWidget(ui->spendAddr, this, true);

        // Connect signals
        connect(ui->payAmount_cs, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
        connect(ui->checkboxSubtractFeeFromAmount_cs, SIGNAL(toggled(bool)), this, SIGNAL(subtractFeeFromAmountChanged()));
        connect(ui->deleteButton_cs, SIGNAL(clicked()), this, SLOT(deleteClicked()));
        connect(ui->useAvailableBalanceButton_cs, SIGNAL(clicked()), this, SLOT(useAvailableBalanceClicked()));

    }else if( fConvert ){
        ui->addressBookButton_convert->setIcon(platformStyle->BitcoinCColorIcon(":/icons/address-book"));
        ui->pasteButton_convert->setIcon(platformStyle->BitcoinCColorIcon(":/icons/editpaste"));
        ui->deleteButton_convert->setIcon(platformStyle->BitcoinCColorIcon(":/icons/remove"));

        setCurrentWidget(ui->SendCoins_convert);
        removeWidget(ui->SendCoins);
        removeWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        removeWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        removeWidget(ui->SendCoins_cs);

        // normal bitcoin address field
        if( fSpending ){
            GUIUtil::setupAddressWidget(ui->payTo_convert, this);
        }else{
            GUIUtil::setupStakeAddressWidget(ui->payTo_convert, this);
        }

        // Connect signals
        connect(ui->payAmount_convert, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
        connect(ui->checkboxSubtractFeeFromAmount_convert, SIGNAL(toggled(bool)), this, SIGNAL(subtractFeeFromAmountChanged()));
        connect(ui->deleteButton_convert, SIGNAL(clicked()), this, SLOT(deleteClicked()));
        connect(ui->useAvailableBalanceButton_convert, SIGNAL(clicked()), this, SLOT(useAvailableBalanceClicked()));

    }else{

        ui->addressBookButton->setIcon(platformStyle->BitcoinCColorIcon(":/icons/address-book"));
        ui->pasteButton->setIcon(platformStyle->BitcoinCColorIcon(":/icons/editpaste"));
        ui->deleteButton->setIcon(platformStyle->BitcoinCColorIcon(":/icons/remove"));
        ui->deleteButton_is->setIcon(platformStyle->BitcoinCColorIcon(":/icons/remove"));
        ui->deleteButton_s->setIcon(platformStyle->BitcoinCColorIcon(":/icons/remove"));

        setCurrentWidget(ui->SendCoins);
        removeWidget(ui->SendCoins_cs);
        removeWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        removeWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        removeWidget(ui->SendCoins_convert);

        ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));
        // normal bitcoin address field

        GUIUtil::setupAddressWidget(ui->payTo, this);

        // Connect signals
        connect(ui->payAmount, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
        connect(ui->checkboxSubtractFeeFromAmount, SIGNAL(toggled(bool)), this, SIGNAL(subtractFeeFromAmountChanged()));
        connect(ui->deleteButton, SIGNAL(clicked()), this, SLOT(deleteClicked()));
        connect(ui->deleteButton_is, SIGNAL(clicked()), this, SLOT(deleteClicked()));
        connect(ui->deleteButton_s, SIGNAL(clicked()), this, SLOT(deleteClicked()));
        connect(ui->useAvailableBalanceButton, SIGNAL(clicked()), this, SLOT(useAvailableBalanceClicked()));
    }
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui;
}

void SendCoinsEntry::hideMessage()
{
    ui->lblNarration->hide();
    ui->edtNarration->hide();
}

void SendCoinsEntry::hideAddLabel()
{
    ui->addAsLabel->hide();
    ui->labellLabel->hide();
}

void SendCoinsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(platformStyle,
                        AddressBookPage::ForSelection,
                        fSpending ? AddressBookPage::SendingTab : AddressBookPage::StakingTab,
                        this);
    dlg.setModel(fSpending ? model->getAddressTableModel() : model->getStakingAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendCoinsEntry::on_payTo_textChanged(const QString &address)
{
    updateLabel(address);
}

void SendCoinsEntry::on_pasteButton_cs_clicked()
{
    // Paste text from clipboard into recipient field
    ui->stakeAddr->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton_cs_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(platformStyle,
                        AddressBookPage::ForSelection,
                        AddressBookPage::StakingTab,
                        this);
    dlg.setModel(model->getStakingAddressTableModel());
    if(dlg.exec())
    {
        ui->stakeAddr->setText(dlg.getReturnValue());
        ui->spendAddr->setFocus();
    }
}

void SendCoinsEntry::on_pasteButton2_cs_clicked()
{
    // Paste text from clipboard into recipient field
    ui->spendAddr->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton2_cs_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(platformStyle,
                        AddressBookPage::ForSelection,
                        AddressBookPage::StakingTab,
                        this);
    dlg.setModel(model->getStakingAddressTableModel());
    if(dlg.exec())
    {
        ui->spendAddr->setText(dlg.getReturnValue());
        ui->payAmount_cs->setFocus();
    }
}

void SendCoinsEntry::setModel(WalletModel *_model)
{
    this->model = _model;

    if (_model && _model->getOptionsModel())
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    clear();
}

void SendCoinsEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    ui->checkboxSubtractFeeFromAmount->setCheckState(Qt::Unchecked);
    // clear UI elements for unauthenticated payment request
    ui->payTo_is->clear();
    ui->memoTextLabel_is->clear();
    ui->payAmount_is->clear();
    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();
    ui->payAmount_s->clear();

    ui->stakeAddr->clear();
    ui->spendAddr->clear();
    ui->payAmount_cs->clear();
    ui->checkboxSubtractFeeFromAmount_cs->setCheckState(Qt::Unchecked);
    ui->edtNarration->clear();

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void SendCoinsEntry::checkSubtractFeeFromAmount()
{
    if ( fColdstake ) {
        ui->checkboxSubtractFeeFromAmount_cs->setChecked(true);
    }else if( fConvert ){
        ui->checkboxSubtractFeeFromAmount_convert->setChecked(true);
    }else{
        ui->checkboxSubtractFeeFromAmount->setChecked(true);
    }
}

void SendCoinsEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

void SendCoinsEntry::useAvailableBalanceClicked()
{
    Q_EMIT useAvailableBalance(this);
}

bool SendCoinsEntry::validate(interfaces::Node& node)
{
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

    // Skip checks for payment request
    if (recipient.paymentRequest.IsInitialized())
        return retval;

    if (fColdstake) {
        if (!model->validateAddress(ui->stakeAddr->text(), true)) {
            ui->stakeAddr->setValid(false);
            retval = false;
        }
        if (!model->validateColdStakeAddress(ui->spendAddr->text())) {
            ui->spendAddr->setValid(false);
            retval = false;
        }

        if (!ui->payAmount_cs->validate()) {
            retval = false;
        }

        // Sending a zero amount is invalid
        if (ui->payAmount_cs->value(0) <= 0)
        {
            ui->payAmount_cs->setValid(false);
            retval = false;
        }

        // Reject dust outputs:
        if (retval && GUIUtil::isDust(node, ui->spendAddr->text(), ui->payAmount_cs->value())) {
            ui->payAmount_cs->setValid(false);
            retval = false;
        }

    }else if( fConvert ){

        if (!model->validateAddress(ui->payTo_convert->text()))
        {
            ui->payTo_convert->setValid(false);
            retval = false;
        }

        if (!ui->payAmount_convert->validate())
        {
            retval = false;
        }

        // Sending a zero amount is invalid
        if (ui->payAmount_convert->value(0) <= 0)
        {
            ui->payAmount_convert->setValid(false);
            retval = false;
        }

        // Reject dust outputs:
        if (retval && GUIUtil::isDust(node, ui->payTo_convert->text(), ui->payAmount_convert->value())) {
            ui->payAmount_convert->setValid(false);
            retval = false;
        }

    }else{

        if (!model->validateAddress(ui->payTo->text()))
        {
            ui->payTo->setValid(false);
            retval = false;
        }

        if (!ui->payAmount->validate())
        {
            retval = false;
        }

        // Sending a zero amount is invalid
        if (ui->payAmount->value(0) <= 0)
        {
            ui->payAmount->setValid(false);
            retval = false;
        }

        // Reject dust outputs:
        if (retval && GUIUtil::isDust(node, ui->payTo->text(), ui->payAmount->value())) {
            ui->payAmount->setValid(false);
            retval = false;
        }

    }

    return retval;
}

SendCoinsRecipient SendCoinsEntry::getValue()
{
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    recipient.m_coldstake = fColdstake;
    if (fColdstake) {
        recipient.stake_address = ui->stakeAddr->text();
        recipient.spend_address = ui->spendAddr->text();
        recipient.amount = ui->payAmount_cs->value();
        recipient.narration = "";
        recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount_cs->checkState() == Qt::Checked);
    }else if( fConvert ){
        // Convert payment
        recipient.address = ui->payTo_convert->text();
        recipient.label = "";
        recipient.amount = ui->payAmount_convert->value();
        recipient.narration = "";
        recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount_convert->checkState() == Qt::Checked);
    }else{
        // Normal payment
        recipient.address = ui->payTo->text();
        recipient.label = ui->addAsLabel->text();
        recipient.amount = ui->payAmount->value();
        recipient.narration = ui->edtNarration->text();
        recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked);
    }

    return recipient;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget *last;
    if( fColdstake ){
        QWidget::setTabOrder(prev, ui->stakeAddr);
        QWidget::setTabOrder(ui->stakeAddr, ui->spendAddr);
        QWidget *w = ui->payAmount_cs->setupTabChain(ui->spendAddr);
        QWidget::setTabOrder(w, ui->checkboxSubtractFeeFromAmount_cs);
        QWidget::setTabOrder(ui->checkboxSubtractFeeFromAmount_cs, ui->addressBookButton_cs);
        QWidget::setTabOrder(ui->addressBookButton_cs, ui->pasteButton_cs);
        QWidget::setTabOrder(ui->pasteButton_cs, ui->deleteButton_cs);
        last = ui->deleteButton_cs;
    }else if( fConvert ){
        QWidget::setTabOrder(prev, ui->payTo_convert);
        QWidget *w = ui->payAmount->setupTabChain(ui->payAmount_convert);
        last = w;
    }else{
        QWidget::setTabOrder(prev, ui->payTo);
        QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
        QWidget *w = ui->payAmount->setupTabChain(ui->addAsLabel);
        QWidget::setTabOrder(w, ui->edtNarration);
        QWidget::setTabOrder(ui->edtNarration, ui->checkboxSubtractFeeFromAmount);
        QWidget::setTabOrder(ui->checkboxSubtractFeeFromAmount, ui->addressBookButton);
        QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
        QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
        last = ui->deleteButton;
    }
    return last;
}

void SendCoinsEntry::setValue(const SendCoinsRecipient &value)
{
    recipient = value;

    if (recipient.paymentRequest.IsInitialized()) // payment request
    {
        if (recipient.authenticatedMerchant.isEmpty()) // unauthenticated
        {
            ui->payTo_is->setText(recipient.address);
            ui->memoTextLabel_is->setText(recipient.message);
            ui->payAmount_is->setValue(recipient.amount);
            ui->payAmount_is->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        }
        else // authenticated
        {
            ui->payTo_s->setText(recipient.authenticatedMerchant);
            ui->memoTextLabel_s->setText(recipient.message);
            ui->payAmount_s->setValue(recipient.amount);
            ui->payAmount_s->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        }
    }
    else // normal payment
    {

        ui->addAsLabel->clear();
        ui->payTo->setText(recipient.address); // this may set a label from addressbook
        if (!recipient.label.isEmpty()) // if a label had been set from the addressbook, don't overwrite with an empty label
            ui->addAsLabel->setText(recipient.label);
        ui->payAmount->setValue(recipient.amount);
    }
}

void SendCoinsEntry::setAddress(const QString &address)
{
    ui->payTo->setText(address);
    ui->payAmount->setFocus();
}

void SendCoinsEntry::setAmount(const CAmount &amount)
{
    if (fColdstake) {
        ui->payAmount_cs->setValue(amount);
    }else if( fConvert ){
        ui->payAmount_convert->setValue(amount);
    }else{
        ui->payAmount->setValue(amount);
    }
}

void SendCoinsEntry::setStakeAddress(const QString &address)
{
    ui->stakeAddr->setText(address);
    ui->spendAddr->setFocus();
}

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty() &&
           ui->stakeAddr->text().isEmpty() &&
           ui->payTo_convert->text().isEmpty() &&
           ui->payTo_is->text().isEmpty() &&
           ui->payTo_s->text().isEmpty();
}

void SendCoinsEntry::setFocus()
{
    if( fColdstake ){
        ui->stakeAddr->setFocus();
    }else if( fConvert ){
        ui->payTo_convert->setFocus();
    }else{
        ui->payTo->setFocus();
    }
}

void SendCoinsEntry::hideDeleteButton()
{
    ui->deleteButton->hide();
    ui->deleteButton_cs->hide();
}

void SendCoinsEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update payAmount with the current unit
        ui->payAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_is->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_s->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_cs->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_convert->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

bool SendCoinsEntry::updateLabel(const QString &address)
{
    if(!model)
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
    {
        ui->addAsLabel->setText(associatedLabel);
        return true;
    }

    return false;
}
