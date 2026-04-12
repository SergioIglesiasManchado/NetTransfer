
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

    // check if keys exist
    // TODO change trusted_file path in future
    ensureCertExists("");

    // load ssl keys
    ssl_ctx.set_verify_mode(asio::ssl::verify_none);
    client_ctx.set_verify_mode(asio::ssl::verify_none);
    try {
        ssl_ctx.use_certificate_file("cert.pem", asio::ssl::context::pem);
        ssl_ctx.use_private_key_file("key.pem", asio::ssl::context::pem);
       
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

    // load our ssl to the receiver
    // TODO sending path to trusted file, change in future
    receiver.setOnValidateCert([this](SSL* ssl) {
        return validatePeerCert(ssl, "");
    });

    // launch io thread
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
    // TODO sending trusted path, fix in the future
    sender->setOnValidateCert([this](SSL* ssl) {
        return validatePeerCert(ssl, "");
    });
    if (onProgress) sender->setOnProgress(onProgress);
    sender->setOnComplete([this, sender](bool success) {
        if (onComplete) onComplete(success);
    });
    bool sent = sender->start();
    return sent;
}

void NetTransfer::ensureCertExists(std::string config_directory_path) {

    if (std::filesystem::exists(config_directory_path + KEY_FILE) && std::filesystem::exists(config_directory_path + CERT_FILE)) {
        return;
    }

    // generate key file
    EVP_PKEY* pkey = EVP_PKEY_new();
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1); // P-256
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);

    // write to key.pem
    // DEBUG TOD delete this
    std::cout << "writing key to: " << (config_directory_path + KEY_FILE) << "\n";
    std::cout << "current directory: " << std::filesystem::current_path() << "\n";
    FILE* f = fopen((config_directory_path + KEY_FILE).c_str(), "wb");
    if (!f) {
        std::cerr << "failed to write key.pem\n";
        EVP_PKEY_free(pkey);
        return;
    }
    PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(f);

    // generate cert file
    X509* cert = X509_new();

    // set serial number
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);

    // set validity period (365 days)
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 31536000L); // 365 days in seconds

    // set public key
    X509_set_pubkey(cert, pkey);

    // set subject name
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        (unsigned char*)device_name.c_str(), -1, -1, 0);

    // self-signed — issuer = subject
    X509_set_issuer_name(cert, name);

    // sign with private key
    X509_sign(cert, pkey, EVP_sha256());

    // write to cert.pem
    f = fopen((config_directory_path + CERT_FILE).c_str(), "wb");
    if (!f) {
        std::cerr << "failed to write cert.pem\n";
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return;
    }
    PEM_write_X509(f, cert);
    fclose(f);

    // cleanup
    X509_free(cert);
    EVP_PKEY_free(pkey);
}

std::string NetTransfer::getCertFingerprint(SSL* ssl) {

    // get certificate and compute sha256
    X509* cert = SSL_get1_peer_certificate(ssl);
    if (!cert) return ""; // peer didn't return certificate
    unsigned char buffer[32];
    unsigned int length;
    int ok = X509_digest(cert, EVP_sha256(), buffer, &length);
    if (ok == 0) return "";

    // get hex string 
    std::stringstream fingerprint;
    for (int i = 0; i < length; i++) {
        if (i > 0) fingerprint << ":";
        fingerprint << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i];
    }
    X509_free(cert);
    return fingerprint.str();
}

bool NetTransfer::validatePeerCert(SSL* ssl, std::string config_directory_path) {

    // tries to load trustedDevices.json
    std::ifstream f(config_directory_path + TRUSTED_FILE);

    // opens json and advances to trust zone
    nlohmann::json trust_store = nlohmann::json::object();
    if (f.is_open()) {
        try {
        f >> trust_store;
        } catch (std::exception& e) {
            std::cerr << "trust store corrupted, starting fresh\n";
            trust_store = nlohmann::json::object();
        }
    }

    // get fingerprit from peer
    std::string fingerprint = getCertFingerprint(ssl);
    if (fingerprint.empty()) return true;

    // check if fingerint is in json
    if (trust_store.contains(fingerprint)) {
        // known device, allow
        return true;
    } else {
        // new device, add and allow (TOFU)
        trust_store[fingerprint] = true;
        // save updated trust store
        std::ofstream out(config_directory_path + TRUSTED_FILE);
        if (!out.is_open()) {
            std::cerr << "failed to save trust store\n";
            return true;  // still allow, just couldn't save
        }
        out << trust_store.dump(4);  // pretty print with 4 spaces
        std::cout << "New device trusted: " << fingerprint << "\n";
        return true;
    }
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