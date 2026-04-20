#include "mainWindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QInputDialog>
#include <QFile>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {

    // build layout
    deviceList = new QListWidget(this);
    log = new QTextEdit(this);
    log->setReadOnly(true);
    progressBar = new QProgressBar(this);
    progressBar->hide(); // hidden until a transfer starts
    QPushButton *sendButton = new QPushButton("Send File", this);

    // left panel: device list + send button stacked vertically
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->addWidget(deviceList);
    leftLayout->addWidget(sendButton);

    // right panel: log + progress bar stacked vertically
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(log);
    rightLayout->addWidget(progressBar);

    // wrap each side in a QWidget so the splitter can hold them
    QWidget *leftWidget = new QWidget(this);
    leftWidget->setLayout(leftLayout);
    QWidget *rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout);

    // splitter puts them side by side, user can resize
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);

    setCentralWidget(splitter);

    // button
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::onSendFile);

    // get bridge
    bridge = new NetTransferBridge(this);

    // connect signals
    connect(bridge, &NetTransferBridge::deviceFound, this, &MainWindow::onDeviceFound);
    connect(bridge, &NetTransferBridge::deviceLeft, this, &MainWindow::onDeviceLeft);
    connect(bridge, &NetTransferBridge::offerReceived, this, &MainWindow::onOfferReceived);
    connect(bridge, &NetTransferBridge::progressUpdated, this, &MainWindow::onProgress);
    connect(bridge, &NetTransferBridge::transferComplete, this, &MainWindow::onTransferComplete);
    connect(bridge, &NetTransferBridge::newDevicePending, this, &MainWindow::onNewDevice);

    // start
    bridge->start();

    // if first run, ask for name
    while (bridge->getDeviceName().isEmpty()) {
        bool ok;
        QString name = QInputDialog::getText(this, "Welcome", "Enter your device name:", 
                                            QLineEdit::Normal, "", &ok);
        if (!ok) {
            QApplication::quit();
            return;
        }
        if (!name.trimmed().isEmpty()) {
            bridge->setDeviceName(name.trimmed());
        }
    }

}

void MainWindow::onDeviceFound(QString name, QString ip, uint16_t port) {
    deviceList->addItem(name + " - " + ip);
}

void MainWindow::onDeviceLeft(QString name, QString ip, uint16_t port) {
    auto items = deviceList->findItems(name + " - " + ip, Qt::MatchExactly);
    for (auto item : items) delete item;
}

void MainWindow::onOfferReceived(QString fileName, quint64 fileSize, QString senderName) {

    QString message = senderName + " wants to send you:\n" + fileName + 
                      "\nSize: "; //+ QString::number(fileSize) + " bytes";
    
    float file_size_kb = fileSize / 1024;
    float file_size_mb = file_size_kb / 1024;
    float file_size_gb = file_size_mb / 1024;

    if (file_size_gb > 1) {
        message = message + QString::number(file_size_gb) + "GB";
    } else if (file_size_mb > 1) {
        message = message + QString::number(file_size_mb) + "MB";
    } else if (file_size_kb > 1) {
        message = message + QString::number(file_size_kb) + "KB";
    } else {
        message = message + QString::number(fileSize) + " bytes";
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Incoming file", message,
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        bridge->accept();
        progressBar->setValue(0);
        progressBar->show();
    } else {
        bridge->reject(RejectReason::USER_DECLINED);
    }
}

void MainWindow::onProgress(quint64 sent, quint64 total) {
    progressBar->setMinimumHeight(24);
    progressBar->setTextVisible(true);
    progressBar->setFormat("%p%");
    progressBar->setValue((double)sent / total * 100);
}

void MainWindow::onTransferComplete(bool ok) {
    progressBar->hide();
    log->append(ok ? "Transfer complete." : "Transfer failed.");
}

void MainWindow::onNewDevice(QString fingerprint, QString name) {

    QString message = "Unknown device tried to connect:\n\n"
                      "Name: " + name + "\n"
                      "Fingerprint: " + fingerprint + "\n\n"
                      "Trust this device? (They will need to send again)";

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "New device", message,
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        bridge->trustDevice();
    } else {
        bridge->ignorePendingTrust();
    }
}

void MainWindow::onSendFile() {

    // open file picker
    QString filePath = QFileDialog::getOpenFileName(this, "Select file to send");
    if (filePath.isEmpty())
        return; // user cancelled

    // pick a device from the list
    QListWidgetItem *selected = deviceList->currentItem();
    if (!selected) {
        log->append("No device selected.");
        return;
    }

    // get the DiscoveredDevice that matches the selected item
    log->append("Sending to: " + selected->text());
    auto devices = bridge->getDevices();
    QString selectedText = selected->text();
    for (auto &d : devices) {
        if (selectedText == QString::fromStdString(d.name) + " - " + QString::fromStdString(d.ip)) {
            bridge->sendFile(d, filePath.toStdString());
            log->append("Sending " + filePath + " to " + QString::fromStdString(d.name) + "...");
            progressBar->show();
            progressBar->setValue(0);
            return;
        }
    }
    log->append("Could not match selected device.");
}

void MainWindow::closeEvent(QCloseEvent *event) {
    bridge->stop();
    event->accept();
}
