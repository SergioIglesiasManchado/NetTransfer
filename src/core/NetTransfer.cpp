
#include "NetTransfer.h"

NetTransfer::NetTransfer()
    : ssl_ctx(asio::ssl::context::tls), client_ctx(asio::ssl::context::tls),
      strand(asio::make_strand(io)) {
  config_directory_path = getConfigPath();
}

bool NetTransfer::start() {

  // load config
  bool loaded = loadConfig();
  if (!loaded) {
    // first run — ask user for name via callback
    if (onFirstRun) {
      config.device_name = onFirstRun();
    } else {
      config.device_name = "NetTransfer"; // fallback default
    }
    config.tcp_port = TCP_PORT_MIN; // autopicks port
  }

  // initialize discovery and receiver after getting tcp port from config
  discovery = std::make_unique<DiscoveryService>(io, config.device_name,
                                                 config.tcp_port);
  receiver = std::make_unique<TransferReceiver>(io, ssl_ctx, config.tcp_port);

  // check if keys exist
  // TODO change trusted_file path in future
  ensureCertExists();

  try {
    ssl_ctx.use_certificate_file(config_directory_path + "cert.pem",
                                 asio::ssl::context::pem);
    ssl_ctx.use_private_key_file(config_directory_path + "key.pem",
                                 asio::ssl::context::pem);
    client_ctx.use_certificate_file(config_directory_path + "cert.pem",
                                    asio::ssl::context::pem);
    client_ctx.use_private_key_file(config_directory_path + "key.pem",
                                    asio::ssl::context::pem);
    SSL_CTX_set_verify(ssl_ctx.native_handle(),
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       nullptr);
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

  /** Dummy callback that lets the handshake finish so we can check the
  fingerprint auto dummy_verify = [](bool preverify_ok,
  asio::ssl::verify_context& ctx) {
      // just tell openssl that we'll be verifying it, not him
      return true;
  };
  */
  // for server
  ssl_ctx.set_verify_mode(asio::ssl::verify_peer |
                          asio::ssl::verify_client_once);
  ssl_ctx.set_verify_callback(
      [](bool preverify_ok, asio::ssl::verify_context &ctx) {
        return true; // Let the handshake finish so we can validate manually
      });

  // for client
  client_ctx.set_verify_mode(asio::ssl::verify_peer);
  client_ctx.set_verify_callback(
      [](bool preverify_ok, asio::ssl::verify_context &ctx) { return true; });

  // callbacks
  discovery->setOnDeviceFound([this](DiscoveredDevice d) {
    asio::post(strand, [this, d]() {
      if (onDeviceFound)
        onDeviceFound(d);
    });
  });
  discovery->setOnDeviceLeft([this](DiscoveredDevice d) {
    asio::post(strand, [this, d]() {
      if (onDeviceLeft)
        onDeviceLeft(d);
    });
  });
  receiver->setOnOffer([this](OfferPayload op) {
    asio::post(strand, [this, op]() {
      if (onOffer)
        onOffer(op);
    });
  });
  receiver->setOnProgress([this](uint64_t bytes_sent, uint64_t total) {
    asio::post(strand, [this, bytes_sent, total]() {
      if (onProgress)
        onProgress(bytes_sent, total);
    });
  });
  receiver->setOnComplete([this](bool success) {
    asio::post(strand, [this, success]() {
      if (onComplete)
        onComplete(success);
    });
  });
  receiver->setOnValidateCert([this](SSL *ssl, std::string device_name) {
    return validatePeerCert(ssl, device_name);
  });
  // start discovery and receiver
  bool fine = discovery->start();
  if (!fine)
    return false;
  fine = receiver->start();
  if (!fine)
    return false;
  discovery->setTcpPort(receiver->getPort());

  // save actual port used and save config
  config.tcp_port = receiver->getPort();
  saveConfig();

  // launch io thread
  io_thread = std::thread([this]() { io.run(); });

  return true;
}

bool NetTransfer::stop() {

  bool fine = discovery->stop();
  if (!fine)
    return false;
  fine = receiver->stop();
  if (!fine)
    return false;
  if (active_sender) {
    active_sender->stop();
  }
  io.stop();
  io_thread.join();

  return true;
}

bool NetTransfer::sendFile(DiscoveredDevice target, std::string file_path) {

  // get device info from discoveryDevice, call callbacks and start sender
  // TODO delete, is DEBUG
  std::cout << "sending file called: " << file_path << "\n";
  active_sender = std::make_shared<TransferSender>(io, client_ctx, target.ip,
                                                   target.tcp_port, file_path,
                                                   config.device_name);

  if (onProgress) active_sender->setOnProgress(onProgress);
  active_sender->setOnComplete([this](bool success) {
    if (onComplete) onComplete(success);
  });
  asio::post(io, [this]() {
        active_sender->start();
  });
  return true;
}

void NetTransfer::ensureCertExists() {

  if (std::filesystem::exists(config_directory_path + KEY_FILE) &&
      std::filesystem::exists(config_directory_path + CERT_FILE)) {
    return;
  }

  // generate key file
  EVP_PKEY *pkey = EVP_PKEY_new();
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
  EVP_PKEY_keygen_init(ctx);
  EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1); // P-256
  EVP_PKEY_keygen(ctx, &pkey);
  EVP_PKEY_CTX_free(ctx);

  // write to key.pem
  FILE *f = fopen((config_directory_path + KEY_FILE).c_str(), "wb");
  if (!f) {
    std::cerr << "failed to write key.pem\n";
    EVP_PKEY_free(pkey);
    return;
  }
  PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
  fclose(f);

  // generate cert file
  X509 *cert = X509_new();

  // set serial number
  ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);

  // set validity period (365 days)
  X509_gmtime_adj(X509_get_notBefore(cert), 0);
  X509_gmtime_adj(X509_get_notAfter(cert), 31536000L); // 365 days in seconds

  // set public key
  X509_set_pubkey(cert, pkey);

  // set subject name
  X509_NAME *name = X509_get_subject_name(cert);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             (unsigned char *)config.device_name.c_str(), -1,
                             -1, 0);

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

std::string NetTransfer::getCertFingerprint(SSL *ssl) {

  // get certificate and compute sha256
  X509 *cert = SSL_get1_peer_certificate(ssl);
  if (!cert)
    return ""; // peer didn't return certificate
  unsigned char buffer[32];
  unsigned int length;
  int ok = X509_digest(cert, EVP_sha256(), buffer, &length);
  if (ok == 0)
    return "";

  // get hex string
  std::stringstream fingerprint;
  for (int i = 0; i < length; i++) {
    if (i > 0)
      fingerprint << ":";
    fingerprint << std::hex << std::setw(2) << std::setfill('0')
                << (int)buffer[i];
  }
  X509_free(cert);
  return fingerprint.str();
}

bool NetTransfer::validatePeerCert(SSL *ssl, std::string device_name) {

  // tries to load trusted_devices.json
  std::ifstream f(config_directory_path + TRUSTED_FILE);
  nlohmann::json trust_store = nlohmann::json::object();
  if (f.is_open()) {
    try {
      f >> trust_store;
    } catch (std::exception &e) {
      std::cerr << "trust store corrupted, starting fresh\n";
      trust_store = nlohmann::json::object();
    }
  }

  // get fingerprint from peer
  std::string fingerprint = getCertFingerprint(ssl);
  if (fingerprint.empty())
    return false; // no cert, no deal

  // check if fingerprint is known
  if (trust_store.contains(fingerprint)) {
    // known device  allow
    return true;
  }

  // unknown device — queue for user decision, reject
  // TODO if user sending, if validate device autosend again
  {
    std::lock_guard<std::mutex> lock(state_mutex);
    pending_trust_fingerprint = fingerprint;
    pending_trust_device_name = device_name;
  }
  pending_trust_flag = true;
  if (onNewDevice)
    onNewDevice(fingerprint, device_name);
  return false;
}

bool NetTransfer::loadConfig() {

  std::ifstream f(config_directory_path + CONFIG_FILE);
  if (f.is_open()) {
    // load config
    nlohmann::json conf = nlohmann::json::object();
    try {
      f >> conf;
    } catch (std::exception &e) {
      std::cerr << "config corrupted, starting fresh\n";
      return false;
    }
    if (!conf.contains("device_name") || !conf.contains("tcp_port")) {
      std::cerr << "config missing fields, starting fresh\n";
      return false;
    }
    config.device_name = conf["device_name"];
    config.tcp_port = conf["tcp_port"];
    return true;
  } else {
    return false;
  }
}

bool NetTransfer::saveConfig() {

  std::ofstream f(config_directory_path + CONFIG_FILE);
  if (!f.is_open()) {
    return false;
  }
  nlohmann::json conf = nlohmann::json::object();
  conf["device_name"] = config.device_name;
  conf["tcp_port"] = config.tcp_port;
  f << conf.dump(4);
  return true;
}

std::string NetTransfer::getConfigPath() {

  std::filesystem::path config_path;
#ifdef _WIN32
  // get for windows
  PWSTR path = nullptr;
  SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path);
  config_path = std::filesystem::path(path) / "NetTransfer";
  CoTaskMemFree(path);
#else
  // get for linux
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg)
    config_path = std::filesystem::path(xdg) / "NetTransfer";
  else
    config_path =
        std::filesystem::path(getenv("HOME")) / ".config" / "NetTransfer";
#endif

  std::filesystem::create_directories(config_path); // create if don't exist
  return (config_path / "").string();
}

std::vector<DiscoveredDevice> NetTransfer::getDevices() {
  std::lock_guard<std::mutex> lock(state_mutex);
  return discovery->getDevices();
}

void NetTransfer::accept() {
  asio::post(strand, [this]() { receiver->accept(0); });
}

void NetTransfer::reject(RejectReason reason) {
  asio::post(strand, [this, reason]() { receiver->reject(reason); });
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

void NetTransfer::setOnFirstRun(std::function<std::string()> callback) {
  onFirstRun = callback;
}

void NetTransfer::setOnNewDevice(
    std::function<bool(std::string, std::string)> callback) {
  onNewDevice = callback;
}

std::string NetTransfer::getDeviceName() {
  return config.device_name;
}

void NetTransfer::trustDevice() {
  if (!pending_trust_flag)
    return;

  std::ifstream f(config_directory_path + TRUSTED_FILE);
  nlohmann::json trust_store = nlohmann::json::object();
  if (f.is_open()) {
    try {
      f >> trust_store;
    } catch (...) {
    }
  }

  trust_store[pending_trust_fingerprint] = pending_trust_device_name;

  std::ofstream out(config_directory_path + TRUSTED_FILE);
  out << trust_store.dump(4);

  pending_trust_flag = false;
  std::cout << "Device " << pending_trust_device_name << " trusted.\n";
}

bool NetTransfer::hasPendingTrust() { return pending_trust_flag; }

void NetTransfer::ignorePendingTrust() { pending_trust_flag = false; }

void NetTransfer::setDeviceName(std::string new_device_name) {

  // change name
  config.device_name = new_device_name;

  // update config file
  bool check;
  if (!(check = saveConfig())) {
    std::cerr << "setDeviceName: couldn't update config\n";
  }
}
