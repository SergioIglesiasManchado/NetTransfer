#pragma once

#include <QObject>
#include "NetTransfer.h"

class NetTransferBridge : public QObject {
    Q_OBJECT

    private:
        NetTransfer net;

    public:
        explicit NetTransferBridge(QObject *parent = nullptr);
        bool start();
        bool stop();
        void setDeviceName(const QString &name);
        QString getDeviceName();
        std::vector<DiscoveredDevice> getDevices();
        void accept();
        void reject(RejectReason reason);
        void trustDevice();
        void ignorePendingTrust();
        void sendFile(DiscoveredDevice target, std::string filePath);

    signals:
        void deviceFound(QString name, QString ip, uint16_t port);
        void deviceLeft(QString name, QString ip, uint16_t port);
        void offerReceived(QString fileName, quint64 fileSize, QString senderName);
        void progressUpdated(quint64 sent, quint64 total);
        void transferComplete(bool ok);
        void newDevicePending(QString fingerprint, QString name);
};