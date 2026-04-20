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

#ifdef _WIN32
    #include <ShlObj.h>
#endif

#define TRUSTED_FILE "trusted_devices.json"
#define KEY_FILE "key.pem"
#define CERT_FILE "cert.pem"
#define CONFIG_FILE "config.json"

struct Config {
    std::string device_name;
    uint16_t tcp_port;
};


class NetTransfer {

    private:
        asio::io_context io;
        asio::ssl::context ssl_ctx;
        asio::ssl::context client_ctx;
        std::unique_ptr<DiscoveryService> discovery;
        std::unique_ptr<TransferReceiver> receiver;
        std::shared_ptr<TransferSender> active_sender;
        asio::strand<asio::io_context::executor_type> strand; // for callbacks not stepping into each other
        std::thread io_thread;
        //std::string device_name;
        uint16_t tcp_port;
        std::mutex state_mutex; // for getDevices and hasPendingTrust to not interfiere

        Config config;
        std::atomic<bool> pending_trust_flag{false};
        std::string pending_trust_fingerprint;
        std::string pending_trust_device_name;
        std::string config_directory_path;

        void ensureCertExists();
        std::string getCertFingerprint(SSL* ssl);
        bool validatePeerCert(SSL* ssl, std::string device_name);
        bool loadConfig();
        bool saveConfig();
        std::string getConfigPath();
        std::function<void(DiscoveredDevice)> onDeviceFound;
        std::function<void(DiscoveredDevice)> onDeviceLeft;
        std::function<void(uint64_t, uint64_t)> onProgress;
        std::function<void(bool)> onComplete;
        std::function<void(OfferPayload)> onOffer;
        std::function<bool(std::string fingerprint, std::string device_name)> onNewDevice;
        std::function<std::string()> onFirstRun;

    public:
        NetTransfer();
        ~NetTransfer();
        bool start();
        bool stop();
        void accept();
        void reject(RejectReason reason);
        bool sendFile(DiscoveredDevice target, std::string file_path);
        std::vector<DiscoveredDevice> getDevices(); 
        std::string getDeviceName();
        uint16_t getDevicePort();
        void setDeviceName(std::string new_device_name);
        void trustDevice();
        bool hasPendingTrust();
        void ignorePendingTrust();

        void setOnDeviceFound(std::function<void(DiscoveredDevice)> callback);
        void setOnDeviceLeft(std::function<void(DiscoveredDevice)> callback);
        void setOnProgress(
            std::function<void(uint64_t bytes_sent, uint64_t total)> callback);
        void setOnComplete(std::function<void(bool success)> callback);
        void setOnOffer(std::function<void(OfferPayload)> callback);
        void setOnNewDevice(std::function<bool(std::string, std::string)> callback);
        void setOnFirstRun(std::function<std::string()> callback);
};