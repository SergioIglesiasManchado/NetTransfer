#pragma once

#include "asio.hpp"
#include "asio/ssl.hpp"
#include "protocol.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <openssl/evp.h>

#define MAX_BUFFER_SIZE 32768 // size in bytes

class TransferSender {

private:
  asio::io_context &io_context;
  asio::ssl::stream<asio::ip::tcp::socket> socket;
  std::string target_ip;
  uint16_t target_port;
  std::string file_path;
  uint64_t session_id;
  std::fstream file;
  unsigned char sha256[32];
  uint64_t file_size;
  uint64_t bytes_sent;

  uint8_t buffer[MAX_BUFFER_SIZE];

  void sendNextChunk();
  std::function<void(uint64_t, uint64_t)> onProgress;
  std::function<void(bool)> onComplete;

public:
  TransferSender(asio::io_context &io, asio::ssl::context &ssl_ctx,
                 std::string target_ip, uint16_t target_port,
                 std::string file_path);

  void setOnProgress(
      std::function<void(uint64_t bytes_sent, uint64_t total)> callback);
  void setOnComplete(std::function<void(bool success)> callback);

  bool start();
  bool stop();
};

class TransferReceiver {

private:
  asio::io_context &io_context;
  asio::ssl::stream<asio::ip::tcp::socket> socket;
  asio::ip::tcp::acceptor acceptor;
  OfferPayload pending_offer;
  asio::ip::tcp::endpoint pending_sender;
  uint64_t pending_session_id;
  std::fstream file;
  std::string file_path;
  uint64_t file_size;
  uint64_t bytes_sent;
  uint16_t listen_port;

  uint8_t buffer[MAX_BUFFER_SIZE];

  void listenForConnections();
  void receiveNextChunk();
  std::function<void(uint64_t, uint64_t)> onProgress;
  std::function<void(bool)> onComplete;
  std::function<void(OfferPayload)> onOffer;

public:
  TransferReceiver(asio::io_context &io, asio::ssl::context &ssl_ctx,
                   uint16_t listen_port);

  void setOnProgress(
      std::function<void(uint64_t bytes_sent, uint64_t total)> callback);
  void setOnComplete(std::function<void(bool success)> callback);
  void setOnOffer(std::function<void(OfferPayload)> callback);
  void accept(uint64_t resume_offset);
  void reject(RejectReason reason);
  uint16_t TransferReceiver::getPort();

  bool start();
  bool stop();
};
