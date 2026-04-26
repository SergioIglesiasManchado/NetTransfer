#include "NetTransferBridge.h"

NetTransferBridge::NetTransferBridge(QObject *parent) : QObject(parent) {}

void NetTransferBridge::setDeviceName(const QString &name) {
    net.setDeviceName(name.toStdString());
}

QString NetTransferBridge::getDeviceName() {
    return QString::fromStdString(net.getDeviceName());
}

bool NetTransferBridge::start() {

    // set all callbacks
    net.setOnDeviceFound([this](DiscoveredDevice d) {
        QMetaObject::invokeMethod(this, [this, d]() {
            emit deviceFound(QString::fromStdString(d.name),
                            QString::fromStdString(d.ip),
                            d.tcp_port);
        }, Qt::QueuedConnection);
    });

    net.setOnOffer([this](OfferPayload offer) {
        QMetaObject::invokeMethod(this, [this, offer]() {
            emit offerReceived(QString::fromStdString(offer.file_name), offer.file_size, QString::fromStdString(offer.device_name));
        }, Qt::QueuedConnection);
    });

    net.setOnProgress([this](uint64_t sent, uint64_t total) {
        QMetaObject::invokeMethod(this, [this, sent, total]() {
            emit progressUpdated(sent, total);
        }, Qt::QueuedConnection);
    });

    net.setOnComplete([this](bool ok, std::string path) {
        QMetaObject::invokeMethod(this, [this, ok, path]() {
            emit transferComplete(ok, QString::fromStdString(path));
        }, Qt::QueuedConnection);
    });

    /** 
    net.setOnFirstRun([this]() {
        return net.getDeviceName();
    });
    */

    net.setOnNewDevice([this](std::string fingerprint, std::string name) {
        QMetaObject::invokeMethod(this, [this, fingerprint, name]() {
            emit newDevicePending(QString::fromStdString(fingerprint), QString::fromStdString(name));
        }, Qt::QueuedConnection);
        return true;
    });

    net.setOnDeviceLeft([this](DiscoveredDevice d) {
        QMetaObject::invokeMethod(this, [this, d]() {
            emit deviceLeft(QString::fromStdString(d.name),
                            QString::fromStdString(d.ip),
                            d.tcp_port);
        }, Qt::QueuedConnection);
    });

    return net.start();

}

bool NetTransferBridge::stop() {
    return net.stop();
}

std::vector<DiscoveredDevice> NetTransferBridge::getDevices() {
    return net.getDevices();
}

void NetTransferBridge::accept() {
    net.accept();
}

void NetTransferBridge::reject(RejectReason reason) {
    net.reject(reason);
}

void NetTransferBridge::trustDevice() {
    net.trustDevice();
}

void NetTransferBridge::ignorePendingTrust() {
    net.ignorePendingTrust();
}

void NetTransferBridge::sendFile(DiscoveredDevice target, std::string filePath) {
    net.sendFile(target, filePath);
}