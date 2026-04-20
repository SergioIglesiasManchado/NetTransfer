#include <QApplication>
#include "mainWindow.h"

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
      QMessageBox::warning(nullptr, "NetTransfer",
        "Could not add firewall rules.\n"
        "Please run as Administrator for full functionality.");
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

    QApplication app(argc, argv);

    MainWindow window;
    window.setWindowTitle("NetTransfer");
    window.resize(800, 500);
    window.show();

    #ifdef _WIN32
      app.setWindowIcon(QIcon(":/resources/icon.ico"));
    #else
      app.setWindowIcon(QIcon(":/resources/icon.png"));
    #endif

    return app.exec();
}