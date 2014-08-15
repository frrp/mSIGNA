///////////////////////////////////////////////////////////////////////////////
//
// CoinVault
//
// mainwindow.cpp
//
// Copyright (c) 2013-2014 Eric Lombrozo
//
// All Rights Reserved.

#include <QtWidgets>
#include <QDir>
#include <QTreeWidgetItem>
#include <QTabWidget>
#include <QInputDialog>
#include <QItemSelectionModel>

#include "entropysource.h"

#include "settings.h"
//#include "versioninfo.h"
//#include "copyrightinfo.h"

#include "stylesheets.h"

#include "mainwindow.h"

// Random
#include "entropysource.h"

// Coin scripts
#include <CoinQ/CoinQ_script.h>

// Models/Views
#include "accountmodel.h"
#include "accountview.h"
#include "keychainmodel.h"
#include "keychainview.h"
#include "txmodel.h"
#include "txview.h"

// Actions
#include "txactions.h"

// Dialogs
#include "aboutdialog.h"
#include "entropydialog.h"
#include "newkeychaindialog.h"
#include "quicknewaccountdialog.h"
#include "newaccountdialog.h"
#include "rawtxdialog.h"
#include "createtxdialog.h"
#include "accounthistorydialog.h"
#include "scriptdialog.h"
#include "requestpaymentdialog.h"
#include "networksettingsdialog.h"
#include "keychainbackupdialog.h"
#include "passphrasedialog.h"
#include "currencyunitdialog.h"
//#include "resyncdialog.h"

// Logging
#include "severitylogger.h"

// Coin Parameters
#include "coinparams.h"

// File System
#include "docdir.h"

boost::mutex repaintMutex;

using namespace CoinQ::Script;
using namespace std;

MainWindow::MainWindow() :
    licenseAccepted(false),
    synchedVault(getCoinParams()),
    //networkSync(getCoinParams()),
    syncHeight(0),
    bestHeight(0),
    networkState(NETWORK_STATE_STOPPED),
    accountModel(nullptr),
    keychainModel(nullptr),
    txActions(nullptr)
{
    createActions();
    createToolBars();
    createStatusBar();


    //setCurrentFile("");
    setUnifiedTitleAndToolBarOnMac(true);

    // Keychain tab page
    keychainModel = new KeychainModel();
    keychainView = new KeychainView();
    keychainView->setModel(keychainModel);
    keychainView->setSelectionMode(KeychainView::MultiSelection);
    keychainView->setMenu(keychainMenu);

    keychainSelectionModel = keychainView->selectionModel();
    connect(keychainSelectionModel, &QItemSelectionModel::currentChanged,
            this, &MainWindow::updateCurrentKeychain);
    connect(keychainSelectionModel, &QItemSelectionModel::selectionChanged,
            this, &MainWindow::updateSelectedKeychains);

    //synchedVault.subscribeKeychainUnlocked([this](const std::string& /*keychain_name*/) { keychainModel->update(); });
    //synchedVault.subscribeKeychainLocked([this](const std::string& /*keychain_name*/) { keychainModel->update(); });

    // Account tab page
    accountModel = new AccountModel(synchedVault);
    accountView = new AccountView();
    accountView->setModel(accountModel);
    accountView->updateColumns();
    connect(accountView, SIGNAL(updateModel()), accountModel, SLOT(update()));

/*
    qRegisterMetaType<bytes_t>("bytes_t");
    connect(accountModel, SIGNAL(newTx(const bytes_t&)), this, SLOT(newTx(const bytes_t&)));
    connect(accountModel, SIGNAL(newBlock(const bytes_t&, int)), this, SLOT(newBlock(const bytes_t&, int)));
    connect(accountModel, &AccountModel::updateSyncHeight, [this](int height) { emit updateSyncHeight(height); });
*/

    connect(accountModel, SIGNAL(error(const QString&)), this, SLOT(showError(const QString&)));

    synchedVault.subscribeBestHeightChanged([this](uint32_t height) { emit updateBestHeight((int)height); });
    synchedVault.subscribeSyncHeightChanged([this](uint32_t height) { emit updateSyncHeight((int)height); });

    synchedVault.subscribeStatusChanged([this](CoinDB::SynchedVault::status_t status) {
        switch (status)
        {
        case CoinDB::SynchedVault::STOPPED:
            networkStopped();
            break;
        case CoinDB::SynchedVault::STARTING:
            networkStarted();
            break;
        case CoinDB::SynchedVault::SYNCHING_HEADERS:
            fetchingHeaders();
            break;
        case CoinDB::SynchedVault::SYNCHING_BLOCKS:
            fetchingBlocks();
            break;
        case CoinDB::SynchedVault::SYNCHED:
            blocksSynched();
            break;
        default:
            break;
        }
    });

    synchedVault.subscribeConnectionError([this](const std::string& error, int /*code*/) { emit signal_error(tr("Connection error: ") + QString::fromStdString(error)); });
    //synchedVault.subscribeVaultError([this](const std::string& error, int /*code*/) { emit signal_error(tr("Vault error: ") + QString::fromStdString(error)); });
    connect(this, SIGNAL(signal_error(const QString&)), this, SLOT(showError(const QString&)));

    synchedVault.subscribeTxInserted([this](std::shared_ptr<CoinDB::Tx> /*tx*/) { if (isSynched()) emit signal_newTx(); });
    synchedVault.subscribeTxUpdated([this](std::shared_ptr<CoinDB::Tx> /*tx*/) { if (isSynched()) emit signal_newTx(); });
    synchedVault.subscribeMerkleBlockInserted([this](std::shared_ptr<CoinDB::MerkleBlock> /*merkleblock*/) { emit signal_newBlock(); });

    connect(this, SIGNAL(signal_newTx()), this, SLOT(newTx()));
    connect(this, SIGNAL(signal_newBlock()), this, SLOT(newBlock()));
    connect(this, SIGNAL(signal_refreshAccounts()), this, SLOT(refreshAccounts()));

    accountSelectionModel = accountView->selectionModel();
    connect(accountSelectionModel, &QItemSelectionModel::currentChanged,
            this, &MainWindow::updateCurrentAccount);
    connect(accountSelectionModel, &QItemSelectionModel::selectionChanged,
            this, &MainWindow::updateSelectedAccounts);

    connect(keychainModel, SIGNAL(error(const QString&)), this, SLOT(showError(const QString&)));

    // Transaction tab page
    txModel = new TxModel();
    connect(txModel, SIGNAL(txSigned(const QString&)), this, SLOT(showUpdate(const QString&)));

    txView = new TxView();
    txView->setModel(txModel);
    txActions = new TxActions(txModel, txView, accountModel, keychainModel, &synchedVault);
    connect(txActions, SIGNAL(error(const QString&)), this, SLOT(showError(const QString&)));

    // Menus
    createMenus();
    accountView->setMenu(accountMenu);
    txView->setMenu(txActions->getMenu());

    tabWidget = new QTabWidget();
    tabWidget->addTab(accountView, tr("Accounts"));
    tabWidget->addTab(txView, tr("Transactions"));
    tabWidget->addTab(keychainView, tr("Keychains"));
    setCentralWidget(tabWidget);

    requestPaymentDialog = new RequestPaymentDialog(accountModel, accountView);

    // Vault open and close
    synchedVault.subscribeVaultOpened([this](CoinDB::Vault* vault) { emit vaultOpened(vault); });
    synchedVault.subscribeVaultClosed([this]() { emit vaultClosed(); });

    connect(this, &MainWindow::vaultOpened, [this](CoinDB::Vault* vault) {
        keychainModel->setVault(vault);
        keychainModel->update();
        keychainView->updateColumns();

        accountModel->update();
        accountView->updateColumns();

        txModel->setVault(vault);
        txModel->update();
        txView->updateColumns();

        selectAccount(0);

        // TODO: prompt user to unlock chain codes.
        synchedVault.getVault()->unlockChainCodes(uchar_vector("1234"));
    });
    connect(this, &MainWindow::vaultClosed, [this]() {
        keychainModel->setVault(nullptr);
        keychainModel->update();

        accountModel->update();

        txModel->setVault(nullptr);
        txModel->update();
        txView->updateColumns();

        updateStatusMessage(tr("Closed vault"));
    });

    // status updates
    connect(this, &MainWindow::status, [this](const QString& message) { updateStatusMessage(message); });
    connect(this, &MainWindow::updateSyncHeight, [this](int height) {
        LOGGER(debug) << "MainWindow::updateBestHeight emitted. New best height: " << height << std::endl;
        syncHeight = height;
        updateSyncLabel();
    });
    connect(this, &MainWindow::updateBestHeight, [this](int height) {
        LOGGER(debug) << "MainWindow::updateBestHeight emitted. New best height: " << height << std::endl;
        bestHeight = height;
        updateSyncLabel();
    });

    connect(this, &MainWindow::signal_currencyUnitChanged, [this]() {
        accountModel->update();
        accountView->updateColumns();
        txModel->update();
        txView->updateColumns();
    });

    setAcceptDrops(true);

    loadSettings();
}

void MainWindow::loadHeaders()
{
    synchedVault.loadHeaders(blockTreeFile.toStdString(), false,
        [this](const CoinQBlockTreeMem& blockTree) {
            std::stringstream progress;
            progress << "Height: " << blockTree.getBestHeight() << " / " << "Total Work: " << blockTree.getTotalWork().getDec();
            emit headersLoadProgress(QString::fromStdString(progress.str()));
            return true;
        });
    emit updateBestHeight(synchedVault.getBestHeight());
}

void MainWindow::tryConnect()
{
    if (!autoConnect) return;
    startNetworkSync();
}

void MainWindow::updateStatusMessage(const QString& message)
{
    LOGGER(debug) << "MainWindow::updateStatusMessage - " << message.toStdString() << std::endl;
//    boost::lock_guard<boost::mutex> lock(repaintMutex);
//    statusBar()->showMessage(message);
}

void MainWindow::closeEvent(QCloseEvent * /*event*/)
{
    synchedVault.stopSync();
    saveSettings();
    joinEntropyThread();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    //if (event->mimeData()->hasFormat("text/plain"))
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        for (auto& url: mimeData->urls()) {
            if (url.isLocalFile()) {
                processFile(url.toLocalFile());
            }
        }
        return;
    }

    QMessageBox::information(this, tr("Drop Event"), mimeData->text());
}

void MainWindow::updateFonts(int fontSize)
{
    switch (fontSize)
    {
    case SMALL_FONTS:
        smallFontsAction->setChecked(true);
        setStyleSheet(getSmallFontsStyle());
        break;

    case MEDIUM_FONTS:
        mediumFontsAction->setChecked(true);
        setStyleSheet(getMediumFontsStyle());
        break;

    case LARGE_FONTS:
        largeFontsAction->setChecked(true);
        setStyleSheet(getLargeFontsStyle());
        break;

    default:
        return;
    }

    accountView->updateColumns();
    keychainView->updateColumns();
    txView->updateColumns();
            
    QSettings settings("Ciphrex", getDefaultSettings().getSettingsRoot());
    settings.setValue("fontsize", fontSize);
}

void MainWindow::updateSyncLabel()
{
    syncLabel->setText(QString::number(syncHeight) + "/" + QString::number(bestHeight));
}

void MainWindow::updateNetworkState(network_state_t newState)
{
    if (newState != networkState)
    {
        networkState = newState;
        switch (networkState)
        {
        case NETWORK_STATE_STOPPED:
            networkStateLabel->setPixmap(*stoppedIcon);
            emit signal_refreshAccounts();
            break;
        case NETWORK_STATE_STARTED:
        case NETWORK_STATE_SYNCHING:
            networkStateLabel->setMovie(synchingMovie);
            break;
        case NETWORK_STATE_SYNCHED:
            networkStateLabel->setPixmap(*synchedIcon);
            emit signal_refreshAccounts();
            break;
        default:
            // We should never get here
            ;
        }

        connectAction->setEnabled(!isConnected());
        disconnectAction->setEnabled(isConnected());
        sendRawTxAction->setEnabled(isConnected());
    }
}

void MainWindow::updateVaultStatus(const QString& name)
{
    bool isOpen = !name.isEmpty();

    // vault actions
    importVaultAction->setEnabled(isOpen);
    exportVaultAction->setEnabled(isOpen);
    exportPublicVaultAction->setEnabled(isOpen);
    closeVaultAction->setEnabled(isOpen);

    // keychain actions
    newKeychainAction->setEnabled(isOpen);
    lockAllKeychainsAction->setEnabled(keychainModel && keychainModel->rowCount());
    importKeychainAction->setEnabled(isOpen);

    // account actions
    quickNewAccountAction->setEnabled(isOpen);
    newAccountAction->setEnabled(isOpen);
    importAccountAction->setEnabled(isOpen);

    // transaction actions
    insertRawTxAction->setEnabled(isOpen);
    createRawTxAction->setEnabled(isOpen);
    //createTxAction->setEnabled(isOpen);
    signRawTxAction->setEnabled(isOpen);

    if (isOpen) {
        setWindowTitle(getDefaultSettings().getAppName() + " - " + name);
    }
    else {
        setWindowTitle(getDefaultSettings().getAppName());
    }

    if (txActions) txActions->updateVaultStatus();
}

void MainWindow::showError(const QString& errorMsg)
{
    updateStatusMessage(tr("Operation failed"));
    QMessageBox::critical(this, tr("Error"), errorMsg);
}

void MainWindow::showUpdate(const QString& updateMsg)
{
    QMessageBox::information(this, tr("Update"), updateMsg);
}

void MainWindow::selectCurrencyUnit()
{
    try
    {
        CurrencyUnitDialog dlg(this);
        if (dlg.exec())
        {
            QString newCurrencyUnitPrefix = dlg.getCurrencyUnitPrefix();
            if (newCurrencyUnitPrefix != currencyUnitPrefix)
            {
                currencyUnitPrefix = newCurrencyUnitPrefix;
                saveSettings();
                setCurrencyUnitPrefix(currencyUnitPrefix);
                emit signal_currencyUnitChanged();
            }
        }
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::createTx - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::newVault(QString fileName)
{
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getSaveFileName(
            this,
            tr("Create New Vault"),
            getDocDir(),
            tr("Vaults (*.vault)"));
    }
    if (fileName.isEmpty()) return;

    QFileInfo fileInfo(fileName);
    setDocDir(fileInfo.dir().absolutePath());
    saveSettings();

    try
    {
        synchedVault.openVault(fileName.toStdString(), true);
        updateVaultStatus(fileName);
    }
    catch (const exception& e)
    {
        LOGGER(debug) << "MainWindow::newVault - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::promptSync()
{
    if (!isConnected())
    {
        QMessageBox msgBox;
        msgBox.setText(tr("You are not connected to network."));
        msgBox.setInformativeText(tr("Would you like to connect to network to synchronize your accounts?"));
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Ok);
        if (msgBox.exec() == QMessageBox::Ok) {
            startNetworkSync();
        }
    }
    else
    {
        syncBlocks();
    }
}

void MainWindow::openVault(QString fileName)
{
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getOpenFileName(
            this,
            tr("Open Vault"),
            getDocDir(),
            tr("Vaults (*.vault)"));
    }
    if (fileName.isEmpty()) return;

    fileName = QFileInfo(fileName).absoluteFilePath();

    QFileInfo fileInfo(fileName);
    setDocDir(fileInfo.dir().absolutePath());
    saveSettings();

    try
    {
        synchedVault.openVault(fileName.toStdString(), false);
        updateVaultStatus(fileName);
        updateStatusMessage(tr("Opened ") + fileName);
        //if (synchedVault.isConnected()) { synchedVault.syncBlocks(); }
        //promptSync();
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::openVault - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::importVault(QString /* fileName */)
{
}

void MainWindow::exportVault(QString fileName, bool exportPrivKeys)
{
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getSaveFileName(
            this,
            tr("Export Vault"),
            getDocDir(),
            tr("Portable Vault (*.vault.all)"));
    }
    if (fileName.isEmpty()) return;

    QFileInfo fileInfo(fileName);
    setDocDir(fileInfo.dir().absolutePath());
    saveSettings();

    try
    {
        if (!synchedVault.isVaultOpen()) throw std::runtime_error("No vault is open.");
        CoinDB::VaultLock lock(synchedVault);
        if (!synchedVault.isVaultOpen()) throw std::runtime_error("No vault is open.");

        synchedVault.getVault()->exportVault(fileName.toStdString(), exportPrivKeys);
    }
    catch (const exception& e)
    {
        LOGGER(debug) << "MainWindow::exportVault - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::closeVault()
{
    try
    {
        synchedVault.closeVault();
        updateVaultStatus();
    }
    catch (const exception& e)
    {
        LOGGER(debug) << "MainWindow::closeVault - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::newKeychain()
{
    try {
        if (!synchedVault.isVaultOpen()) throw std::runtime_error("No vault is open.");

        NewKeychainDialog dlg(this);
        if (dlg.exec()) {
            QString name = dlg.getName();
            //unsigned long numKeys = dlg.getNumKeys();
            //updateStatusMessage(tr("Generating ") + QString::number(numKeys) + tr(" keys..."));

            if (keychainModel->exists(name)) throw std::runtime_error("A keychain with that name already exists.");

            {
                // TODO: Randomize using user input for entropy
                CoinDB::VaultLock lock(synchedVault);
                if (!synchedVault.isVaultOpen()) throw std::runtime_error("No vault is open.");
                secure_bytes_t entropy = getRandomBytes(32);
                synchedVault.getVault()->newKeychain(name.toStdString(), entropy);
            }

            keychainModel->update();
            keychainView->updateColumns();
            tabWidget->setCurrentWidget(keychainView);
            updateStatusMessage(tr("Created keychain ") + name);
        }
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::newKeyChain - " << e.what() << std::endl;
        showError(e.what());
    }    
}

void MainWindow::unlockKeychain()
{
    QModelIndex index = keychainSelectionModel->currentIndex();
    int row = index.row();
    if (row < 0)
    {
        showError(tr("No keychain is selected."));
        return;
    }

    QStandardItem* nameItem = keychainModel->item(row, 0);
    QString name = nameItem->data(Qt::DisplayRole).toString();
    secure_bytes_t hash;
    if (keychainModel->isEncrypted(name))
    {
        PassphraseDialog dlg(tr("Enter unlock passphrase for ") + name + tr(":"));
        if (!dlg.exec()) return;

        // TODO: proper hash
        hash = sha256_2(dlg.getPassphrase().toStdString());
    }

    try
    {
        keychainModel->unlockKeychain(name, hash);
    }
    catch (const CoinDB::KeychainPrivateKeyUnlockFailedException& e)
    {
        throw std::runtime_error(tr("Invalid passphrase.").toStdString());
    }
}

void MainWindow::lockKeychain()
{
    QModelIndex index = keychainSelectionModel->currentIndex();
    int row = index.row();
    if (row < 0)
    {
        showError(tr("No keychain is selected."));
        return;
    }

    QStandardItem* nameItem = keychainModel->item(row, 0);
    QString name = nameItem->data(Qt::DisplayRole).toString();

    keychainModel->lockKeychain(name);
}

void MainWindow::lockAllKeychains()
{
    keychainModel->lockAllKeychains();
}

void MainWindow::importKeychain(QString fileName)
{
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getOpenFileName(
            this,
            tr("Import Keychain"),
            getDocDir(),
            tr("Keychains") + "(*.priv *.pub)");
    }
    if (fileName.isEmpty()) return;

    QFileInfo fileInfo(fileName);
    setDocDir(fileInfo.dir().absolutePath());
    saveSettings();

    bool ok;
    QString name = QFileInfo(fileName).baseName();
    while (true) {
        name = QInputDialog::getText(this, tr("Keychain Import"), tr("Keychain Name:"), QLineEdit::Normal, name, &ok);

        if (!ok) return;
        if (name.isEmpty()) {
            showError(tr("Name cannot be empty."));
            continue;
        }
        if (keychainModel->exists(name)) {
            showError(tr("There is already a keychain with that name."));
            continue;
        }
        break;
    }

    try {
        updateStatusMessage(tr("Importing keychain..."));
        bool isPrivate = importPrivate; // importPrivate is a user setting. isPrivate is whether or not this particular keychain is private.
        keychainModel->importKeychain(name, fileName, isPrivate);
        keychainView->updateColumns();
        tabWidget->setCurrentWidget(keychainView);
        updateStatusMessage(tr("Imported ") + (isPrivate ? tr("private") : tr("public")) + tr(" keychain ") + name);
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::importKeychain - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::exportKeychain(bool exportPrivate)
{
    QModelIndex index = keychainSelectionModel->currentIndex();
    int row = index.row();
    if (row < 0) {
        showError(tr("No keychain is selected."));
        return;
    }

    QStandardItem* typeItem = keychainModel->item(row, 1);
    int lockFlags = typeItem->data(Qt::UserRole).toInt();
    bool isPrivate = lockFlags & (1 << 1);
    bool isLocked = lockFlags & 1;

    if (exportPrivate && !isPrivate) {
        showError(tr("Cannot export private keys for public keychain."));
        return;
    }

    if (exportPrivate && isLocked) {
        showError(tr("Cannot export private keys for locked keychain."));
        return;
    }

    QStandardItem* nameItem = keychainModel->item(row, 0);
    QString name = nameItem->data(Qt::DisplayRole).toString();

    QString fileName = name + (exportPrivate ? ".priv" : ".pub");

    fileName = QFileDialog::getSaveFileName(
        this,
        tr("Exporting ") + (exportPrivate ? tr("Private") : tr("Public")) + tr(" Keychain - ") + name,
        getDocDir() + "/" + fileName,
        tr("Keychains (*.priv *.pub)"));

    if (fileName.isEmpty()) return;

    QFileInfo fileInfo(fileName);
    setDocDir(fileInfo.dir().absolutePath());
    saveSettings();

    try {
        updateStatusMessage(tr("Exporting ") + (exportPrivate ? tr("private") : tr("public")) + tr("  keychain...") + name);
        keychainModel->exportKeychain(name, fileName, exportPrivate);
        updateStatusMessage(tr("Saved ") + fileName);
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::exportKeychain - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::backupKeychain()
{
    QModelIndex index = keychainSelectionModel->currentIndex();
    int row = index.row();
    if (row < 0) {
        showError(tr("No keychain is selected."));
        return;
    }

    QStandardItem* nameItem = keychainModel->item(row, 0);
    QString name = nameItem->data(Qt::DisplayRole).toString();

    try {
        bytes_t extendedKey;

        if (keychainModel->isPrivate(name)) {
            // TODO: prompt user for decryption key
            extendedKey = keychainModel->getExtendedKeyBytes(name, true);
        }
        else {
            extendedKey = keychainModel->getExtendedKeyBytes(name);
        }

        KeychainBackupDialog dlg(tr("Keychain information"));
        dlg.setExtendedKey(extendedKey);
        dlg.exec();
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::backupKeychain - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::updateCurrentKeychain(const QModelIndex& current, const QModelIndex& /*previous*/)
{
    int row = current.row();
    if (row == -1) {
        exportPrivateKeychainAction->setEnabled(false);
        exportPublicKeychainAction->setEnabled(false);
        backupKeychainAction->setEnabled(false);
    }
    else {
        QStandardItem* typeItem = keychainModel->item(row, 1);
        int lockFlags = typeItem->data(Qt::UserRole).toInt();
        bool isPrivate = lockFlags & (1 << 1);
        bool isLocked = lockFlags & 1;

        unlockKeychainAction->setEnabled(isPrivate && isLocked);
        lockKeychainAction->setEnabled(isPrivate && !isLocked);
        exportPrivateKeychainAction->setEnabled(isPrivate);
        exportPublicKeychainAction->setEnabled(true);
        backupKeychainAction->setEnabled(true);
    }
}

void MainWindow::updateSelectedKeychains(const QItemSelection& /*selected*/, const QItemSelection& /*deselected*/)
{
    int selectionCount = keychainView->selectionModel()->selectedRows(0).size();
    bool isSelected = selectionCount > 0;
    newAccountAction->setEnabled(isSelected);
}

bool MainWindow::selectAccount(int i)
{
    if (accountModel->rowCount() <= i) return false;
    QItemSelection selection(accountModel->index(i, 0), accountModel->index(i, accountModel->columnCount() - 1));
    accountView->selectionModel()->select(selection, QItemSelectionModel::SelectCurrent);
    return true;
}

bool MainWindow::selectAccount(const QString& account)
{
    // TODO: find a better implementation
    for (int i = 0; i < accountModel->rowCount(); i++)
    {
        QStandardItem* item = accountModel->item(i, 0);
        if (item->text() == account)
        {
            QItemSelection selection(accountModel->index(i, 0), accountModel->index(i, accountModel->columnCount() - 1));
            accountView->selectionModel()->select(selection, QItemSelectionModel::SelectCurrent);
            return true;
        }
    }
    return false;
}

void MainWindow::updateCurrentAccount(const QModelIndex& /*current*/, const QModelIndex& /*previous*/)
{
/*
    int row = current.row();
    bool isCurrent = (row != -1);

    deleteAccountAction->setEnabled(isCurrent);
    viewAccountHistoryAction->setEnabled(isCurrent);
*/
}

void MainWindow::updateSelectedAccounts(const QItemSelection& /*selected*/, const QItemSelection& /*deselected*/)
{
    QItemSelectionModel* selectionModel = accountView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);
    bool isSelected = indexes.size() > 0;
    if (isSelected) {
        selectedAccount = accountModel->data(indexes.at(0)).toString();
        txModel->setAccount(selectedAccount);
        txModel->update();
        txView->updateColumns();
        tabWidget->setTabText(1, tr("Transactions - ") + selectedAccount);
        requestPaymentDialog->setCurrentAccount(selectedAccount);
    }
    else {
        selectedAccount = "";
        tabWidget->setTabText(1, tr("Transactions"));
    }
    deleteAccountAction->setEnabled(isSelected);
    exportAccountAction->setEnabled(isSelected);
    exportSharedAccountAction->setEnabled(isSelected);
    viewAccountHistoryAction->setEnabled(isSelected);
    viewScriptsAction->setEnabled(isSelected);
    requestPaymentAction->setEnabled(isSelected);
    sendPaymentAction->setEnabled(isSelected);
    viewUnsignedTxsAction->setEnabled(isSelected);
}

void MainWindow::refreshAccounts()
{
    LOGGER(trace) << "MainWindow::refreshAccounts()" << std::endl;

    QString prevSelectedAccount = selectedAccount;
    accountModel->update();
    //accountView->updateColumns();

    if (prevSelectedAccount != selectedAccount)
    {
        selectAccount(prevSelectedAccount);
    }
    else
    {
        txModel->update();
        txView->updateColumns();
    }
}

void MainWindow::quickNewAccount()
{
    if (!synchedVault.isVaultOpen()) throw std::runtime_error("No vault is open.");

    QuickNewAccountDialog dlg(this);
    while (dlg.exec()) {
        try {
            QString accountName = dlg.getName();
            if (accountName.isEmpty())
                throw std::runtime_error(tr("Account name is empty.").toStdString());

            if (dlg.getMinSigs() > dlg.getMaxSigs())
                throw std::runtime_error(tr("The minimum signature count cannot exceed the number of keychains.").toStdString());

            if (accountModel->accountExists(accountName))
                throw std::runtime_error(tr("An account with that name already exists.").toStdString());

            const int MAX_KEYCHAIN_INDEX = 1000;
            int i = 0;
            QList<QString> keychainNames;
            while (keychainNames.size() < dlg.getMaxSigs() && ++i <= MAX_KEYCHAIN_INDEX) {
                QString keychainName = accountName + "_" + QString::number(i);
                if (!keychainModel->exists(keychainName))
                    keychainNames << keychainName;
            }
            if (i > MAX_KEYCHAIN_INDEX)
                throw std::runtime_error(tr("Ran out of keychain indices.").toStdString());

            for (auto& keychainName: keychainNames) {
                // TODO: Randomize using user input for entropy
                CoinDB::VaultLock lock(synchedVault);
                if (!synchedVault.isVaultOpen()) throw std::runtime_error("No vault is open.");
                secure_bytes_t entropy = getRandomBytes(32);
                synchedVault.getVault()->newKeychain(keychainName.toStdString(), entropy);
            }

            accountModel->newAccount(accountName, dlg.getMinSigs(), keychainNames, dlg.getCreationTime());
            accountModel->update();
            accountView->updateColumns();
            keychainModel->update();
            keychainView->updateColumns();
            selectAccount(accountName);
            tabWidget->setCurrentWidget(accountView);
            updateStatusMessage(tr("Created account ") + accountName);
            break;
        }
        catch (const exception& e) {
            showError(e.what());
        }
    }
}

void MainWindow::newAccount()
{
    QItemSelectionModel* selectionModel = keychainView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);

    QList<QString> keychainNames;
    for (auto& index: indexes) {
        keychainNames << keychainModel->data(index).toString();
    }

    try {
        NewAccountDialog dlg(keychainNames, this);
        if (dlg.exec()) {
            accountModel->newAccount(dlg.getName(), dlg.getMinSigs(), dlg.getKeychainNames(), dlg.getCreationTime());
            accountView->updateColumns();
            selectAccount(dlg.getName());
            tabWidget->setCurrentWidget(accountView);
            synchedVault.updateBloomFilter();
            //networkSync.setBloomFilter(accountModel->getBloomFilter(0.0001, 0, 0));
            if (isConnected()) syncBlocks();
        }
    }
    catch (const exception& e) {
        // TODO: Handle other possible errors.
        showError(tr("No keychains selected."));
    }
}

void MainWindow::importAccount(QString fileName)
{
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getOpenFileName(
            this,
            tr("Import Account"),
            getDocDir(),
            tr("Account") + "(*.acct *.sharedacct)");
    }

    if (fileName.isEmpty()) return;

    QFileInfo fileInfo(fileName);
    setDocDir(fileInfo.dir().absolutePath());
    saveSettings();

    bool ok;
    QString name = QFileInfo(fileName).baseName();
    while (true) {
        name = QInputDialog::getText(this, tr("Account Import"), tr("Account Name:"), QLineEdit::Normal, name, &ok);
        if (!ok) return;
        if (name.isEmpty()) {
            showError(tr("Name cannot be empty."));
            continue;
        }
        if (accountModel->accountExists(name)) {
            showError(tr("There is already an account with that name."));
            continue;
        }
        break;
    }

    try {
        synchedVault.suspendBlockUpdates();
        updateStatusMessage(tr("Importing account..."));
        accountModel->importAccount(name, fileName);
        accountModel->update();
        accountView->updateColumns();
        keychainModel->update();
        keychainView->updateColumns();
        selectAccount(name);
        tabWidget->setCurrentWidget(accountView);
        synchedVault.updateBloomFilter();
        updateStatusMessage(tr("Imported account ") + name);
        if (synchedVault.isConnected()) { synchedVault.syncBlocks(); }
        //promptSync();
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::importAccount - " << e.what() << std::endl;
        showError(e.what());
    }

}

void MainWindow::exportAccount()
{
    QItemSelectionModel* selectionModel = accountView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);
    if (indexes.isEmpty()) {
        showError(tr("No account selected."));
        return;
    }

    QString name = accountModel->data(indexes.at(0)).toString();

    QString fileName = name + ".acct";

    fileName = QFileDialog::getSaveFileName(
        this,
        tr("Exporting Account - ") + name,
        getDocDir() + "/" + fileName,
        tr("Accounts (*.acct)"));

    if (fileName.isEmpty()) return;

    QFileInfo fileInfo(fileName);
    setDocDir(fileInfo.dir().absolutePath());
    saveSettings();

    try {
        updateStatusMessage(tr("Exporting account ") + name + "...");
        accountModel->exportAccount(name, fileName, false);
        updateStatusMessage(tr("Saved ") + fileName);
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::exportAccount - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::exportSharedAccount()
{
    QItemSelectionModel* selectionModel = accountView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);
    if (indexes.isEmpty()) {
        showError(tr("No account selected."));
        return;
    }

    QString name = accountModel->data(indexes.at(0)).toString();

    QString fileName = name + ".sharedacct";

    fileName = QFileDialog::getSaveFileName(
        this,
        tr("Exporting Shared Account - ") + name,
        getDocDir() + "/" + fileName,
        tr("Accounts (*.sharedacct)"));

    if (fileName.isEmpty()) return;

    QFileInfo fileInfo(fileName);
    setDocDir(fileInfo.dir().absolutePath());
    saveSettings();

    try {
        updateStatusMessage(tr("Exporting shared account ") + name + "...");
        accountModel->exportAccount(name, fileName, true);
        updateStatusMessage(tr("Saved ") + fileName);
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::exportSharedAccount - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::deleteAccount()
{
    QItemSelectionModel* selectionModel = accountView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);
    if (indexes.isEmpty()) {
        showError(tr("No account selected."));
        return;
    }

    QString accountName = accountModel->data(indexes.at(0)).toString();

    if (QMessageBox::Yes == QMessageBox::question(this, tr("Confirm"), tr("Are you sure you want to delete account ") + accountName + "?")) {
        try {
            accountModel->deleteAccount(accountName);
            accountView->updateColumns();
            synchedVault.updateBloomFilter();
            //networkSync.setBloomFilter(accountModel->getBloomFilter(0.0001, 0, 0));
        }
        catch (const exception& e) {
            LOGGER(debug) << "MainWindow::deleteAccount - " << e.what() << std::endl;
            showError(e.what());
        }
    }
}

void MainWindow::viewAccountHistory()
{
/*
    QItemSelectionModel* selectionModel = accountView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);
    if (indexes.isEmpty()) {
        showError(tr("No account selected."));
        return;
    }

    // Only open one - show modal for now.
    QString accountName = accountModel->data(indexes.at(0)).toString();

    try {
        // TODO: Make this dialog not modal and allow viewing several account histories simultaneously.
        AccountHistoryDialog dlg(accountModel->getVault(), accountName, &networkSync, this);
        connect(&dlg, &AccountHistoryDialog::txDeleted, [this]() { accountModel->update(); });
        dlg.exec();
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::viewAccountHistory - " << e.what() << std::endl;
        showError(e.what());
    }
*/
}

void MainWindow::viewScripts()
{
    QItemSelectionModel* selectionModel = accountView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);
    if (indexes.isEmpty()) {
        showError(tr("No account selected."));
        return;
    }

    // Only open one - show modal for now.
    QString accountName = accountModel->data(indexes.at(0)).toString();

    try {
        // TODO: Make this dialog not modal and allow viewing several account histories simultaneously.
        ScriptDialog dlg(accountModel->getVault(), accountName, this);
        dlg.exec();
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::viewScripts - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::requestPayment()
{
    QItemSelectionModel* selectionModel = accountView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);
    if (indexes.isEmpty()) {
        showError(tr("No account selected."));
        return;
    }

    // Only open one
    QString accountName = accountModel->data(indexes.at(0)).toString();

    try {
        //RequestPaymentDialog dlg(accountModel, accountName);
        requestPaymentDialog->show();
        requestPaymentDialog->raise();
        requestPaymentDialog->activateWindow();
        //dlg.exec();
/*
        if (dlg.exec()) {
            accountName = dlg.getAccountName(); // in case it was changed by user
            QString label = dlg.getSender();
            auto pair = accountModel->issueNewScript(accountName, label);

            // TODO: create a prettier dialog to display this information
            QMessageBox::information(this, tr("New Script - ") + accountName,
                tr("Label: ") + label + tr(" Address: ") + pair.first + tr(" Script: ") + QString::fromStdString(uchar_vector(pair.second).getHex()));
        }
*/
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::requestPayment - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::viewUnsignedTxs()
{
    showError(tr("Not implemented yet"));
}

void MainWindow::insertRawTx()
{
    try {
        RawTxDialog dlg(tr("Add Raw Transaction:"));
        if (dlg.exec() && accountModel->insertRawTx(dlg.getRawTx())) {
            accountView->updateColumns();
            txModel->update();
            txView->updateColumns();
            tabWidget->setCurrentWidget(txView);
        } 
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::insertRawTx - " << e.what() << std::endl;
        showError(e.what());
    } 
}

void MainWindow::createRawTx()
{
    if (!synchedVault.isVaultOpen())
    {
        showError(tr("No vault is open."));
        return;
    }

    QItemSelectionModel* selectionModel = accountView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);
    if (indexes.isEmpty()) {
        showError(tr("No account selected."));
        return;
    }
 
    QString accountName = accountModel->data(indexes.at(0)).toString();

    CreateTxDialog dlg(accountModel->getVault(), accountName);
    while (dlg.exec()) {
        try {
            std::vector<TaggedOutput> outputs = dlg.getOutputs();
            uint64_t fee = dlg.getFeeValue();
            bytes_t rawTx = accountModel->createRawTx(dlg.getAccountName(), outputs, fee);
            RawTxDialog rawTxDlg(tr("Unsigned Transaction"));
            rawTxDlg.setRawTx(rawTx);
            rawTxDlg.exec();
            return;
        }
        catch (const exception& e) {
            LOGGER(debug) << "MainWindow::createRawTx - " << e.what() << std::endl;
            showError(e.what());
        }
    }
}

void MainWindow::createTx(const PaymentRequest& paymentRequest)
{
    if (!synchedVault.isVaultOpen())
    {
        showError(tr("No vault is open."));
        return;
    }

    QItemSelectionModel* selectionModel = accountView->selectionModel();
    QModelIndexList indexes = selectionModel->selectedRows(0);
    if (indexes.isEmpty()) {
        showError(tr("No account selected."));
        return;
    }
 
    QString accountName = accountModel->data(indexes.at(0)).toString();

    CreateTxDialog dlg(accountModel->getVault(), accountName, paymentRequest);
    bool saved = false;
    while (!saved && dlg.exec()) {
        try {
            CreateTxDialog::status_t status = dlg.getStatus();
            bool sign = status == CreateTxDialog::SIGN_AND_SEND || status == CreateTxDialog::SIGN_AND_SAVE;
            std::shared_ptr<CoinDB::Tx> tx = accountModel->getVault()->createTx(
                dlg.getAccountName().toStdString(),
                1,
                0,
                dlg.getInputTxOutIds(),
                dlg.getTxOuts(),
                dlg.getFeeValue(),
                0,
                true);
            if (!tx) throw std::runtime_error("Error creating transaction.");

            saved = true;
            newTx();            
            if (sign)
            {
                std::vector<std::string> keychains;
                tx = accountModel->getVault()->signTx(tx->id(), keychains, true);
            }
            tabWidget->setCurrentWidget(txView);
            if (status == CreateTxDialog::SIGN_AND_SEND) {
                // TODO: Clean up signing and sending code
                if (tx->status() == CoinDB::Tx::UNSIGNED) {
                    throw std::runtime_error(tr("Could not send - transaction still missing signatures.").toStdString());
                }
                if (!isConnected()) {
                    throw std::runtime_error(tr("Must be connected to network to send.").toStdString());
                }
                Coin::Transaction coin_tx = tx->toCoinCore();
                synchedVault.sendTx(coin_tx);
            }
            return;
        }
        catch (const exception& e) {
            LOGGER(debug) << "MainWindow::createTx - " << e.what() << std::endl;
            showError(e.what());
        }
    }
}

void MainWindow::signRawTx()
{
    try {
        RawTxDialog dlg(tr("Sign Raw Transaction:"));
        if (dlg.exec()) {
            bytes_t rawTx = accountModel->signRawTx(dlg.getRawTx());
            RawTxDialog rawTxDlg(tr("Signed Transaction"));
            rawTxDlg.setRawTx(rawTx);
            rawTxDlg.exec();
        }
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::signRawTx - " << e.what() << std::endl;
        showError(e.what());
    }

}

void MainWindow::sendRawTx()
{
    if (!isConnected()) {
        showError(tr("Must be connected to send raw transaction"));
        return;
    }
 
    try {
        RawTxDialog dlg(tr("Send Raw Transaction:"));
        if (dlg.exec()) {
            bytes_t rawTx = dlg.getRawTx();
            Coin::Transaction tx(rawTx);
            synchedVault.sendTx(tx);
            //networkSync.sendTx(tx);
            updateStatusMessage(tr("Sent tx " ) + QString::fromStdString(tx.getHashLittleEndian().getHex()) + tr(" to peer"));
        }
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::sendRawTx - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::newTx()
{
    refreshAccounts();
}

void MainWindow::newBlock()
{
    if (isSynched() || syncHeight % 10 == 0) { refreshAccounts(); }
}

void MainWindow::syncBlocks()
{
    updateStatusMessage(tr("Synchronizing vault"));
/*
    uint32_t startTime = accountModel->getMaxFirstBlockTimestamp();
    std::vector<bytes_t> locatorHashes = accountModel->getLocatorHashes();
    for (auto& hash: locatorHashes) {
        LOGGER(debug) << "MainWindow::syncBlocks() - hash: " << uchar_vector(hash).getHex() << std::endl;
    }
    networkSync.syncBlocks(locatorHashes, startTime);
*/
    synchedVault.syncBlocks();
}

void MainWindow::fetchingHeaders()
{
    updateNetworkState(NETWORK_STATE_SYNCHING);
    emit status(tr("Synching headers"));
}

void MainWindow::headersSynched()
{
    emit updateBestHeight(synchedVault.getBestHeight());
    emit status(tr("Finished loading headers."));
    if (synchedVault.isVaultOpen()) {
    try {
        syncBlocks();
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::doneHeaderSync - " << e.what() << std::endl;
        emit status(QString::fromStdString(e.what()));
    }
}
}

void MainWindow::fetchingBlocks()
{
    updateNetworkState(NETWORK_STATE_SYNCHING);
    emit status(tr("Synching blocks"));
}

void MainWindow::blocksSynched()
{
    updateNetworkState(NETWORK_STATE_SYNCHED);
    emit status(tr("Finished block sync"));
}

void MainWindow::addBestChain(const chain_header_t& header)
{
    emit updateBestHeight(header.height);
}

void MainWindow::removeBestChain(const chain_header_t& header)
{
    bytes_t hash = header.getHashLittleEndian();
    LOGGER(debug) << "MainWindow::removeBestChain - " << uchar_vector(hash).getHex() << std::endl;
    
    int diff = bestHeight - synchedVault.getBestHeight();
    //int diff = bestHeight - networkSync.getBestHeight();
    if (diff >= 0) {
        QString message = tr("Reorganization of ") + QString::number(diff + 1) + tr(" blocks");
        emit status(message);
        emit updateBestHeight(synchedVault.getBestHeight());
        //emit updateBestHeight(networkSync.getBestHeight());
    }
}

void MainWindow::connectionOpen()
{
    QString message = tr("Connected to ") + host + ":" + QString::number(port);
    emit status(message);
}

void MainWindow::connectionClosed()
{
    emit status(tr("Connection closed"));
}

void MainWindow::startNetworkSync()
{
    //connectAction->setEnabled(false);
    try {
        QString message(tr("Connecting to ") + host + ":" + QString::number(port) + "...");
        updateStatusMessage(message);
        //networkStarted();
        synchedVault.startSync(host.toStdString(), port);
    }
    catch (const exception& e) {
        LOGGER(debug) << "MainWindow::startNetworkSync - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::stopNetworkSync()
{
    disconnectAction->setEnabled(false);
    try
    {
        updateStatusMessage(tr("Disconnecting..."));
        synchedVault.stopSync();
    }
    catch (const exception& e)
    {
        networkStopped();
        LOGGER(debug) << "MainWindow::stopNetworkSync - " << e.what() << std::endl;
        showError(e.what());
    }
}

void MainWindow::networkStatus(const QString& status)
{
    updateStatusMessage(status);
}

void MainWindow::networkError(const QString& error)
{
    QString message = tr("Network Error: ") + error;
    emit status(message);
}

void MainWindow::networkStarted()
{
    updateNetworkState(NETWORK_STATE_STARTED);
    emit status(tr("Network started"));
}

// TODO: put common state change operations into common functions
void MainWindow::networkStopped()
{
    disconnectAction->setText(tr("Disconnect from ") + host);
    updateNetworkState(NETWORK_STATE_STOPPED);
    emit status(tr("Network stopped"));
}

void MainWindow::networkTimeout()
{
    emit status(tr("Network timed out"));
}

void MainWindow::networkSettings()
{
    // TODO: Add a connect button to dialog.
    NetworkSettingsDialog dlg(host, port, autoConnect);
    if (dlg.exec()) {
        host = dlg.getHost();
        port = dlg.getPort();
        autoConnect = dlg.getAutoConnect();
        connectAction->setText(tr("Connect to ") + host);
        if (!isConnected()) {
            disconnectAction->setText(tr("Disconnect from ") + host);
        }

        QSettings settings("Ciphrex", getDefaultSettings().getNetworkSettingsPath());
        settings.setValue("host", host);
        settings.setValue("port", port);
    }
}

void MainWindow::about()
{
    AboutDialog dlg(this);
    dlg.exec();
/*
   QMessageBox::about(this, tr("About ") + getDefaultSettings().getAppName(),
            tr("<b>") + getDefaultSettings().getAppName() + "(TM) " + getVersionText() + "</b><br />" +
            "Commit: " + getCommitHash() + "<br />" + "Schema version: " + QString::number(getSchemaVersion()) + "<br />" + getCopyrightText());
*/
}

void MainWindow::errorStatus(const QString& message)
{
    LOGGER(debug) << "MainWindow::errorStatus - " << message.toStdString() << std::endl;
    QString error = tr("Error - ") + message;
    emit status(error);
}

void MainWindow::processUrl(const QUrl& url)
{
    try {
        if (url.scheme().compare("bitcoin:", Qt::CaseInsensitive)) {
            PaymentRequest paymentRequest(url);
            createTx(paymentRequest);            
        }
        else {
            throw std::runtime_error(tr("Unhandled URL protocol").toStdString());
        }
    }
    catch (const exception& e) {
        showError(e.what());
    }
}

void MainWindow::processFile(const QString& fileName)
{
    QString ext = fileName.section('.', -1);
    if (ext == "vault") {
        openVault(fileName);
    }
    else if (ext == "acct") {
        importAccount(fileName);
    }
    else if (ext == "keys") {
        importKeychain(fileName);
    }
    else {
        LOGGER(debug) << "MainWindow::processFile - unhandled file type: " << fileName.toStdString() << std::endl;
    }
}

void MainWindow::processCommand(const QString& command)
{
    LOGGER(trace) << "MainWindow::processCommand - " << command.toStdString() << std::endl;
    if (command == "clearsettings") {
        clearSettings();
    }
    else if (command == "unlicense") {
        licenseAccepted = false;
        saveSettings();
    }
    else {
        LOGGER(trace) << "MainWindow::processCommand - unhandled command: " << command.toStdString() << std::endl;
    }
}

void MainWindow::createActions()
{
    // application actions
    selectCurrencyUnitAction = new QAction(tr("Select Currency Units..."), this);
    selectCurrencyUnitAction->setStatusTip(tr("Select the curr3ncy unit"));
    connect(selectCurrencyUnitAction, SIGNAL(triggered()), this, SLOT(selectCurrencyUnit()));

    quitAction = new QAction(tr("&Quit ") + getDefaultSettings().getAppName(), this);
    quitAction->setShortcuts(QKeySequence::Quit);
    quitAction->setStatusTip(tr("Quit the application"));
    connect(quitAction, SIGNAL(triggered()), this, SLOT(close()));

    // vault actions
    newVaultAction = new QAction(QIcon(":/icons/newvault.png"), tr("&New Vault..."), this);
    newVaultAction->setShortcuts(QKeySequence::New);
    newVaultAction->setStatusTip(tr("Create a new vault"));
    connect(newVaultAction, SIGNAL(triggered()), this, SLOT(newVault()));

    openVaultAction = new QAction(QIcon(":/icons/openvault.png"), tr("&Open Vault..."), this);
    openVaultAction->setShortcuts(QKeySequence::Open);
    openVaultAction->setStatusTip(tr("Open an existing vault"));
    connect(openVaultAction, SIGNAL(triggered()), this, SLOT(openVault())); 

    importVaultAction = new QAction(tr("&Import Vault..."), this);
    importVaultAction->setStatusTip("Import vault from portable file");
    importVaultAction->setEnabled(false);
    connect(importVaultAction, SIGNAL(triggered()), this, SLOT(importVault()));

    exportVaultAction = new QAction(tr("&Export Vault..."), this);
    exportVaultAction->setStatusTip("Export vault to portable file");
    exportVaultAction->setEnabled(false);
    connect(exportVaultAction, &QAction::triggered, [=]() { this->exportVault(QString(), false); });

    exportPublicVaultAction = new QAction(tr("&Export Public Vault..."), this);
    exportPublicVaultAction->setStatusTip("Export public vault to portable file");
    exportPublicVaultAction->setEnabled(false);
    connect(exportPublicVaultAction, &QAction::triggered, [=]() { this->exportVault(QString(), true); });

    closeVaultAction = new QAction(QIcon(":/icons/closevault.png"), tr("Close Vault"), this);
    closeVaultAction->setStatusTip(tr("Close vault"));
    closeVaultAction->setEnabled(false);
    connect(closeVaultAction, SIGNAL(triggered()), this, SLOT(closeVault()));

    // keychain actions
    newKeychainAction = new QAction(QIcon(":/icons/keypair.png"), tr("New &Keychain..."), this);
    newKeychainAction->setStatusTip(tr("Create a new keychain"));
    newKeychainAction->setEnabled(false);
    connect(newKeychainAction, SIGNAL(triggered()), this, SLOT(newKeychain()));

    unlockKeychainAction = new QAction(tr("Unlock keychain..."), this);
    unlockKeychainAction->setStatusTip(tr("Unlock keychain"));
    unlockKeychainAction->setEnabled(false);
    connect(unlockKeychainAction, SIGNAL(triggered()), this, SLOT(unlockKeychain()));

    lockKeychainAction = new QAction(tr("Lock keychain"), this);
    lockKeychainAction->setStatusTip(tr("Lock keychain"));
    lockKeychainAction->setEnabled(false);
    connect(lockKeychainAction, SIGNAL(triggered()), this, SLOT(lockKeychain()));

    lockAllKeychainsAction = new QAction(tr("Lock all keychains"), this);
    lockAllKeychainsAction->setStatusTip(tr("Lock all keychains"));
    lockAllKeychainsAction->setEnabled(false);
    connect(lockAllKeychainsAction, SIGNAL(triggered()), this, SLOT(lockAllKeychains()));

    importPrivateAction = new QAction(tr("Private Imports"), this);
    importPrivateAction->setCheckable(true);
    importPrivateAction->setStatusTip(tr("Import private keys if available"));
    connect(importPrivateAction, &QAction::triggered, [=]() { this->importPrivate = true; });

    importPublicAction = new QAction(tr("Public Imports"), this);
    importPublicAction->setCheckable(true);
    importPublicAction->setStatusTip(tr("Only import public keys"));
    connect(importPublicAction, &QAction::triggered, [=]() { this->importPrivate = false; });

    importModeGroup = new QActionGroup(this);
    importModeGroup->addAction(importPrivateAction);
    importModeGroup->addAction(importPublicAction);
    importPrivateAction->setChecked(true);
    importPrivate = true;

    importKeychainAction = new QAction(tr("Import Keychain..."), this);
    importKeychainAction->setStatusTip(tr("Import keychain from file"));
    importKeychainAction->setEnabled(false);
    connect(importKeychainAction, SIGNAL(triggered()), this, SLOT(importKeychain()));

    exportPrivateKeychainAction = new QAction(tr("Export Private Keychain..."), this);
    exportPrivateKeychainAction->setStatusTip(tr("Export private keychain to file"));
    exportPrivateKeychainAction->setEnabled(false);
    connect(exportPrivateKeychainAction, &QAction::triggered, [=]() { this->exportKeychain(true); });

    exportPublicKeychainAction = new QAction(tr("Export Public Keychain..."), this);
    exportPublicKeychainAction->setStatusTip(tr("Export public keychain to file"));
    exportPublicKeychainAction->setEnabled(false);
    connect(exportPublicKeychainAction, &QAction::triggered, [=]() { this->exportKeychain(false); });

    backupKeychainAction = new QAction(tr("Backup Keychain..."), this);
    backupKeychainAction->setStatusTip(tr("Make paper backup"));
    backupKeychainAction->setEnabled(false);
    connect(backupKeychainAction, SIGNAL(triggered()), this, SLOT(backupKeychain()));

    // account actions
    quickNewAccountAction = new QAction(QIcon(":/icons/magicwand.png"), tr("Account &Wizard..."), this);
    quickNewAccountAction->setStatusTip(tr("Create a new account, automatically create new keychains for it"));
    quickNewAccountAction->setEnabled(false);
    connect(quickNewAccountAction, SIGNAL(triggered()), this, SLOT(quickNewAccount()));

    newAccountAction = new QAction(QIcon(":/icons/money.png"), tr("Create &Account..."), this);
    newAccountAction->setStatusTip(tr("Create a new account with selected keychains"));
    newAccountAction->setEnabled(false);
    connect(newAccountAction, SIGNAL(triggered()), this, SLOT(newAccount()));

    requestPaymentAction = new QAction(QIcon(":/icons/cashregister.png"), tr("Request Payment..."), this);
    requestPaymentAction->setStatusTip(tr("Get a new address to request a payment"));
    requestPaymentAction->setEnabled(false);
    connect(requestPaymentAction, SIGNAL(triggered()), this, SLOT(requestPayment()));

    sendPaymentAction = new QAction(QIcon(":/icons/payment.png"), tr("Send Payment..."), this);
    sendPaymentAction->setStatusTip(tr("Create a new transaction to send payment"));
    sendPaymentAction->setEnabled(false);
    connect(sendPaymentAction, SIGNAL(triggered()), this, SLOT(createTx()));

    importAccountAction = new QAction(tr("Import Account..."), this);
    importAccountAction->setStatusTip(tr("Import an account"));
    importAccountAction->setEnabled(false);
    connect(importAccountAction, SIGNAL(triggered()), this, SLOT(importAccount()));

    exportAccountAction = new QAction(tr("Export Account..."), this);
    exportAccountAction->setStatusTip(tr("Export an account"));
    exportAccountAction->setEnabled(false);
    connect(exportAccountAction, SIGNAL(triggered()), this, SLOT(exportAccount()));

    exportSharedAccountAction = new QAction(tr("Export Shared Account..."), this);
    exportSharedAccountAction->setStatusTip(tr("Export a shared account"));
    exportSharedAccountAction->setEnabled(false);
    connect(exportSharedAccountAction, SIGNAL(triggered()), this, SLOT(exportSharedAccount()));

    deleteAccountAction = new QAction(tr("Delete Account"), this);
    deleteAccountAction->setStatusTip(tr("Delete current account"));
    deleteAccountAction->setEnabled(false);
    connect(deleteAccountAction, SIGNAL(triggered()), this, SLOT(deleteAccount()));

    viewAccountHistoryAction = new QAction(tr("View Account History"), this);
    viewAccountHistoryAction->setStatusTip(tr("View history for active account"));
    viewAccountHistoryAction->setEnabled(false);
    connect(viewAccountHistoryAction, SIGNAL(triggered()), this, SLOT(viewAccountHistory()));

    viewScriptsAction = new QAction(tr("View Scripts"), this);
    viewScriptsAction->setStatusTip(tr("View scripts for active account"));
    viewScriptsAction->setEnabled(false);
    connect(viewScriptsAction, SIGNAL(triggered()), this, SLOT(viewScripts()));
    
    viewUnsignedTxsAction = new QAction(tr("View Unsigned Transactions"), this);
    viewUnsignedTxsAction->setStatusTip(tr("View transactions pending signature"));
    viewUnsignedTxsAction->setEnabled(false);
    connect(viewUnsignedTxsAction, SIGNAL(triggered()), this, SLOT(viewUnsignedTxs()));

    // transaction actions
    insertRawTxAction = new QAction(tr("Insert Raw Transaction..."), this);
    insertRawTxAction->setStatusTip(tr("Insert a raw transaction into vault"));
    insertRawTxAction->setEnabled(false);
    connect(insertRawTxAction, SIGNAL(triggered()), this, SLOT(insertRawTx()));

    signRawTxAction = new QAction(tr("Sign Raw Transaction..."), this);
    signRawTxAction->setStatusTip(tr("Sign a raw transaction using keychains in vault"));
    signRawTxAction->setEnabled(false);
    connect(signRawTxAction, SIGNAL(triggered()), this, SLOT(signRawTx()));

    createRawTxAction = new QAction(tr("Create Transaction..."), this);
    createRawTxAction->setStatusTip(tr("Create a new transaction"));
    createRawTxAction->setEnabled(false);
//    connect(createRawTxAction, SIGNAL(triggered()), this, SLOT(createRawTx()));

    createTxAction = new QAction(tr("Create Transaction..."), this);
    createTxAction->setStatusTip(tr("Create a new transaction"));
    createTxAction->setEnabled(false);
    connect(createTxAction, SIGNAL(triggered()), this, SLOT(createTx()));

    sendRawTxAction = new QAction(tr("Send Raw Transaction..."), this);
    sendRawTxAction->setStatusTip(tr("Send a raw transaction to peer"));
    sendRawTxAction->setEnabled(false);
    connect(sendRawTxAction, SIGNAL(triggered()), this, SLOT(sendRawTx()));

    // network actions
    connectAction = new QAction(QIcon(":/icons/connect.png"), tr("Connect to ") + host, this);
    connectAction->setStatusTip(tr("Connect to a p2p node"));
    connectAction->setEnabled(true);

    disconnectAction = new QAction(QIcon(":/icons/disconnect.png"), tr("Disconnect from ") + host, this);
    disconnectAction->setStatusTip(tr("Disconnect from p2p node"));
    disconnectAction->setEnabled(false);

    connect(connectAction, SIGNAL(triggered()), this, SLOT(startNetworkSync()));
    connect(disconnectAction, SIGNAL(triggered()), this, SLOT(stopNetworkSync()));

    networkSettingsAction = new QAction(tr("Settings..."), this);
    networkSettingsAction->setStatusTip(tr("Configure network settings"));
    networkSettingsAction->setEnabled(true);
    connect(networkSettingsAction, SIGNAL(triggered()), this, SLOT(networkSettings()));

    // view actions
    smallFontsAction = new QAction(tr("&Small"), this);
    smallFontsAction->setCheckable(true);
    smallFontsAction->setStatusTip(tr("Use small fonts"));
    connect(smallFontsAction, &QAction::triggered, [this]() { updateFonts(SMALL_FONTS); });

    mediumFontsAction = new QAction(tr("&Medium"), this);
    mediumFontsAction->setCheckable(true);
    mediumFontsAction->setStatusTip(tr("Use medium fonts"));
    connect(mediumFontsAction, &QAction::triggered, [this]() { updateFonts(MEDIUM_FONTS); });

    largeFontsAction = new QAction(tr("&Large"), this);
    largeFontsAction->setCheckable(true);
    largeFontsAction->setStatusTip(tr("Use large fonts"));
    connect(largeFontsAction, &QAction::triggered, [this]() { updateFonts(LARGE_FONTS); });

    fontSizeGroup = new QActionGroup(this);
    fontSizeGroup->addAction(smallFontsAction);
    fontSizeGroup->addAction(mediumFontsAction);
    fontSizeGroup->addAction(largeFontsAction);

    // about/help actions
    aboutAction = new QAction(tr("About..."), this);
    aboutAction->setStatusTip(tr("About ") + getDefaultSettings().getAppName());
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(about()));

    updateVaultStatus();
}

void MainWindow::createMenus()
{
    fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(newVaultAction);
    fileMenu->addAction(openVaultAction);
    fileMenu->addAction(closeVaultAction);
    fileMenu->addSeparator();
    fileMenu->addAction(importVaultAction);
    fileMenu->addAction(exportVaultAction);
    fileMenu->addAction(exportPublicVaultAction);
    fileMenu->addSeparator();
    fileMenu->addAction(selectCurrencyUnitAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);    

    accountMenu = menuBar()->addMenu(tr("&Accounts"));
    accountMenu->addAction(requestPaymentAction);
    accountMenu->addAction(sendPaymentAction);
    accountMenu->addSeparator();
    //accountMenu->addAction(deleteAccountAction);
    //accountMenu->addSeparator();
    accountMenu->addAction(importAccountAction);
    accountMenu->addAction(exportAccountAction);
    accountMenu->addAction(exportSharedAccountAction);
    accountMenu->addSeparator();
    //accountMenu->addAction(viewAccountHistoryAction);
    accountMenu->addAction(viewScriptsAction);
    accountMenu->addSeparator();
    accountMenu->addAction(viewUnsignedTxsAction);

    menuBar()->addMenu(txActions->getMenu());
/*
    txMenu = menuBar()->addMenu(tr("&Transactions"));
    txMenu->addAction(insertRawTxAction);
    //txMenu->addSeparator();
    //txMenu->addAction(signRawTxAction);
    //txMenu->addSeparator();
//    txMenu->addAction(createRawTxAction);
    //txMenu->addAction(createTxAction);
    //txMenu->addSeparator();
    txMenu->addAction(sendRawTxAction);
*/

    keychainMenu = menuBar()->addMenu(tr("&Keychains"));
    keychainMenu->addAction(quickNewAccountAction);
    keychainMenu->addSeparator();
    keychainMenu->addAction(newKeychainAction);
    keychainMenu->addAction(newAccountAction);
    keychainMenu->addSeparator();
    keychainMenu->addAction(unlockKeychainAction);
    keychainMenu->addAction(lockKeychainAction);
    keychainMenu->addAction(lockAllKeychainsAction);
    keychainMenu->addSeparator()->setText(tr("Import Mode"));
    keychainMenu->addAction(importPrivateAction);
    keychainMenu->addAction(importPublicAction);
    keychainMenu->addSeparator();
    keychainMenu->addAction(importKeychainAction);
    keychainMenu->addAction(exportPrivateKeychainAction);
    keychainMenu->addAction(exportPublicKeychainAction);
    keychainMenu->addSeparator();
    keychainMenu->addAction(backupKeychainAction);

    networkMenu = menuBar()->addMenu(tr("&Network"));
    networkMenu->addAction(connectAction);
    networkMenu->addAction(disconnectAction);
    networkMenu->addSeparator();
    networkMenu->addAction(networkSettingsAction);

    menuBar()->addSeparator();

    fontsMenu = menuBar()->addMenu(tr("Fonts"));
    fontsMenu->addSeparator()->setText(tr("Font Size"));
    fontsMenu->addAction(smallFontsAction);
    fontsMenu->addAction(mediumFontsAction);
    fontsMenu->addAction(largeFontsAction);

    menuBar()->addSeparator();

    helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(aboutAction);
}

void MainWindow::createToolBars()
{
    // TODO: rename toolbars more appropriately

    fileToolBar = addToolBar(tr("File"));
    fileToolBar->addAction(newVaultAction);
    fileToolBar->addAction(openVaultAction);
    fileToolBar->addAction(closeVaultAction);

    accountToolBar = addToolBar(tr("Accounts"));
    accountToolBar->addAction(requestPaymentAction);
    accountToolBar->addAction(sendPaymentAction);

    keychainToolBar = addToolBar(tr("Keychains"));
    keychainToolBar->addAction(quickNewAccountAction);
    keychainToolBar->addAction(newKeychainAction);
    keychainToolBar->addAction(newAccountAction);

    networkToolBar = addToolBar(tr("Network"));
    networkToolBar->addAction(connectAction);
    networkToolBar->addAction(disconnectAction);
}

void MainWindow::createStatusBar()
{
    syncLabel = new QLabel();
    updateSyncLabel();

    networkStateLabel = new QLabel();
    stoppedIcon = new QPixmap(":/icons/vault_status_icons/36x22/nc-icon-36x22.gif");
    synchingMovie = new QMovie(":/icons/vault_status_icons/36x22/synching-icon-animated-36x22.gif");
    synchingMovie->start();
    synchedIcon = new QPixmap(":/icons/vault_status_icons/36x22/synched-icon-36x22.gif");
    networkStateLabel->setPixmap(*stoppedIcon);

    statusBar()->addPermanentWidget(syncLabel);
    statusBar()->addPermanentWidget(networkStateLabel);

    updateStatusMessage(tr("Ready"));
}

void MainWindow::loadSettings()
{
    {
        QSettings settings("Ciphrex", getDefaultSettings().getSettingsRoot());
        QPoint pos = settings.value("pos", QPoint(200, 200)).toPoint();
        QSize size = settings.value("size", QSize(800, 400)).toSize();
        resize(size);
        move(pos);

        int fontSize = settings.value("fontsize", MEDIUM_FONTS).toInt();
        updateFonts(fontSize);

        licenseAccepted = settings.value("licenseaccepted", false).toBool();
    }

    {
        QSettings settings("Ciphrex", getDefaultSettings().getNetworkSettingsPath());
        currencyUnitPrefix = settings.value("currencyunitprefix", "").toString();
        blockTreeFile = settings.value("blocktreefile", getDefaultSettings().getDataDir() + "/blocktree.dat").toString();
        host = settings.value("host", "localhost").toString();
        port = settings.value("port", getCoinParams().default_port()).toInt();
        autoConnect = settings.value("autoconnect", false).toBool();

        setDocDir(settings.value("lastvaultdir", getDefaultSettings().getDocumentDir()).toString());
    }
}

void MainWindow::saveSettings()
{
    {
        QSettings settings("Ciphrex", getDefaultSettings().getSettingsRoot());
        settings.setValue("pos", pos());
        settings.setValue("size", size());
        settings.setValue("licenseaccepted", licenseAccepted);
    }

    {
        QSettings settings("Ciphrex", getDefaultSettings().getNetworkSettingsPath());
        settings.setValue("currencyunitprefix", currencyUnitPrefix);
        settings.setValue("blocktreefile", blockTreeFile);
        settings.setValue("host", host);
        settings.setValue("port", port);
        settings.setValue("autoconnect", autoConnect);
        settings.setValue("lastvaultdir", getDocDir());
    }
}

void MainWindow::clearSettings()
{
    QSettings settings("Ciphrex", getDefaultSettings().getSettingsRoot());
    settings.clear();
    loadSettings();
}

void MainWindow::loadVault(const QString &fileName)
{
    synchedVault.openVault(fileName.toStdString(), false);
    accountModel->getVault()->unlockChainCodes(uchar_vector("1234"));
}

