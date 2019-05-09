// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionview.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/csvmodelwriter.h>
#include <qt/editaddressdialog.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/sendcoinsdialog.h>
#include <qt/transactiondescdialog.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactionrecord.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>

#include <ui_interface.h>

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QScrollBar>
#include <QSignalMapper>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

TransactionView::TransactionView(const PlatformStyle *platformStyle, bool fStaking, QWidget *parent) :
    QWidget(parent), model(0), transactionProxyModel(0),
    transactionView(0), abandonAction(0), bumpFeeAction(0), columnResizingFixer(0), fStaking(fStaking)
{
    // Build filter row
    setContentsMargins(0,0,0,0);

    QHBoxLayout *hlayout = new QHBoxLayout();
    hlayout->setContentsMargins(0,0,0,0);

    hlayout->setSpacing(5);

    watchOnlyWidget = new QComboBox(this);
    watchOnlyWidget->setFixedWidth(40);
    watchOnlyWidget->addItem(tr("All"), TransactionFilterProxy::WatchOnlyFilter_All);
    watchOnlyWidget->addItem(platformStyle->BitcoinCColorIcon(":/icons/eye_plus"), "", TransactionFilterProxy::WatchOnlyFilter_Yes);
    watchOnlyWidget->addItem(platformStyle->BitcoinCColorIcon(":/icons/eye_minus"), "", TransactionFilterProxy::WatchOnlyFilter_No);
    hlayout->addWidget(watchOnlyWidget);

    dateWidget = new QComboBox(this);
    if (platformStyle->getUseExtraSpacing()) {
        dateWidget->setFixedWidth(131);
    } else {
        dateWidget->setFixedWidth(130);
    }
    dateWidget->addItem(tr("All"), All);
    dateWidget->addItem(tr("Today"), Today);
    dateWidget->addItem(tr("This week"), ThisWeek);
    dateWidget->addItem(tr("This month"), ThisMonth);
    dateWidget->addItem(tr("Last month"), LastMonth);
    dateWidget->addItem(tr("This year"), ThisYear);
    dateWidget->addItem(tr("Range..."), Range);
    hlayout->addWidget(dateWidget);

    if( !fStaking ){

        typeWidget = new QComboBox(this);
        if (platformStyle->getUseExtraSpacing()) {
            typeWidget->setFixedWidth(131);
        } else {
            typeWidget->setFixedWidth(130);
        }

        typeWidget->addItem(tr("All"), TransactionFilterProxy::ALL_TYPES);
        typeWidget->addItem(tr("Received with"), TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) |
                                            TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther));
        typeWidget->addItem(tr("Sent to"), TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) |
                                      TransactionFilterProxy::TYPE(TransactionRecord::SendToOther));
        typeWidget->addItem(tr("To yourself"), TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf));
        typeWidget->addItem(tr("Other"), TransactionFilterProxy::TYPE(TransactionRecord::Other));
        typeWidget->addItem(tr("Airdrop"), TransactionFilterProxy::TYPE(TransactionRecord::Generated));
        hlayout->addWidget(typeWidget);
    }

    search_widget = new QLineEdit(this);
    search_widget->setPlaceholderText(tr("Enter address, transaction id, or label to search"));
    hlayout->addWidget(search_widget);

    if( !fStaking ){
        amountWidget = new QLineEdit(this);
        amountWidget->setPlaceholderText(tr("Min amount"));
        if (platformStyle->getUseExtraSpacing()) {
            amountWidget->setFixedWidth(97);
        } else {
            amountWidget->setFixedWidth(100);
        }
        amountWidget->setValidator(new QDoubleValidator(0, 1e20, 8, this));
        hlayout->addWidget(amountWidget);
    }
    // Delay before filtering transactions in ms
    static const int input_filter_delay = 200;

    QTimer* amount_typing_delay = new QTimer(this);
    amount_typing_delay->setSingleShot(true);
    amount_typing_delay->setInterval(input_filter_delay);

    QTimer* prefix_typing_delay = new QTimer(this);
    prefix_typing_delay->setSingleShot(true);
    prefix_typing_delay->setInterval(input_filter_delay);

    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0,0,0,0);
    vlayout->setSpacing(0);

    QWidget *spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    spacer->setFixedHeight(10);

    QTableView *view = new QTableView(this);
    vlayout->addLayout(hlayout);
    vlayout->addWidget(createDateRangeWidget());
    vlayout->addWidget(spacer);
    vlayout->addWidget(view);
    vlayout->setSpacing(0);

    // Always show scroll bar
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setTabKeyNavigation(false);
    view->setContextMenuPolicy(Qt::CustomContextMenu);

    view->installEventFilter(this);

    transactionView = view;
    if( fStaking ){
        transactionView->setObjectName("transactionView_staking");
    }else{
        transactionView->setObjectName("transactionView");
    }

    transactionView->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);

    // Actions
    abandonAction = new QAction(tr("Abandon transaction"), this);
    bumpFeeAction = new QAction(tr("Increase transaction fee"), this);
    bumpFeeAction->setObjectName("bumpFeeAction");
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
    QAction *copyTxIDAction = new QAction(tr("Copy transaction ID"), this);
    QAction *copyTxHexAction = new QAction(tr("Copy raw transaction"), this);
    QAction *copyTxPlainText = new QAction(tr("Copy full transaction details"), this);
    QAction *editLabelAction = new QAction(tr("Edit label"), this);
    QAction *showDetailsAction = new QAction(tr("Show transaction details"), this);

    contextMenu = new QMenu(this);
    contextMenu->setObjectName("contextMenu");
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyTxIDAction);
    contextMenu->addAction(copyTxHexAction);
    contextMenu->addAction(copyTxPlainText);
    contextMenu->addAction(showDetailsAction);
    contextMenu->addSeparator();
    contextMenu->addAction(bumpFeeAction);
    contextMenu->addAction(abandonAction);
    contextMenu->addAction(editLabelAction);

    mapperThirdPartyTxUrls = new QSignalMapper(this);

    // Connect actions
    connect(mapperThirdPartyTxUrls, SIGNAL(mapped(QString)), this, SLOT(openThirdPartyTxUrl(QString)));

    connect(dateWidget, SIGNAL(activated(int)), this, SLOT(chooseDate(int)));
    if( !fStaking ){
        connect(typeWidget, SIGNAL(activated(int)), this, SLOT(chooseType(int)));
    }
    connect(watchOnlyWidget, SIGNAL(activated(int)), this, SLOT(chooseWatchonly(int)));
    if( !fStaking ){
        connect(amountWidget, SIGNAL(textChanged(QString)), amount_typing_delay, SLOT(start()));
        connect(amount_typing_delay, SIGNAL(timeout()), this, SLOT(changedAmount()));
    }
    connect(search_widget, SIGNAL(textChanged(QString)), prefix_typing_delay, SLOT(start()));
    connect(prefix_typing_delay, SIGNAL(timeout()), this, SLOT(changedSearch()));

    connect(view, SIGNAL(doubleClicked(QModelIndex)), this, SIGNAL(doubleClicked(QModelIndex)));
    connect(view, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    connect(bumpFeeAction, SIGNAL(triggered()), this, SLOT(bumpFee()));
    connect(abandonAction, SIGNAL(triggered()), this, SLOT(abandonTx()));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyTxIDAction, SIGNAL(triggered()), this, SLOT(copyTxID()));
    connect(copyTxHexAction, SIGNAL(triggered()), this, SLOT(copyTxHex()));
    connect(copyTxPlainText, SIGNAL(triggered()), this, SLOT(copyTxPlainText()));
    connect(editLabelAction, SIGNAL(triggered()), this, SLOT(editLabel()));
    connect(showDetailsAction, SIGNAL(triggered()), this, SLOT(showDetails()));
    connect(transactionView, SIGNAL(clicked(QModelIndex)), this, SLOT(computeSelectedSum()));
}

void TransactionView::setModel(WalletModel *_model)
{
    this->model = _model;
    if(_model)
    {
        transactionProxyModel = new TransactionFilterProxy(this);
        if( fStaking ){
            transactionProxyModel->setSourceModel(_model->getStakingTransactionTableModel());
            connect(_model->getStakingTransactionTableModel(), SIGNAL(transactionsChanged()), this, SLOT(computeTotalSum()));
        }else{
            transactionProxyModel->setSourceModel(_model->getTransactionTableModel());
        }
        transactionProxyModel->setDynamicSortFilter(true);
        transactionProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

        transactionProxyModel->setSortRole(Qt::EditRole);

        transactionView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        transactionView->setModel(transactionProxyModel);
        transactionView->setAlternatingRowColors(true);
        transactionView->setSelectionBehavior(QAbstractItemView::SelectRows);
        transactionView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        if( fStaking ){
            transactionView->sortByColumn(TransactionTableModel::DateStaking, Qt::DescendingOrder);
        }else{
            transactionView->sortByColumn(TransactionTableModel::Date, Qt::DescendingOrder);
        }
        transactionView->setSortingEnabled(true);
        transactionView->verticalHeader()->hide();

        if( fStaking ){
            transactionView->setColumnWidth(TransactionTableModel::StatusStaking, STATUS_COLUMN_WIDTH);
            transactionView->setColumnWidth(TransactionTableModel::WatchonlyStaking, WATCHONLY_COLUMN_WIDTH);
            transactionView->setColumnWidth(TransactionTableModel::DateStaking, DATE_COLUMN_WIDTH);
            transactionView->setColumnWidth(TransactionTableModel::AmountStaking, AMOUNT_MINIMUM_COLUMN_WIDTH);
        }else{
            transactionView->setColumnWidth(TransactionTableModel::Status, STATUS_COLUMN_WIDTH);
            transactionView->setColumnWidth(TransactionTableModel::Watchonly, WATCHONLY_COLUMN_WIDTH);
            transactionView->setColumnWidth(TransactionTableModel::Date, DATE_COLUMN_WIDTH);
            transactionView->setColumnWidth(TransactionTableModel::Type, TYPE_COLUMN_WIDTH);
            transactionView->setColumnWidth(TransactionTableModel::TypeIn, IN_TYPE_COLUMN_WIDTH);
            transactionView->setColumnWidth(TransactionTableModel::TypeOut, OUT_TYPE_COLUMN_WIDTH);
            transactionView->setColumnWidth(TransactionTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);
        }

        connect(transactionView->selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this, SLOT(computeSelectedSum()));

        if( fStaking ){
            columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(transactionView, AMOUNT_MINIMUM_COLUMN_WIDTH, MINIMUM_COLUMN_WIDTH, this, 2);
        }else{
            columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(transactionView, AMOUNT_MINIMUM_COLUMN_WIDTH, MINIMUM_COLUMN_WIDTH, this, 4);
        }

        if (_model->getOptionsModel())
        {
            // Add third party transaction URLs to context menu
            QStringList listUrls = _model->getOptionsModel()->getThirdPartyTxUrls().split("|", QString::SkipEmptyParts);
            for (int i = 0; i < listUrls.size(); ++i)
            {
                QString host = QUrl(listUrls[i].trimmed(), QUrl::StrictMode).host();
                if (!host.isEmpty())
                {
                    QAction *thirdPartyTxUrlAction = new QAction(host, this); // use host as menu item label
                    if (i == 0)
                        contextMenu->addSeparator();
                    contextMenu->addAction(thirdPartyTxUrlAction);
                    connect(thirdPartyTxUrlAction, SIGNAL(triggered()), mapperThirdPartyTxUrls, SLOT(map()));
                    mapperThirdPartyTxUrls->setMapping(thirdPartyTxUrlAction, listUrls[i].trimmed());
                }
            }
        }

        // show/hide column Watch-only
        updateWatchOnlyColumn(_model->wallet().haveWatchOnly());

        // Watch-only signal
        connect(_model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyColumn(bool)));

        computeSelectedSum();
        if( fStaking ){
            computeTotalSum();
            chooseDate(3);
            dateWidget->setCurrentIndex(3);
        }

        this->resize(this->geometry().width(), this->geometry().height());
    }
}

void TransactionView::chooseDate(int idx)
{
    if (!transactionProxyModel) return;
    QDate current = QDate::currentDate();
    dateRangeWidget->setVisible(false);
    switch(dateWidget->itemData(idx).toInt())
    {
    case All:
        transactionProxyModel->setDateRange(
                TransactionFilterProxy::MIN_DATE,
                TransactionFilterProxy::MAX_DATE);
        break;
    case Today:
        transactionProxyModel->setDateRange(
                QDateTime(current),
                TransactionFilterProxy::MAX_DATE);
        break;
    case ThisWeek: {
        // Find last Monday
        QDate startOfWeek = current.addDays(-(current.dayOfWeek()-1));
        transactionProxyModel->setDateRange(
                QDateTime(startOfWeek),
                TransactionFilterProxy::MAX_DATE);

        } break;
    case ThisMonth:
        transactionProxyModel->setDateRange(
                QDateTime(QDate(current.year(), current.month(), 1)),
                TransactionFilterProxy::MAX_DATE);
        break;
    case LastMonth:
        transactionProxyModel->setDateRange(
                QDateTime(QDate(current.year(), current.month(), 1).addMonths(-1)),
                QDateTime(QDate(current.year(), current.month(), 1)));
        break;
    case ThisYear:
        transactionProxyModel->setDateRange(
                QDateTime(QDate(current.year(), 1, 1)),
                TransactionFilterProxy::MAX_DATE);
        break;
    case Range:
        dateRangeWidget->setVisible(true);
        dateRangeChanged();
        break;
    }

    computeTotalSum();
}

void TransactionView::chooseType(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setTypeFilter(
        typeWidget->itemData(idx).toInt());
}

void TransactionView::chooseWatchonly(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setWatchOnlyFilter(
        static_cast<TransactionFilterProxy::WatchOnlyFilter>(watchOnlyWidget->itemData(idx).toInt()));
}

void TransactionView::changedSearch()
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setSearchString(search_widget->text());

    computeTotalSum();
}

void TransactionView::changedAmount()
{
    if(!transactionProxyModel)
        return;
    CAmount amount_parsed = 0;
    if (BitcoinUnits::parse(model->getOptionsModel()->getDisplayUnit(), amountWidget->text(), &amount_parsed)) {
        transactionProxyModel->setMinAmount(amount_parsed);
    }
    else
    {
        transactionProxyModel->setMinAmount(0);
    }
}

void TransactionView::exportClicked()
{
    if (!model || !model->getOptionsModel()) {
        return;
    }

    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Export Transaction History"), QString(),
        tr("Comma separated file (*.csv)"), nullptr);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(transactionProxyModel);
    writer.addColumn(tr("Confirmed"), 0, TransactionTableModel::ConfirmedRole);
    if (model->wallet().haveWatchOnly())
        writer.addColumn(tr("Watch-only"), TransactionTableModel::Watchonly);
    writer.addColumn(tr("Date"), 0, TransactionTableModel::DateRole);
    if( !fStaking ){
        writer.addColumn(tr("Type"), TransactionTableModel::Type, Qt::EditRole);
    }
    writer.addColumn(tr("Label"), 0, TransactionTableModel::LabelRole);
    writer.addColumn(tr("Address"), 0, TransactionTableModel::AddressRole);
    writer.addColumn(BitcoinUnits::getAmountColumnTitle(model->getOptionsModel()->getDisplayUnit()), 0, TransactionTableModel::FormattedAmountRole);
    writer.addColumn(tr("ID"), 0, TransactionTableModel::TxHashRole);

    if(!writer.write()) {
        Q_EMIT message(tr("Exporting Failed"), tr("There was an error trying to save the transaction history to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
    }
    else {
        Q_EMIT message(tr("Exporting Successful"), tr("The transaction history was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void TransactionView::contextualMenu(const QPoint &point)
{
    QModelIndex index = transactionView->indexAt(point);
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if (selection.empty())
        return;

    // check if transaction can be abandoned, disable context menu action in case it doesn't
    uint256 hash;
    hash.SetHex(selection.at(0).data(TransactionTableModel::TxHashRole).toString().toStdString());
    abandonAction->setEnabled(model->wallet().transactionCanBeAbandoned(hash));
    bumpFeeAction->setEnabled(model->wallet().transactionCanBeBumped(hash));

    if(index.isValid())
    {
        contextMenu->popup(transactionView->viewport()->mapToGlobal(point));
    }
}

void TransactionView::abandonTx()
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);

    // get the hash from the TxHashRole (QVariant / QString)
    uint256 hash;
    QString hashQStr = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
    hash.SetHex(hashQStr.toStdString());

    // Abandon the wallet transaction over the walletModel
    model->wallet().abandonTransaction(hash);

    // Update the table
    if( fStaking ){
        model->getStakingTransactionTableModel()->updateTransaction(hashQStr, CT_UPDATED, false);
    }else{
        model->getTransactionTableModel()->updateTransaction(hashQStr, CT_UPDATED, false);
    }
}

void TransactionView::bumpFee()
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);

    // get the hash from the TxHashRole (QVariant / QString)
    uint256 hash;
    QString hashQStr = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
    hash.SetHex(hashQStr.toStdString());

    // Bump tx fee over the walletModel
    if (model->bumpFee(hash)) {
        // Update the table
        if( fStaking ){
            model->getStakingTransactionTableModel()->updateTransaction(hashQStr, CT_UPDATED, true);
        }else{
            model->getTransactionTableModel()->updateTransaction(hashQStr, CT_UPDATED, true);
        }

    }
}

void TransactionView::copyAddress()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::AddressRole);
}

void TransactionView::copyLabel()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::LabelRole);
}

void TransactionView::copyAmount()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::FormattedAmountRole);
}

void TransactionView::copyTxID()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxHashRole);
}

void TransactionView::copyTxHex()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxHexRole);
}

void TransactionView::copyTxPlainText()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxPlainTextRole);
}

void TransactionView::editLabel()
{
    if(!transactionView->selectionModel() ||!model)
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        AddressTableModel *addressBook = model->getAddressTableModel();
        if(!addressBook)
            return;
        QString address = selection.at(0).data(TransactionTableModel::AddressRole).toString();
        if(address.isEmpty())
        {
            // If this transaction has no associated address, exit
            return;
        }
        // Is address in address book? Address book can miss address when a transaction is
        // sent from outside the UI.
        int idx = addressBook->lookupAddress(address);
        if(idx != -1)
        {
            // Edit sending / receiving address
            QModelIndex modelIdx = addressBook->index(idx, 0, QModelIndex());
            // Determine type of address, launch appropriate editor dialog type
            QString type = modelIdx.data(AddressTableModel::TypeRole).toString();

            EditAddressDialog dlg(
                type == AddressTableModel::Receive
                ? EditAddressDialog::EditReceivingAddress
                : EditAddressDialog::EditSendingAddress, this);
            dlg.setModel(addressBook);
            dlg.loadRow(idx);
            dlg.exec();
        }
        else
        {
            // Add sending address
            EditAddressDialog dlg(EditAddressDialog::NewSendingAddress,
                this);
            dlg.setModel(addressBook);
            dlg.setAddress(address);
            dlg.exec();
        }
    }
}

void TransactionView::showDetails()
{
    if(!transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        TransactionDescDialog *dlg = new TransactionDescDialog(selection.at(0));
        dlg->setStyleSheet(GUIUtil::loadStyleSheet());
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
}

void TransactionView::openThirdPartyTxUrl(QString url)
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if(!selection.isEmpty())
         QDesktopServices::openUrl(QUrl::fromUserInput(url.replace("%s", selection.at(0).data(TransactionTableModel::TxHashRole).toString())));
}

QWidget *TransactionView::createDateRangeWidget()
{
    dateRangeWidget = new QFrame();
    dateRangeWidget->setFrameStyle(QFrame::Panel | QFrame::Raised);
    dateRangeWidget->setContentsMargins(1,1,1,1);
    QHBoxLayout *layout = new QHBoxLayout(dateRangeWidget);
    layout->setContentsMargins(0,0,0,0);
    layout->addSpacing(23);
    layout->addWidget(new QLabel(tr("Range:")));

    dateFrom = new QDateTimeEdit(this);
    dateFrom->setDisplayFormat("dd/MM/yy");
    dateFrom->setCalendarPopup(true);
    dateFrom->setMinimumWidth(100);
    dateFrom->setDate(QDate::currentDate().addDays(-7));
    layout->addWidget(dateFrom);
    layout->addWidget(new QLabel(tr("to")));

    dateTo = new QDateTimeEdit(this);
    dateTo->setDisplayFormat("dd/MM/yy");
    dateTo->setCalendarPopup(true);
    dateTo->setMinimumWidth(100);
    dateTo->setDate(QDate::currentDate());
    layout->addWidget(dateTo);
    layout->addStretch();

    // Hide by default
    dateRangeWidget->setVisible(false);

    // Notify on change
    connect(dateFrom, SIGNAL(dateChanged(QDate)), this, SLOT(dateRangeChanged()));
    connect(dateTo, SIGNAL(dateChanged(QDate)), this, SLOT(dateRangeChanged()));

    return dateRangeWidget;
}

void TransactionView::dateRangeChanged()
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setDateRange(
            QDateTime(dateFrom->date()),
            QDateTime(dateTo->date()).addDays(1));
}

void TransactionView::focusTransaction(const QModelIndex &idx)
{
    if(!transactionProxyModel)
        return;
    QModelIndex targetIdx = transactionProxyModel->mapFromSource(idx);
    transactionView->scrollTo(targetIdx);
    transactionView->setCurrentIndex(targetIdx);
    transactionView->setFocus();
}

void TransactionView::focusTransaction(const uint256& txid)
{
    if (!transactionProxyModel)
        return;

    QModelIndexList results;

    if( fStaking ){
        results = this->model->getStakingTransactionTableModel()->match(
                    this->model->getStakingTransactionTableModel()->index(0,0),
                    TransactionTableModel::TxHashRole,
                    QString::fromStdString(txid.ToString()), -1);
    }else{
        results = this->model->getTransactionTableModel()->match(
                    this->model->getTransactionTableModel()->index(0,0),
                    TransactionTableModel::TxHashRole,
                    QString::fromStdString(txid.ToString()), -1);
    }

    transactionView->setFocus();
    transactionView->selectionModel()->clearSelection();
    for (const QModelIndex& index : results) {
        const QModelIndex targetIndex = transactionProxyModel->mapFromSource(index);
        transactionView->selectionModel()->select(
            targetIndex,
            QItemSelectionModel::Rows | QItemSelectionModel::Select);
        // Called once per destination to ensure all results are in view, unless
        // transactions are not ordered by (ascending or descending) date.
        transactionView->scrollTo(targetIndex);
        // scrollTo() does not scroll far enough the first time when transactions
        // are ordered by ascending date.
        if (index == results[0]) transactionView->scrollTo(targetIndex);
    }
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void TransactionView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if( fStaking ){ 
        columnResizingFixer->stretchColumnWidth(TransactionTableModel::ToAddressStaking);
    }else{
        columnResizingFixer->stretchColumnWidth(TransactionTableModel::ToAddress);
    }
}

// Need to override default Ctrl+C action for amount as default behaviour is just to copy DisplayRole text
bool TransactionView::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_C && ke->modifiers().testFlag(Qt::ControlModifier))
        {
             GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxPlainTextRole);
             return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// show/hide column Watch-only
void TransactionView::updateWatchOnlyColumn(bool fHaveWatchOnly)
{
    watchOnlyWidget->setVisible(fHaveWatchOnly);
    transactionView->setColumnHidden(TransactionTableModel::Watchonly, !fHaveWatchOnly);
}

/** Compute sum of all selected transactions */
void TransactionView::computeSelectedSum()
{
    qint64 amount = 0;

    if(!transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();

    Q_FOREACH (const QModelIndex &index, selection){
        amount += index.data(TransactionTableModel::AmountRole).toLongLong();
    }
    Q_EMIT selectedAmount(amount);
}

/** Compute sum of all selected transactions */
void TransactionView::computeTotalSum()
{
    qint64 nAmount = 0, nAmountVisible = 0;
    int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();
    if(!model || (fStaking && !model->getStakingTransactionTableModel()) || (!fStaking && !model->getTransactionTableModel()))
        return;

    TransactionTableModel *txModel;

    if( fStaking ){
        txModel = model->getStakingTransactionTableModel();
    }else{
        txModel = model->getTransactionTableModel();
    }

    int nColumn, nRowsVisible = 0;
    int nStatus = -1;

    if( fStaking ){
        nColumn = TransactionTableModel::AmountStaking;
    }else{
        nColumn = TransactionTableModel::Amount;
    }

    for ( int nRow = 0; nRow < txModel->rowCount(QModelIndex()); ++nRow ){
        QModelIndex index = txModel->index(nRow, nColumn);

        nStatus = index.data(TransactionTableModel::Status).toLongLong();

        // Don't count orphans
        if( fStaking && nStatus == TransactionStatus::Abandoned ){
            continue;
        }

        nAmount += index.data(TransactionTableModel::AmountRole).toLongLong();
        QModelIndex indexFiltered = transactionProxyModel->mapFromSource(index);

        if( !indexFiltered.isValid() ){
            continue;
        }
        ++nRowsVisible;
        nAmountVisible += indexFiltered.data(TransactionTableModel::AmountRole).toLongLong();
    }

    bool fShowSign = fStaking ? false : true;

    QString strCount = QString::number(txModel->rowCount(QModelIndex()));
    QString strCountVisible = QString::number(nRowsVisible);
    QString strAmount(BitcoinUnits::formatWithUnit(nDisplayUnit, nAmount, fShowSign, BitcoinUnits::separatorAlways));
    QString strAmountVisible(BitcoinUnits::formatWithUnit(nDisplayUnit, nAmountVisible, fShowSign   , BitcoinUnits::separatorAlways));

    Q_EMIT totalAmount(strCountVisible, strAmountVisible, strCount, strAmount);
}
