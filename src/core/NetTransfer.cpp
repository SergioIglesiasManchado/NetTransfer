
#include "NetTransfer.h"

NetTransfer::NetTransfer(std::string device_name, uint16_t tcp_port)
    : ssl_ctx(asio::ssl::context::tls),
      client_ctx(asio::ssl::context::tls), // copilot changed, before one ssl, now ssl (server) and client
      discovery(io, device_name, tcp_port),       // to undo, delete here, in .h, in start() and in sendFile()
      receiver(io, ssl_ctx, tcp_port),
      device_name(device_name),
      tcp_port(tcp_port) {
}

bool NetTransfer::start() {

    // load ssl
    ssl_ctx.set_verify_mode(asio::ssl::verify_none);
    client_ctx.set_verify_mode(asio::ssl::verify_none);
    try {
        ssl_ctx.use_certificate_file("cert.pem", asio::ssl::context::pem);
        ssl_ctx.use_private_key_file("key.pem", asio::ssl::context::pem);
        /**
        if (!SSL_CTX_check_private_key(ssl_ctx.native_handle())) {
            std::cerr << "private key does not match certificate\n";
            return false;
        }
        std::cout << "SSL cert loaded OK\n";  // DEBUG
        client_ctx.use_certificate_file("cert.pem", asio::ssl::context::pem);
        client_ctx.use_private_key_file("key.pem", asio::ssl::context::pem);
        std::cout << "SSL client ctx loaded OK\n";  // DEBUG
        // DEBUG TODO probably delete this
        SSL_CTX_set_mode(ssl_ctx.native_handle(), SSL_MODE_AUTO_RETRY);
        SSL_CTX_set_mode(client_ctx.native_handle(), SSL_MODE_AUTO_RETRY);
        SSL_CTX_set_security_level(ssl_ctx.native_handle(), 0);
        SSL_CTX_set_security_level(client_ctx.native_handle(), 0);
        SSL_CTX_set_cipher_list(ssl_ctx.native_handle(), "ALL:@SECLEVEL=0");
        SSL_CTX_set_cipher_list(client_ctx.native_handle(), "ALL:@SECLEVEL=0");
        SSL_CTX_set_ciphersuites(ssl_ctx.native_handle(), "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
        SSL_CTX_set_ciphersuites(client_ctx.native_handle(), "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
         */
       
    } catch (std::exception &e) {
        std::cout << "ssl setup failed: " << e.what() << "\n";
        unsigned long err;
        while ((err = ERR_get_error()) != 0) {
            char buf[256];
            ERR_error_string_n(err, buf, sizeof(buf));
            std::cout << "OpenSSL error: " << buf << "\n";
        }
        return false;
    }

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
    discovery.setTcpPort(receiver.getPort());

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
    // TODO delete, is DEBUG
    std::cout << "sending file called: " << file_path << "\n";
    auto sender = std::make_shared<TransferSender>(io, client_ctx, target.ip, target.tcp_port, file_path);
    if (onProgress) sender->setOnProgress(onProgress);
    sender->setOnComplete([this, sender](bool success) {
        if (onComplete) onComplete(success);
    });
    bool sent = sender->start();
    return sent;
}

std::vector<DiscoveredDevice> NetTransfer::getDevices() {
    return discovery.getDevices();
}

void NetTransfer::accept() { 
    asio::post(io, [this]() { receiver.accept(0); });
}

void NetTransfer::reject(RejectReason reason) { 
    asio::post(io, [this, reason]() { receiver.reject(reason); });
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