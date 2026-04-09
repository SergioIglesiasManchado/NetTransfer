
#include "asio.hpp"
#include "protocol.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>

#define MAX_BUFFER_SIZE 4096

class TransferSender {

private:
  asio::ip::tcp::socket target_tcp_port;
  asio::io_context io_context;
  asio::io target_ip;
  std::string file_path;

  uint8_t buffer[MAX_BUFFER_SIZE];

public:
  void setOnProgress(std::function<void(uint64_t bytes_sent, uint64_t total)>);
  void setOnComplete(std::function<void(bool success)>);

  bool start();
  bool stop();
};

class TransferReceiver {

private:
  asio::io_context io_context;
  asio::ip::tcp::socket tcp_port;

  uint8_t buffer[MAX_BUFFER_SIZE];

public:
  void setOnProgress(std::function<void(uint64_t bytes_sent, uint64_t total)>);
  void setOnComplete(std::function<void(bool success)>);
  void setOnOffer(std::function<void(OfferPayload)>);
  void accept(uint64_t resume_offset);
  void reject(RejectReason reason);

  bool start();
  bool stop();
};
