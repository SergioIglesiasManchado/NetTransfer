
#include "NetTransfer.h"

NetTransfer::NetTransfer(std::string device_name, uint16_t tcp_port)
    : ssl_ctx(asio::ssl::context::tls),
      discovery(io, device_name, tcp_port),
      receiver(io, ssl_ctx, tcp_port),
      device_name(device_name),
      tcp_port(tcp_port) {}

bool NetTransfer::start() {

    // load ssl
    ssl_ctx.use_certificate_file("cert.pem", asio::ssl::context::pem);
    ssl_ctx.use_private_key_file("key.pem", asio::ssl::context::pem);

    // callbacks
    discovery.setOnDeviceFound(onDeviceFound);
    discovery.setOnDeviceLeft(onDeviceLeft);
    receiver.setOnOffer(onOffer);
    receiver.setOnProgress(onProgress);
    receiver.setOnComplete(onComplete);

    // start discovery and receiver
    bool fine = discovery.start();
    if (!fine) return false;
    fine = receiver.start();
    if (!fine) return false;

    io_thread = std::thread([this]() {
        io.run();
    });

    return true;
}

bool NetTransfer::stop() {

    bool fine = discovery.stop();
    if (!fine) return false;
    fine = receiver.stop();
    if (!fine) return false;
    io.stop();
    io_thread.join();

    return true;
}

bool NetTransfer::sendFile(DiscoveredDevice target, std::string file_path) {

    // get device info from discoveryDevice, call callbacks and start sender
    auto sender = std::make_shared<TransferSender>(io, ssl_ctx, target.ip, target.tcp_port, file_path);
    if (onProgress) sender->setOnProgress(onProgress);
    if (onComplete) sender->setOnComplete([this, sender](bool success) {
        if (onComplete) onComplete(success);
    });
    bool sent = sender->start();
    return sent;
}

void NetTransfer::setOnDeviceFound(
    std::function<void(DiscoveredDevice)> callback) {
  onDeviceFound = callback;
}

void NetTransfer::setOnDeviceLeft(
    std::function<void(DiscoveredDevice)> callback) {
  onDeviceLeft = callback;
}

void NetTransfer::setOnProgress(
    std::function<void(uint64_t, uint64_t)> callback) {
  onProgress = callback;
}

void NetTransfer::setOnComplete(std::function<void(bool)> callback) {
  onComplete = callback;
}

void NetTransfer::setOnOffer(std::function<void(OfferPayload)> callback) {
  onOffer = callback;
}