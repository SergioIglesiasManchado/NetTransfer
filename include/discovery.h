#pragma once

#include "asio.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

// time between searchs in seconds
#define SEARCH_LOOP 5
#define BUFFER_MAX_SIZE 512 // size in bytes
#define WAIT_TIME 15 // seconds to clean known device if he has exited the net.

struct DiscoveredDevice {
  std::string name;
  std::string ip;
  uint16_t tcp_port;
  std::chrono::steady_clock::time_point last_seen;
};

class DiscoveryService {

private:
  asio::ip::udp::socket udp_socket;
  std::string device_name;
  uint16_t tcp_port;
  std::vector<DiscoveredDevice> discoveredDevices;
  asio::steady_timer timer;
  uint8_t buffer_incoming[BUFFER_MAX_SIZE];

  void listenForPackages();
  void sendBroadcast();
  void startTimer();
  std::function<void(DiscoveredDevice)> onDeviceFound;
  std::function<void(DiscoveredDevice)> onDeviceLeft;

public:
  DiscoveryService(asio::io_context &io, std::string device_name,
                   uint16_t tcp_port);
  bool start();
  bool stop();
  std::vector<DiscoveredDevice> getDevices();
  void setTcpPort(uint16_t port);
  void setOnDeviceFound(std::function<void(DiscoveredDevice)> callback);
  void setOnDeviceLeft(std::function<void(DiscoveredDevice)> callback);
};
