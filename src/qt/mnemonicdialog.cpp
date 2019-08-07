// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2019 The Bitcoin Confidential Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mnemonicdialog.h"
#include <qt/forms/ui_mnemonicdialog.h>

#include <qt/guiutil.h>

#include <qt/walletmodel.h>

#include <interfaces/wallet.h>

#include <rpc/rpcutil.h>
#include <util.h>
#include <key/mnemonic.h>
#include <key/extkey.h>

#include <QDebug>

void RPCThread::run()
{
    bool passed = false;
    CallRPCVoidRv(m_command.toStdString(), m_wallet.toStdString(), &passed, m_rv);
    Q_EMIT complete(passed);   // Can't pass back a UniValue or signal won't get detected ?
}

MnemonicDialog::MnemonicDialog(QWidget *parent, WalletModel *wm) :
    QDialog(parent), walletModel(wm),
    ui(new Ui::MnemonicDialog)
{
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    ui->setupUi(this);

    QObject::connect(ui->btnCancel2, SIGNAL(clicked()), this, SLOT(on_btnCancel_clicked()));
//    QObject::connect(ui->btnCancel3, SIGNAL(clicked()), this, SLOT(on_btnCancel_clicked()));

    QObject::connect(this, SIGNAL(startRescan()), walletModel, SLOT(startRescan()), Qt::QueuedConnection);

    setWindowTitle(QString("HD Wallet Setup - %1").arg(QString::fromStdString(wm->wallet().getWalletName())));
//    ui->edtPath->setPlaceholderText(tr("Path to derive account from, if not using default. (optional, default=%1)").arg(QString::fromStdString(GetDefaultAccountPath())));
    ui->edtPassword->setPlaceholderText(tr("Optionally enter a passphrase to protect the Recovery Phrase. (This is not a wallet password.)"));
#if QT_VERSION >= 0x050200
    ui->tbxMnemonic->setPlaceholderText(tr("Enter the Recovery Phrase/Mnemonic."));
#endif

#if ENABLE_USBDEVICE
#else
//    ui->tabWidget->setTabEnabled(2, false);
#endif

    UniValue rv;
    if (walletModel->tryCallRpc("mnemonic new '' english 32", rv)) {
        ui->lblMnemonicOut->setText(QString::fromStdString(rv["mnemonic"].get_str()));
    }

    fInitialSetup = !wm->wallet().isDefaultAccountSet();

    if ( fInitialSetup ) {
        ui->stackedWidget->setCurrentIndex(0);
        ui->lblHelp->hide();

        ui->btnTabCreate->setEnabled(false);
        ui->btnTabImport->setEnabled(false);
        ui->btnTabCreate->setChecked(true);
        ui->btnTabImport->setChecked(false);

        ui->btnCancel->hide();

        ui->btnImport->setText(tr("Create wallet"));

//        ui->lblHelp->setText(QString(
//            "Wallet %1 needs a mnemonic(recovery phrase) created in order to receive funds.\n"
//            "Create a new recovery phrase with the 'Create' tab, Generate, copy, and paste in the 'Import' tab.\n"
//            "If you are recovering a backup, enter your recovery phrase in the 'Import' tab.\n").arg(QString::fromStdString(wm->wallet().getWalletName())));
    } else {
        ui->stackedWidget->setCurrentIndex(1);
        ui->lblHelp->show();
        ui->btnNext->hide();
        ui->btnBack->hide();
        ui->lblWarning->setText(QString(
            "Wallet %1 already has an HD account loaded.\n"
            "By importing another recovery phrase a new account will be created and set as the default. "
            "The wallet will receive on addresses from the new and existing accounts. "
            "New addresses will be generated from the new account.").arg(QString::fromStdString(wm->wallet().getWalletName())));
        ui->mnemonicCreateHelp->setText(tr("Generate a new Recovery Phrase/Mnemonic with the desired settings by clicking \"NEW MNEMONIC\"."));

        tabButtons.addButton(ui->btnTabCreate, 0);
        tabButtons.addButton(ui->btnTabImport, 1);

        ui->btnTabImport->setText(tr("Import a mnemonic"));

        connect(&tabButtons, SIGNAL(buttonClicked(int)), ui->stackedWidget, SLOT(setCurrentIndex(int)));
    }

    ui->cbxLanguage->clear();
    for (int l = 1; l < WLL_MAX; ++l) {
        ui->cbxLanguage->addItem(mnLanguagesDesc[l], QString(mnLanguagesTag[l]));
    }

    ui->importChainLabel->hide();
    ui->chkImportChain->hide();

    return;
}

MnemonicDialog::~MnemonicDialog()
{
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
};

void MnemonicDialog::on_btnCancel_clicked()
{
    if( fInitialSetup ){
        // confirmation dialog
        QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm wallet creation cancel"),
            tr("Are you sure you want to cancel the wallet creation?") + "<br><br>" + tr("This is only recommended for advanced users to import with custom settings in the debug console or with the RPC interface."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

        if(btnRetVal != QMessageBox::Yes)
            return;
    }

    close();
}

void MnemonicDialog::on_btnImport_clicked()
{
    QString sCommand = (ui->chkImportChain->checkState() == Qt::Unchecked)
        ? "extkeyimportmaster" : "extkeygenesisimport";
    sCommand += " \"" + ui->tbxMnemonic->toPlainText() + "\"";

    QString sPassword = ui->edtPassword->text();
    sCommand += " \"" + sPassword + "\" false \"Master Key\" \"Default Account\" 0";

    UniValue rv;
    if (walletModel->tryCallRpc(sCommand, rv)) {
        close();
        if (!rv["warnings"].isNull()) {
            for (size_t i = 0; i < rv["warnings"].size(); ++i) {
                walletModel->warningBox(tr("Import"), QString::fromStdString(rv["warnings"][i].get_str()));
            }
        }
        startRescan();
    }

    return;
};

void MnemonicDialog::on_btnGenerate_clicked()
{
    int nBytesEntropy = ui->spinEntropy->value();
    QString sLanguage = ui->cbxLanguage->itemData(ui->cbxLanguage->currentIndex()).toString();

    QString sCommand = "mnemonic new  \"\" " + sLanguage + " " + QString::number(nBytesEntropy);

    UniValue rv;
    if (walletModel->tryCallRpc(sCommand, rv)) {
        ui->lblMnemonicOut->setText(QString::fromStdString(rv["mnemonic"].get_str()));
    }

    ui->btnNext->setEnabled(true);
}

void MnemonicDialog::on_btnNext_clicked()
{
    // Clear the mnemonic from the clipboard to force the user to at least copy it somewhere else before
    GUIUtil::setClipboard("");
    // Show the import tab
    ui->stackedWidget->setCurrentIndex(1);

    ui->btnTabCreate->setChecked(false);
    ui->btnTabImport->setChecked(true);
}

void MnemonicDialog::on_btnBack_clicked()
{
    ui->stackedWidget->setCurrentIndex(0);

    ui->btnTabCreate->setChecked(true);
    ui->btnTabImport->setChecked(false);
}

//void MnemonicDialog::on_btnImportFromHwd_clicked()
//{
//    if (m_thread) {
//        qWarning() << "MnemonicDialog hwd thread exists.";
//        return;
//    }
//    QString sCommand = "initaccountfromdevice \"From Hardware Device\"";

//    QString sPath = ui->edtPath->text();
//    sCommand += " \"" + sPath + "\" true -1";

//    ui->tbxHwdOut->appendPlainText("Waiting for device.");
//    setEnabled(false);

//    m_thread = new RPCThread(sCommand, walletModel->getWalletName(), &m_rv);
//    connect(m_thread, SIGNAL(complete(bool)), this, SLOT(hwImportComplete(bool)));
//    m_thread->setObjectName("bitcoinc-hwImport");
//    m_thread->start();

//    return;
//};

//void MnemonicDialog::hwImportComplete(bool passed)
//{
//    setEnabled(true);

//    m_thread->wait();
//    delete m_thread;
//    m_thread = nullptr;

//    if (!passed) {
//        QString sError;
//        if (m_rv["Error"].isStr()) {
//            sError = QString::fromStdString(m_rv["Error"].get_str());
//        } else {
//            sError = QString::fromStdString(m_rv.write(1));
//        }

//        ui->tbxHwdOut->appendPlainText(sError);
//        if (sError == "No device found."
//            || sError.indexOf("6982") > -1) {
//            ui->tbxHwdOut->appendPlainText("Open bitcoinc app on device before importing.");
//        }
//    } else {
//        UniValue rv;
//        QString sCommand = "devicegetnewstealthaddress \"default stealth\"";
//        walletModel->tryCallRpc(sCommand, rv);
//        close();

//        startRescan();
//    }

//    return;
//};
