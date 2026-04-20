#include "discovery.h"
#include "protocol.h"
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

/**
 * Constructor, assing variables to the class
 */
DiscoveryService::DiscoveryService(asio::io_context &io,
                                   std::string device_name, uint16_t tcp_port)
    : udp_socket(io), timer(io), device_name(device_name), tcp_port(tcp_port) {}

/**
 * Start function
 * This will open the socket, set the broadcast to true, bind to ip, start
 * listening to incoming packages and start sending the discovery payload every
 * 5s
 * @return true if everything is all right, false if there was a problem
 */
bool DiscoveryService::start() {

  try {
    // open socket
    udp_socket.open(asio::ip::udp::v4());

    // set broadcast
    asio::socket_base::broadcast option(true);
    udp_socket.set_option(option);
    udp_socket.set_option(asio::ip::udp::socket::reuse_address(true));

    // bind to ip
    asio::ip::udp::endpoint endpoint(asio::ip::make_address("0.0.0.0"),
                                     UDP_DISCOVERY_PORT);
    udp_socket.bind(endpoint);

    // start listening for incoming packages
    listenForPackages();

    // send first broadcast
    sendBroadcast();

    // set timer
    startTimer();

  } catch (std::exception &e) {
    std::cerr << e.what() << "\n";
    return false;
  }

  return true;
}

bool DiscoveryService::stop() {

  try {
    // close socket
    udp_socket.close();

    // stop timer
    timer.cancel();

  } catch (std::exception &e) {
    std::cerr << e.what() << "\n";
    return false;
  }

  return true;
}

std::vector<DiscoveredDevice> DiscoveryService::getDevices() {

  // get actual time
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

  // if last seen > WAIT_TIME
  for (int i = 0; i < discoveredDevices.size(); i++) {
    if ((discoveredDevices[i].last_seen + std::chrono::seconds(WAIT_TIME)) <
        now) {
      DiscoveredDevice dd = discoveredDevices[i];
      discoveredDevices.erase(discoveredDevices.begin() + i);
      onDeviceLeft(dd); // trigger ondeviceleft to warn higher lvl
      i--;
    }
  }

  return discoveredDevices;
}

void DiscoveryService::setOnDeviceFound(
    std::function<void(DiscoveredDevice)> callback) {
  onDeviceFound = callback;
}

void DiscoveryService::setOnDeviceLeft(
    std::function<void(DiscoveredDevice)> callback) {
  onDeviceLeft = callback;
}

void DiscoveryService::startTimer() {

  // timer expires after that time (5s or whatever it says in discovery.h)
  timer.expires_after(std::chrono::seconds(SEARCH_LOOP));

  // lambda recall, every time the function is needed
  timer.async_wait([this](std::error_code ec) {
    if (ec)
      return;
    sendBroadcast();
    startTimer();
  });
}

void DiscoveryService::sendBroadcast() {

  try {
    // send discovery payload to the net
    BaseHeader base;
    base.magic = NT_MAGIC;
    base.version = NT_VERSION;
    base.msg_type = MessageType::DISCOVERY_BROADCAST;
    base.session_id = 0;
    base.header_crc = 0;

    DiscoveryPayload dp;
    dp.device_name = device_name;
    dp.tcp_port = tcp_port;

    std::vector<uint8_t> payload = serializeDiscovery(dp);

    base.payload_len = static_cast<uint32_t>(payload.size());

    std::vector<uint8_t> message = serializeHeader(base);
    message.insert(message.end(), payload.begin(), payload.end());

    asio::ip::udp::endpoint endpoint(asio::ip::make_address("255.255.255.255"),
                                     UDP_DISCOVERY_PORT);

    udp_socket.send_to(asio::buffer(message), endpoint);

  } catch (std::exception &e) {
    std::cerr << e.what() << "\n";
  }
}

void DiscoveryService::listenForPackages() {

  auto sender = std::make_shared<asio::ip::udp::endpoint>();

  // listen for udp broadcasts from other devices
  udp_socket.async_receive_from(
      asio::buffer(buffer_incoming, BUFFER_MAX_SIZE), *sender,
      [this, sender](std::error_code ec, size_t bytes) {
        // check for errors in lambda
        if (ec) {
            if (ec != asio::error::operation_aborted)
                std::cerr << ec.message() << "\n";
            listenForPackages();
            return;
        }
        if (sender->address() == udp_socket.local_endpoint().address()) {
          listenForPackages();
          return;
        }

        // first, read header, identify type of packet (either broadcast or
        // response to our broadcast)
        BaseHeader base;
        if (!deserializeHeader(buffer_incoming, base)) {
          // error, throw exception
          std::cerr << "failed to deserialize header\n";
          listenForPackages();
          return;
        }
        switch (base.msg_type) {
        case MessageType::DISCOVERY_BROADCAST: {
          // case we receive a broadcast
          DiscoveryPayload dp;
          if (!deserializeDiscovery(buffer_incoming + HEADER_SIZE,
                                    base.payload_len, dp)) {
            std::cerr << "failed to deserialize payload\n";
            listenForPackages();
            return;
          }
          // check if it's mysef
          if (dp.device_name == device_name) {
            listenForPackages();
            return;
          }
          // if device in list, update last_seen, if not, add to list
          bool deviceInList = false;
          for (int i = 0; i < discoveredDevices.size(); i++) {
            if (discoveredDevices[i].name == dp.device_name) {
              discoveredDevices[i].last_seen = std::chrono::steady_clock::now();
              deviceInList = true;
              break;
            }
          }
          if (!deviceInList) {
            DiscoveredDevice dd;
            dd.name = dp.device_name;
            dd.tcp_port = dp.tcp_port;
            dd.ip = sender->address().to_string();
            dd.last_seen = std::chrono::steady_clock::now();
            discoveredDevices.push_back(dd);
            onDeviceFound(dd);
          }

          // reply
          DiscoveryPayload out;
          out.device_name = device_name;
          out.tcp_port = tcp_port;
          std::vector<uint8_t> payload = serializeDiscovery(out);

          BaseHeader baseOut;
          buildHeader(MessageType::DISCOVERY_REPLY, 0, static_cast<uint32_t>(payload.size()), baseOut);
          std::vector<uint8_t> message = serializeHeader(baseOut);
          
          message.insert(message.end(), payload.begin(), payload.end());

          asio::ip::udp::endpoint endpoint(sender->address(),
                                           UDP_DISCOVERY_PORT);
          udp_socket.send_to(asio::buffer(message), endpoint);
          break;
        }
        case MessageType::DISCOVERY_REPLY: {
          // case someone is answering our broadcast
          DiscoveryPayload dp;
          if (!deserializeDiscovery(buffer_incoming + HEADER_SIZE,
                                    base.payload_len, dp)) {
            std::cerr << "failed to deserialize payload\n";
            listenForPackages();
            return;
          }
          // check for myself
          if (dp.device_name == device_name) {
            listenForPackages();
            return;
          }
          bool deviceInList = false;
          for (int i = 0; i < discoveredDevices.size(); i++) {
            if (discoveredDevices[i].name == dp.device_name) {
              discoveredDevices[i].last_seen = std::chrono::steady_clock::now();
              deviceInList = true;
              break;
            }
          }
          if (!deviceInList) {
            DiscoveredDevice dd;
            dd.name = dp.device_name;
            dd.tcp_port = dp.tcp_port;
            dd.ip = sender->address().to_string();
            dd.last_seen = std::chrono::steady_clock::now();
            discoveredDevices.push_back(dd);
            onDeviceFound(dd);
          }
          break;
        }
        default:
          // unexpected, log and keep on listening
          std::cerr << "unexpected payload\n";
          listenForPackages();
          return;
        }

        listenForPackages();
      });
}

void DiscoveryService::setTcpPort(uint16_t port) { this->tcp_port = port; }
