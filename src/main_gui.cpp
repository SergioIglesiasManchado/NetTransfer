#include <QApplication>
#include <QMessageBox>
#include "mainWindow.h"

#ifdef _WIN32
  #include <cstdlib>
  #include <openssl/applink.c>
  #include <shellapi.h>

  bool runSilent(const wchar_t* cmd) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = L"netsh";
    sei.lpParameters = cmd;
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    ShellExecuteExW(&sei);
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return exitCode == 0;
  }

  bool firewallRuleExists(const wchar_t* name) {
    std::wstring params = std::wstring(L"advfirewall firewall show rule name=\"") + name + L"\"";
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpFile = L"netsh";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    ShellExecuteExW(&sei);
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return exitCode == 0;
  }

  bool addFirewallRules() {
    bool udpExists = firewallRuleExists(L"NetTransfer UDP");
    bool tcpExists = firewallRuleExists(L"NetTransfer TCP");
    
    if (udpExists && tcpExists)
        return true; // nothing to do, no elevation needed
    
    // check if already elevated
    BOOL isAdmin = FALSE;
    PSID adminGroup;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &adminGroup);
    CheckTokenMembership(nullptr, adminGroup, &isAdmin);
    FreeSid(adminGroup);

    if (!isAdmin) {
        // relaunch as admin once
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = path;
        sei.nShow = SW_NORMAL;
        ShellExecuteExW(&sei);
        return false; // close this instance
    }

    // we are admin, add the rules
    if (!udpExists)
        runSilent(L"advfirewall firewall add rule name=\"NetTransfer UDP\" protocol=UDP dir=in localport=50000 action=allow");
    if (!tcpExists)
        runSilent(L"advfirewall firewall add rule name=\"NetTransfer TCP\" protocol=TCP dir=in localport=50001-50100 action=allow");
    
    return true;
}

#endif

int main(int argc, char *argv[]) {

  QApplication app(argc, argv);

  #ifdef _WIN32
    if (!addFirewallRules()) {
      return 0;
    }
  #endif

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