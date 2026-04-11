#include "NetTransfer.h"
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <openssl/opensslv.h>

#ifdef _WIN32
#include <cstdlib>

bool firewallRuleExists(const std::string &name) {
  std::string cmd =
      "netsh advfirewall firewall show rule name=\"" + name + "\" >nul 2>&1";
  return system(cmd.c_str()) == 0; // returns 0 if rule exists
}

void addFirewallRules() {
  if (!firewallRuleExists("NetTransfer UDP")) {
    system("netsh advfirewall firewall add rule name=\"NetTransfer UDP\" "
           "protocol=UDP dir=in localport=50000 action=allow >nul 2>&1");
    std::cout << "Added firewall rule: NetTransfer UDP\n";
  }
  if (!firewallRuleExists("NetTransfer TCP")) {
    system("netsh advfirewall firewall add rule name=\"NetTransfer TCP\" "
           "protocol=TCP dir=in localport=50001-50100 action=allow >nul 2>&1");
    std::cout << "Added firewall rule: NetTransfer TCP\n";
  }
}
#endif

int main(int argc, char *argv[]) {

#ifdef _WIN32
  addFirewallRules();
#endif

  std::cout << "OpenSSL version: " << OpenSSL_version(OPENSSL_VERSION) << "\n";

  std::string device_name;
  uint16_t tcp_port;

  std::cout << "introduce device name: ";
  std::cin >> device_name;

  std::cout << "introduce tcp port (50001-50100): ";
  std::cin >> tcp_port;

  NetTransfer net(device_name, tcp_port);

  // callbacks
  net.setOnDeviceFound([](DiscoveredDevice d) {
    std::cout << "device found: " << d.name << " (" << d.ip << ")\n";
  });

  std::atomic<bool> pending_offer{false};
  std::string offer_file_name;
  uint64_t offer_file_size;
  net.setOnOffer([&net, &pending_offer, &offer_file_name,
                  &offer_file_size](OfferPayload offer) {
    offer_file_name = offer.file_name;
    offer_file_size = offer.file_size;
    pending_offer = true;
  });
  net.setOnProgress([](uint64_t sent, uint64_t total) {
    static auto last_print = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print)
            .count() < 100) {
      return;
    }
    last_print = now;
    double percent = (double)sent / (double)total * 100.0;
    std::cout << "\rprogress: " << sent << "/" << total << " (" << std::fixed
              << std::setprecision(1) << percent << "%)";
    std::cout.flush();
  });
  net.setOnComplete([&pending_offer](bool ok) {
    if (ok) {
      std::cout << "\rprogress: 100.0%                      \n";
    }
    std::cout << "\n" << (ok ? "transfer complete\n" : "transfer failed\n");
  });

  bool fine = net.start();
  if (!fine) {
    std::cout << "error at start, exiting";
    return -1;
  }

  while (true) {

    if (pending_offer) {
      std::cout << "\nincoming file: " << offer_file_name << " ("
                << offer_file_size << " bytes)\n";
      std::cout << "accept? (y/n)";
      std::string ans;
      std::getline(std::cin, ans);
      pending_offer = false;
      if (ans == "y")
        net.accept();
      else
        net.reject(RejectReason::USER_DECLINED);
      continue;
    }

    std::cout << "select option: (l) list, (s) send file, (q) quit: \n";
    std::string line;
    std::getline(std::cin, line);
    if (line.empty())
      continue;

    char input = line[0];
    if (input == 'l') {
      auto devices = net.getDevices();
      for (auto &d : devices) {
        std::cout << d.name << " - " << d.ip << ":" << d.tcp_port << "\n";
      }

    } else if (input == 's') {
      auto devices = net.getDevices();
      if (devices.empty()) {
        std::cout << "no devices found\n";
        continue;
      }
      for (int i = 0; i < devices.size(); i++) {
        std::cout << i << ": " << devices[i].name << " - " << devices[i].ip
                  << ":" << devices[i].tcp_port << "\n";
      }
      std::cout << "select device: ";
      int choice;
      std::cin >> choice;
      std::cin.ignore(); // clear newline from buffer
      if (choice < 0 || choice >= devices.size()) {
        std::cout << "invalid choice\n";
        continue;
      }
      std::cout << "file path: ";
      std::string file_path;
      std::getline(std::cin, file_path);
      net.sendFile(devices[choice], file_path);

    } else if (input == 'q') {
      std::cout << "bye, see ya\n";
      net.stop();
      break;
    }
  }

  return 0;
}
