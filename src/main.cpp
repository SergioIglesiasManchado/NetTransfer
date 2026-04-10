#include "NetTransfer.h"
#include <iostream>

int main(int argc, char *argv[]) { 
    
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
    net.setOnOffer([&net](OfferPayload offer) {
        std::cout << "incoming file: " << offer.file_name 
                << " (" << offer.file_size << " bytes)\n";
        std::cout << "accept? (y/n): ";
        std::string ans;
        std::getline(std::cin, ans);
        if (ans == "y") net.accept();
        else net.reject(RejectReason::USER_DECLINED);
    });
    net.setOnProgress([](uint64_t sent, uint64_t total) {
        std::cout << "progress: " << sent << "/" << total << "\n";
    });
    net.setOnComplete([](bool ok) {
        std::cout << (ok ? "transfer complete\n" : "transfer failed\n");
    });

    bool fine = net.start();
    if (!fine) {
        std::cout << "error at start, exiting";
        return -1;
    }

    while (true) {

        // input loop
        std::cout << "select option: (l) list, (s) send file, (q) quit: \n";
        char input;
        std::cin >> input;
        if (input == 'l') {
            auto devices = net.getDevices();
            for (auto& d : devices) {
                std::cout << d.name << " - " << d.ip << ":" << d.tcp_port << "\n";
            }

        } else if (input == 's') {
            auto devices = net.getDevices();
            if (devices.empty()) {
                std::cout << "no devices found\n";
                continue;
            }
            for (int i = 0; i < devices.size(); i++) {
                std::cout << i << ": " << devices[i].name << " - " 
                        << devices[i].ip << ":" << devices[i].tcp_port << "\n";
            }
            std::cout << "select device: ";
            int choice;
            std::cin >> choice;
            std::cin.ignore();  // clear newline from buffer
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
