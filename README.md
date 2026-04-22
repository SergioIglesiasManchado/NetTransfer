# NetTransfer

A cross-platform (Windows & Linux) peer-to-peer LAN file transfer app. No cloud, no accounts, no middleman — just direct encrypted transfers between devices on the same network.

## Important
- In Windows, the app bust be run as administrator at least the first time to configure firewall rules
- In linux, the firewall must be configured manually depending on the installed software (if you have a firewall instaled):
    Allow UDP on port 50000
    Allow TCP on ports 50001-50100
  Check for your firewall (ufw, firewalld, iptables)

## Features
- Automatic device discovery via UDP broadcast
- Encrypted transfers over TLS (self-signed certificates)
- Device trust system with certificate fingerprint verification
- SHA-256 integrity verification
- Native Qt6 GUI

## Status
Active development — beta. Core transfer functionality working on Windows and Linux.

## Dependencies
- [Asio](https://think-async.com/Asio/) 1.28+ — async networking (standalone, no Boost)
- [OpenSSL](https://openssl.org) 3.x — TLS + SHA-256
- [Qt](https://www.qt.io) 6.x — GUI (LGPL)
- [nlohmann/json](https://github.com/nlohmann/json) — config and trust

## Building
toolchain_file path might change depending on how you install the requiered dependencies
**Windows (using vcpkg package manager):**
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

**Linux:**
```bash
sudo apt install build-essential cmake libssl-dev qt6-base-dev  # Debian/Ubuntu
sudo pacman -S base-devel cmake openssl qt6-base                # Arch
cmake -B build -S .
cmake --build build --config Release
```

## Port Reference
| Port | Protocol | Purpose |
|------|----------|---------|
| 50000 | UDP | Device discovery |
| 50001–50100 | TCP | File transfer |
