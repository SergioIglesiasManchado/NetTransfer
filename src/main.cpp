#include "NetTransfer.h"
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <openssl/opensslv.h>

#ifdef _WIN32
#include <cstdlib>
#include <openssl/applink.c>

bool firewallRuleExists(const std::string &name) {
  std::string cmd =
      "netsh advfirewall firewall show rule name=\"" + name + "\" >nul 2>&1";
  return system(cmd.c_str()) == 0; // returns 0 if rule exists
}

void addFirewallRules() {
  if (!firewallRuleExists("NetTransfer UDP")) {
    int result = system("netsh advfirewall firewall add rule name=\"NetTransfer UDP\" "
           "protocol=UDP dir=in localport=50000 action=allow >nul 2>&1");
    if (result != 0) {
      std::cout << "WARNING: Could not add firewall rules.\n";
      std::cout << "Please run NetTransfer as Administrator for full functionality.\n";
      std::cout << "Without firewall rules, other devices may not be able to find this PC.\n";
      return;
    } else {
      std::cout << "Added firewall rule: NetTransfer UDP\n";
    }
  }
  if (!firewallRuleExists("NetTransfer TCP")) {
    int result = system("netsh advfirewall firewall add rule name=\"NetTransfer TCP\" "
           "protocol=TCP dir=in localport=50001-50100 action=allow >nul 2>&1");
    if (result != 0) {
      std::cout << "WARNING: Could not add firewall rules.\n";
      std::cout << "Please run NetTransfer as Administrator for full functionality.\n";
      std::cout << "Without firewall rules, other devices may not be able to find this PC.\n";
      return;
    } else {
      std::cout << "Added firewall rule: NetTransfer TCP\n";
    }
  }
}
#endif

int main(int argc, char *argv[]) {

#ifdef _WIN32
  addFirewallRules();
#endif

  NetTransfer net;

  // callbacks
  net.setOnDeviceFound([](DiscoveredDevice d) {
    std::cout << "device found: " << d.name << " (" << d.ip << ")\n";
  });

  std::atomic<bool> pending_offer_flag{false};
  OfferPayload pending_offer_data;

  net.setOnOffer([&](OfferPayload offer) {
      pending_offer_data = offer;
      pending_offer_flag = true;
      std::cout << "\nincoming file: " << offer.file_name 
                << " (" << offer.file_size << " bytes)\n";
      std::cout << "type 'y' or 'n' to accept/reject\n";
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

  net.setOnComplete([&pending_offer_flag](bool ok) {
    if (ok) {
      std::cout << "\rprogress: 100.0%                      \n";
    }
    std::cout << "\n" << (ok ? "transfer complete\n" : "transfer failed\n");
  });

  net.setOnFirstRun([]() {
    std::string name;
    std::cout << "First run, Enter device name: ";
    std::getline(std::cin, name);
    return name;
  });

 net.setOnNewDevice([](std::string fingerprint, std::string name) {
    std::cout << "\nNew device tried to connect, but was rejected because it is untrusted:\n";
    std::cout << "  Name:        " << name << "\n";
    std::cout << "  Fingerprint: " << fingerprint << "\n";
    std::cout << "Type 't' to trust them (they will need to send the file again), or 'i' to ignore.\n";
    return true;
  });

  bool fine = net.start();
  if (!fine) {
    std::cout << "error at start, exiting";
    return -1;
  }

  while (true) {

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
      std::string choice_str;
      std::getline(std::cin, choice_str);
      int choice = std::stoi(choice_str);
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
    } else if (input == 'y' && pending_offer_flag) {
    pending_offer_flag = false;
    net.accept();
    } else if (input == 'n' && pending_offer_flag) {
        pending_offer_flag = false;
        net.reject(RejectReason::USER_DECLINED);
    } else if (input == 't' && net.hasPendingTrust()) {
        net.trustDevice();
        std::cout << "Device trusted. They can now send you files.\n";
    } else if (input == 'i' && net.hasPendingTrust()) {
        net.ignorePendingTrust();
        std::cout << "Device ignored.\n";
    }
  }

  return 0;
}
