// uninstall_rules.cpp
#ifdef _WIN32
#include <cstdlib>
#include <iostream>

int main() {
    std::cout << "Removing NetTransfer firewall rules...\n";
    
    int r1 = system("netsh advfirewall firewall delete rule name=\"NetTransfer UDP\"");
    int r2 = system("netsh advfirewall firewall delete rule name=\"NetTransfer TCP\"");

    if (r1 == 0 && r2 == 0) {
        std::cout << "Rules removed successfully.\n";
    } else {
        std::cerr << "One or more rules could not be removed.\n";
        std::cerr << "Make sure you are running as Administrator.\n";
    }

    std::cout << "Press Enter to exit.\n";
    std::cin.get();
    return 0;
}

#else
int main() {
    std::cout << "Firewall rules are only managed on Windows.\n";
    std::cout << "On Linux, NetTransfer uses standard ports that don't require special rules.\n";
    return 0;
}
#endif