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

SendCoinsEntry::SendCoinsEntry(const PlatformStyle *_platformStyle, QWidget *parent, bool fSpending, bool coldstake) :
    QStackedWidget(parent),
    ui(new Ui::SendCoinsEntry),
    model(0),
    platformStyle(_platformStyle),
    m_coldstake(coldstake),
    fSpending(fSpending)
{
    ui->setupUi(this);

    if (m_coldstake) {
        ui->addressBookButton_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/address-book"));
        ui->pasteButton_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/editpaste"));
        ui->addressBookButton2_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/address-book"));
        ui->pasteButton2_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/editpaste"));
        ui->deleteButton_cs->setIcon(platformStyle->BitcoinCColorIcon(":/icons/remove"));

        setCurrentWidget(ui->SendCoins_cs);

        // normal bitcoin address field
        GUIUtil::setupStakeAddressWidget(ui->stakeAddr, this, false, true);
        GUIUtil::setupStakeAddressWidget(ui->spendAddr, this, true);

        // Connect signals
        connect(ui->payAmount_cs, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
        connect(ui->checkboxSubtractFeeFromAmount_cs, SIGNAL(toggled(bool)), this, SIGNAL(subtractFeeFromAmountChanged()));
        connect(ui->deleteButton_cs, SIGNAL(clicked()), this, SLOT(deleteClicked()));
        connect(ui->useAvailableBalanceButton_cs, SIGNAL(clicked()), this, SLOT(useAvailableBalanceClicked()));

        return;
    }

    ui->addressBookButton->setIcon(platformStyle->BitcoinCColorIcon(":/icons/address-book"));
    ui->pasteButton->setIcon(platformStyle->BitcoinCColorIcon(":/icons/editpaste"));
    ui->deleteButton->setIcon(platformStyle->BitcoinCColorIcon(":/icons/remove"));
    ui->deleteButton_is->setIcon(platformStyle->BitcoinCColorIcon(":/icons/remove"));
    ui->deleteButton_s->setIcon(platformStyle->BitcoinCColorIcon(":/icons/remove"));

    setCurrentWidget(ui->SendCoins);

//    if (platformStyle->getUseExtraSpacing())
//        ui->payToLayout->setSpacing(4);
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));

    // normal bitcoin address field

    if( fSpending ){
        GUIUtil::setupAddressWidget(ui->payTo, this);
    }else{
        GUIUtil::setupStakeAddressWidget(ui->payTo, this);
    }

    // Connect signals
    connect(ui->payAmount, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
    connect(ui->checkboxSubtractFeeFromAmount, SIGNAL(toggled(bool)), this, SIGNAL(subtractFeeFromAmountChanged()));
    connect(ui->deleteButton, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_is, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->deleteButton_s, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->useAvailableBalanceButton, SIGNAL(clicked()), this, SLOT(useAvailableBalanceClicked()));
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
    ui->checkboxSubtractFeeFromAmount->setChecked(true);
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

    if (m_coldstake) {
        if (!model->validateAddress(ui->stakeAddr->text(), true)) {
            ui->stakeAddr->setValid(false);
            retval = false;
        }
        if (!model->validateAddress(ui->spendAddr->text())) {
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

        return retval;
    }

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

    return retval;
}

SendCoinsRecipient SendCoinsEntry::getValue()
{
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    recipient.m_coldstake = m_coldstake;
    if (m_coldstake) {
        recipient.stake_address = ui->stakeAddr->text();
        recipient.spend_address = ui->spendAddr->text();
        recipient.amount = ui->payAmount_cs->value();
        recipient.narration = "";
        recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount_cs->checkState() == Qt::Checked);

        return recipient;
    }

    // Normal payment
    recipient.address = ui->payTo->text();
    recipient.label = ui->addAsLabel->text();
    recipient.amount = ui->payAmount->value();
    recipient.narration = ui->edtNarration->text();
    recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked);

    return recipient;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    QWidget *w = ui->payAmount->setupTabChain(ui->addAsLabel);
    QWidget::setTabOrder(w, ui->checkboxSubtractFeeFromAmount);
    QWidget::setTabOrder(ui->checkboxSubtractFeeFromAmount, ui->addressBookButton);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    return ui->deleteButton;
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
    if (m_coldstake) {
        ui->payAmount_cs->setValue(amount);
        return;
    }
    ui->payAmount->setValue(amount);
}

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty();
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus();
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
