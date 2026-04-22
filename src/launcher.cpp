#include <windows.h>
#include <string>
#include <shellapi.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    
    // replace launcher.exe with app\NetTransfer.exe
    std::wstring exePath(path);
    size_t pos = exePath.rfind(L'\\');
    exePath = exePath.substr(0, pos) + L"\\app\\NetTransfer.exe";
    
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpFile = exePath.c_str();
    sei.nShow = SW_NORMAL;
    ShellExecuteExW(&sei);
    return 0;
}