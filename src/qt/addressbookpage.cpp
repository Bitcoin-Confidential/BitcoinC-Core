// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include "addressbookpage.h"
#include <qt/forms/ui_addressbookpage.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoingui.h>
#include <qt/csvmodelwriter.h>
#include <qt/editaddressdialog.h>
#include <qt/guiutil.h>
#include <qt/platformstyle.h>

#include "key_io.h"
#include "utilstrencodings.h"
#include "random.h"

#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>

class AddressBookSortFilterProxyModel final : public QSortFilterProxyModel
{
    const QString m_type;

public:
    AddressBookSortFilterProxyModel(const QString& type, QObject* parent)
        : QSortFilterProxyModel(parent)
        , m_type(type)
    {
        setDynamicSortFilter(true);
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const
    {
        auto model = sourceModel();
        auto label = model->index(row, AddressTableModel::Label, parent);

        if (m_type != AddressTableModel::All && model->data(label, AddressTableModel::TypeRole).toString() != m_type) {
            return false;
        }

        auto address = model->index(row, AddressTableModel::Address, parent);

        if (filterRegExp().indexIn(model->data(address).toString()) < 0 &&
            filterRegExp().indexIn(model->data(label).toString()) < 0) {
            return false;
        }

        return true;
    }
};

AddressBookPage::AddressBookPage(const PlatformStyle *platformStyle, Mode _mode, Tabs _tab, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AddressBookPage),
    model(0),
    mode(_mode),
    tab(_tab)
{
    ui->setupUi(this);

    if (platformStyle->getImagesOnButtons()) {
        ui->newAddress->setIcon(platformStyle->SingleColorIcon(":/icons/add"));
        ui->btnNewStakeAddress->setIcon(platformStyle->SingleColorIcon(":/icons/add"));
        ui->btnNewColdStakeAddress->setIcon(platformStyle->SingleColorIcon(":/icons/add"));
        ui->copyAddress->setIcon(platformStyle->SingleColorIcon(":/icons/editcopy"));
        ui->deleteAddress->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));
        ui->exportButton->setIcon(platformStyle->SingleColorIcon(":/icons/export"));
    }

    switch(tab)
    {
    case SendingTab:
        ui->labelExplanation->setText(tr("These are your Bitcoin Confidential addresses for sending payments. Always check the amount and the receiving address before sending coins."));
        ui->deleteAddress->setVisible(false);
        ui->btnNewStakeAddress->hide();
        ui->btnNewColdStakeAddress->hide();

        switch(mode)
        {
        case ForSelection:
            setWindowTitle(tr("Choose the address to send coins to"));
            connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(accept()));
            ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
            ui->tableView->setFocus();
            ui->closeButton->setText(tr("C&hoose"));
            ui->exportButton->hide();
            ui->newAddress->hide();
            ui->copyAddress->hide();
            break;
        case ForEditing:
            setWindowTitle(tr("Sending addresses"));
            break;
        }
        break;
    case ReceivingTab:
        ui->deleteAddress->setVisible(false);
        ui->btnNewStakeAddress->hide();
        ui->btnNewColdStakeAddress->hide();

        switch(mode)
        {
        case ForSelection:
            setWindowTitle(tr("Choose the address to receive coins with"));
            connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(accept()));
            ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
            ui->tableView->setFocus();
            ui->closeButton->setText(tr("C&hoose"));
            ui->exportButton->hide();
            ui->newAddress->hide();
            ui->copyAddress->hide();
            break;
        case ForEditing:
            setWindowTitle(tr("Receiving addresses"));
            ui->labelExplanation->setText(tr("These are your Bitcoin Confidential addresses for receiving payments. It is recommended to use a new receiving address for each transaction."));
            break;
        }
        break;
    case StakingTab:

        ui->newAddress->hide();
        ui->copyAddress->hide();
        ui->deleteAddress->hide();

        switch(mode)
        {
        case ForSelection:
            setWindowTitle(tr("Choose the Stake or ColdStake address"));
            ui->labelExplanation->setText(tr("These are your Bitcoin Confidential Stake and ColdStake addresses."));
            ui->btnNewStakeAddress->hide();
            ui->btnNewColdStakeAddress->hide();
            ui->exportButton->hide();
            connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(accept()));
            ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
            ui->tableView->setFocus();
            ui->closeButton->setText(tr("C&hoose"));
            break;
        case ForEditing:{

            CBitcoinAddress exampleStake, exampleColdStake;

            exampleStake.Set(CKeyID(uint160(GetRandHash().begin(), 20)));
            exampleColdStake.Set(CKeyID256(GetRandHash()));

            setWindowTitle(tr("Stake and ColdStake addresses"));
            ui->labelExplanation->setText(tr("These addresses cannot be used to receive coins from outside the wallet, such as payments or from exchanges.  Use the Receive tab to generate spending addresses.\n\n"
                                             "Stake addresses, starting with \"%1\", are used to convert spending funds into staking funds in the \"Convert to Staking\" tab. Funds in Stake addresses can be either used for hot staking or the activation of ColdStaking.\n\n"
                                             "ColdStake addresses, starting with \"%2\", are used to manually activate staking funds for ColdStaking. There is no need to generate ColdStake addresses when the automated ColdStaking activation from the Staking Status page is used.")
                                             .arg(exampleStake.ToString()[0]).arg(exampleColdStake.ToString()[0]));
            ui->closeButton->hide();
            break;
        }
        }break;
    }

    // Context menu actions
    QAction *copyAddressAction = new QAction(tr("&Copy Address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy &Label"), this);
    QAction *editAction = new QAction(tr("&Edit"), this);
    deleteAction = new QAction(ui->deleteAddress->text(), this);

    // Build context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(editAction);
//    if(tab == StakingTab)
//        contextMenu->addAction(deleteAction);
    contextMenu->addSeparator();

    // Connect signals for context menu actions
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(on_copyAddress_clicked()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(onCopyLabelAction()));
    connect(editAction, SIGNAL(triggered()), this, SLOT(onEditAction()));
    connect(deleteAction, SIGNAL(triggered()), this, SLOT(on_deleteAddress_clicked()));

    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(accept()));
}

AddressBookPage::~AddressBookPage()
{
    delete ui;
}

void AddressBookPage::setModel(AddressTableModel *_model)
{
    this->model = _model;
    if(!_model)
        return;


    auto type = tab == ReceivingTab ? AddressTableModel::Receive : StakingTab ? AddressTableModel::All : AddressTableModel::Send;
    proxyModel = new AddressBookSortFilterProxyModel(type, this);
    proxyModel->setSourceModel(_model);

    connect(ui->searchLineEdit, SIGNAL(textChanged(QString)), proxyModel, SLOT(setFilterWildcard(QString)));

    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    // Set column widths
    ui->tableView->horizontalHeader()->setSectionResizeMode(AddressTableModel::Label, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(AddressTableModel::Address, QHeaderView::ResizeToContents);

    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
        this, SLOT(selectionChanged()));

    // Select row for newly created address
    connect(_model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewAddress(QModelIndex,int,int)));

    selectionChanged();
}

void AddressBookPage::on_copyAddress_clicked()
{
    GUIUtil::copyEntryData(ui->tableView, AddressTableModel::Address);
}

void AddressBookPage::onCopyLabelAction()
{
    GUIUtil::copyEntryData(ui->tableView, AddressTableModel::Label);
}

void AddressBookPage::onEditAction()
{
    if(!model)
        return;

    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList indexes = ui->tableView->selectionModel()->selectedRows();
    if(indexes.isEmpty())
        return;

    EditAddressDialog dlg(
        tab == SendingTab ?
        EditAddressDialog::EditSendingAddress :
        EditAddressDialog::EditReceivingAddress, this);
    dlg.setModel(model);
    QModelIndex origIndex = proxyModel->mapToSource(indexes.at(0));
    dlg.loadRow(origIndex.row());
    dlg.exec();
}

void AddressBookPage::on_newAddress_clicked()
{
    if(!model)
        return;

    EditAddressDialog::Mode mode;

    if( tab == ReceivingTab ){
        mode = EditAddressDialog::NewReceivingAddress;
    }else if( tab == SendingTab ){
        mode = EditAddressDialog::NewSendingAddress;
    }else{
        return;
    }

    EditAddressDialog dlg(mode, this);
    dlg.setModel(model);
    if(dlg.exec())
    {
        newAddressToSelect = dlg.getAddress();
    }
}

void AddressBookPage::on_deleteAddress_clicked()
{
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    QModelIndexList indexes = table->selectionModel()->selectedRows();
    if(!indexes.isEmpty())
    {
        table->model()->removeRow(indexes.at(0).row());
    }
}

void AddressBookPage::selectionChanged()
{
    // Set button states based on selected tab and selection
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    if(table->selectionModel()->hasSelection())
    {
        switch(tab)
        {
        case SendingTab:
            // In sending tab, allow deletion of selection
            ui->deleteAddress->setEnabled(false);
            ui->deleteAddress->setVisible(false);
            deleteAction->setEnabled(true);
            break;
        case ReceivingTab:
            // Deleting receiving addresses, however, is not allowed
            ui->deleteAddress->setEnabled(false);
            ui->deleteAddress->setVisible(false);
            deleteAction->setEnabled(false);
            break;
        default:
            break;
        }
        ui->copyAddress->setEnabled(true);
    }
    else
    {
        ui->deleteAddress->setEnabled(false);
        ui->copyAddress->setEnabled(false);
    }
}

void AddressBookPage::done(int retval)
{
    QTableView *table = ui->tableView;
    if(!table->selectionModel() || !table->model())
        return;

    // Figure out which address was selected, and return it
    QModelIndexList indexes = table->selectionModel()->selectedRows(AddressTableModel::Address);

    for (const QModelIndex& index : indexes) {
        QVariant address = table->model()->data(index);
        returnValue = address.toString();
    }

    if(returnValue.isEmpty())
    {
        // If no address entry selected, return rejected
        retval = Rejected;
    }

    QDialog::done(retval);
}

void AddressBookPage::on_exportButton_clicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Export Address List"), QString(),
        tr("Comma separated file (*.csv)"), nullptr);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Label", AddressTableModel::Label, Qt::EditRole);
    writer.addColumn("Address", AddressTableModel::Address, Qt::EditRole);

    if(!writer.write()) {
        QMessageBox::critical(this, tr("Exporting Failed"),
            tr("There was an error trying to save the address list to %1. Please try again.").arg(filename));
    }
}

void AddressBookPage::on_btnNewStakeAddress_clicked()
{
    if(!model)
        return;

    EditAddressDialog dlg(EditAddressDialog::NewStakeAddress, this);
    dlg.setModel(model);
    if(dlg.exec())
    {
        newAddressToSelect = dlg.getAddress();
    }
}

void AddressBookPage::on_btnNewColdStakeAddress_clicked()
{
    if(!model)
        return;

    EditAddressDialog dlg(EditAddressDialog::NewColdStakeAddress, this);
    dlg.setModel(model);
    if(dlg.exec())
    {
        newAddressToSelect = dlg.getAddress();
    }
}

void AddressBookPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void AddressBookPage::selectNewAddress(const QModelIndex &parent, int begin, int /*end*/)
{
    QModelIndex idx = proxyModel->mapFromSource(model->index(begin, AddressTableModel::Address, parent));
    if(idx.isValid() && (idx.data(Qt::EditRole).toString() == newAddressToSelect))
    {
        // Select row of newly created address, once
        ui->tableView->setFocus();
        ui->tableView->selectRow(idx.row());
        newAddressToSelect.clear();
    }
}
