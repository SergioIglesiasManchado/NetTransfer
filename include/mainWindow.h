#pragma once

#include "NetTransferBridge.h"
#include <QMainWindow>
#include <QListWidget>
#include <QTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QInputDialog>
#include <QFile>
#include <QCloseEvent>

class MainWindow : public QMainWindow {
    Q_OBJECT

    private:
        NetTransferBridge *bridge;
        QListWidget *deviceList;
        QTextEdit *log;
        QProgressBar *progressBar;

    private slots:
        void onDeviceFound(QString name, QString ip, uint16_t port);
        void onDeviceLeft(QString name, QString ip, uint16_t port);
        void onOfferReceived(QString fileName, quint64 fileSize, QString senderName);
        void onProgress(quint64 sent, quint64 total);
        void onTransferComplete(bool ok);
        void onNewDevice(QString fingerprint, QString name);
        void onSendFile();

    public:
        explicit MainWindow(QWidget *parent = nullptr);
    
    protected:
        void closeEvent(QCloseEvent *event) override;
};