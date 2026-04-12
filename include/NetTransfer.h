#pragma once

#include "asio.hpp"
#include "asio/ssl.hpp"
#include "discovery.h"
#include "transfer.h"
#include <thread>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "json.hpp"

#define TRUSTED_FILE "trusted_devices.json"
#define KEY_FILE "key.pem"
#define CERT_FILE "cert.pem"

class NetTransfer {

    private:
        asio::io_context io;
        asio::ssl::context ssl_ctx;
        asio::ssl::context client_ctx;
        DiscoveryService discovery;
        TransferReceiver receiver;
        std::thread io_thread;
        std::string device_name;
        uint16_t tcp_port;
        std::string config_directory_path;

        void ensureCertExists(std::string config_directory_path);
        std::string getCertFingerprint(SSL* ssl);
        bool validatePeerCert(SSL* ssl, std::string config_directory_path);
        std::function<void(DiscoveredDevice)> onDeviceFound;
        std::function<void(DiscoveredDevice)> onDeviceLeft;
        std::function<void(uint64_t, uint64_t)> onProgress;
        std::function<void(bool)> onComplete;
        std::function<void(OfferPayload)> onOffer;

    public:
        NetTransfer(std::string device_name, uint16_t tcp_port);
        bool start();
        bool stop();
        void accept();
        void reject(RejectReason reason);
        bool sendFile(DiscoveredDevice target, std::string file_path);
        std::vector<DiscoveredDevice> getDevices(); 

        void setOnDeviceFound(std::function<void(DiscoveredDevice)> callback);
        void setOnDeviceLeft(std::function<void(DiscoveredDevice)> callback);
        void setOnProgress(
            std::function<void(uint64_t bytes_sent, uint64_t total)> callback);
        void setOnComplete(std::function<void(bool success)> callback);
        void setOnOffer(std::function<void(OfferPayload)> callback);

};