# NetTransfer

> Local-network file transfer. No cloud. No accounts. No limits.

A cross-platform (Windows & Linux) peer-to-peer file transfer application using UDP for device discovery and TCP for reliable, encrypted file streaming.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  UDP (port 50000)  →  Discovery only ("who's out there?")   │
│  TCP (port 50001+) →  Everything else (handshake + data)    │
└─────────────────────────────────────────────────────────────┘

Flow:
  [Sender]  UDP Broadcast  →  "I'm here, TCP port 50001"
  [Receiver] TCP Connect   →  Handshake (offer / accept / reject)
  [Sender]  TCP Stream     →  File chunks (8–64 KB each)
  [Sender]  TCP Close      →  "Done. SHA-256 = abc123..."
  [Receiver] Verify        →  Checksum match → save to Downloads/
```

---
## IMPORTANT

Windows Defender Firewall → Inbound Rules → New Rule
→ Port → UDP → 50000 → Allow → All profiles → Name: "NetTransfer Discovery"

## TODO list

specify the firewall rules needed for both windows and linux
check the firewall config before executing actual service
add resumable transfers (already defined, need to implement payload and sending)
add the tls actual handshake verification in block 5

for compiling on windows:
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/Users/AriochGuerrero/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build

check in linux if it compiles or not

## Roadmap & Task List

### Block 0 — Tooling & Project Structure

> Goal: A clean repo that compiles on day one, on both platforms.

- [✅] Initialize CMake project (`CMakeLists.txt` at root)
- [✅] Set up directory structure:
  ```
  landrop/
  ├── CMakeLists.txt
  ├── README.md
  ├── src/
  │   ├── main.cpp
  │   ├── network/
  │   ├── protocol/
  │   ├── ui/
  │   └── utils/
  ├── include/
  │   └── asio/          ← drop standalone Asio here
  ├── tests/
  └── third_party/
      ├── spdlog/
      └── openssl/       ← or link system OpenSSL
  ```

```


- [✅] Add `clang-format` config (`.clang-format` at root)
- [ ] Add `spdlog` for logging (header-only, via FetchContent or submodule)
- [ ] Add `GoogleTest` for unit testing (via CMake FetchContent)
- [ ] Set up CI build matrix: Windows + MSVC, Windows + MinGW, Linux + GCC, Linux + Clang

---

### Block 1 — Cross-Platform Foundation (C++ & CMake)
> Goal: Code compiles identically on Windows and Linux with no `#ifdef` soup in logic files.

- [✅] Download Standalone Asio (`asio-1.x.x`) — drop `include/` folder into project, no install needed
- [✅] Configure `CMakeLists.txt`:
  - [✅] Add `-DASIO_STANDALONE` compile definition (no Boost)
  - [✅] `if(WIN32)` → link `ws2_32`, `mswsock`
  - [✅] `if(UNIX)` → link `pthread`
  - [✅] `if(WIN32)` → link OpenSSL pre-built binaries; `if(UNIX)` → find system OpenSSL
- [ ] Verify `asio::io_context` spins up and shuts down cleanly on both platforms
- [ ] Write a trivial TCP echo server/client as a compile smoke test → delete after passing

**Key types to use (never raw descriptors):**
- `asio::ip::tcp::socket`
- `asio::ip::tcp::acceptor`
- `asio::ip::udp::socket`
- `asio::ssl::stream<tcp::socket>` ← wrap TCP sockets in TLS from day one

---

### Block 2 — Protocol Definition
> Goal: A versioned, unambiguous binary protocol both sides agree on before a single byte of file data moves.

#### 2a — Message Types
```

DISCOVERY_BROADCAST (UDP) — "I'm here, name=X, tcp_port=Y"
DISCOVERY_REPLY (UDP) — "Acknowledged, I'm Z"
TRANSFER_OFFER (TCP) — "I want to send you file F, size S"
TRANSFER_ACCEPT (TCP) — "Go ahead"
TRANSFER_REJECT (TCP) — "No thanks" + reason code
DATA_CHUNK (TCP) — Raw file bytes (no header per chunk)
TRANSFER_DONE (TCP) — "Finished. SHA-256 = <32 bytes>"
TRANSFER_ACK (TCP) — "Checksum OK / FAIL"
ERROR (TCP) — Error code + UTF-8 message
KEEPALIVE (TCP) — Heartbeat during long transfers

```

#### 2b — Base Header (all TCP messages)
Every TCP message starts with this fixed-size prefix:

| Field | Type | Size | Notes |
|---|---|---|---|
| `magic` | `uint32_t` | 4 B | `0x4C445250` ("LDRP") — reject anything else |
| `version` | `uint8_t` | 1 B | Protocol version, currently `1` |
| `msg_type` | `uint8_t` | 1 B | See message types above |
| `session_id` | `uint64_t` | 8 B | Random ID per transfer — supports parallel transfers |
| `payload_len` | `uint32_t` | 4 B | Byte length of everything after this header |
| `header_crc` | `uint32_t` | 4 B | CRC32 of the above 18 bytes — detect corrupted headers early |

**Total base header: 22 bytes**

#### 2c — TRANSFER_OFFER Payload
| Field | Type | Notes |
|---|---|---|
| `file_size` | `uint64_t` | Total bytes |
| `resume_offset` | `uint64_t` | Byte offset to resume from (0 = fresh start) |
| `sha256` | `uint8_t[32]` | Pre-computed hash of the full file |
| `name_len` | `uint16_t` | Byte length of the filename string below |
| `file_name` | `uint8_t[name_len]` | UTF-8 filename — **no path, stripped on sender side** |

#### 2d — Serialization Rules
- [✅] **Never** `memcpy` a struct directly onto the wire — compiler padding breaks cross-platform compatibility
- [✅] Write explicit `serialize()` / `deserialize()` functions for every message type, field by field
- [✅] All multi-byte integers go over the wire in **network byte order** (big-endian) — use `htonl` / `ntohl` / `htonll` / `ntohll`
- [✅] Validate `magic` and `version` before parsing any payload — drop and log unknown messages

---

### Block 3 — State Machine
> Goal: Model every possible transfer state explicitly before writing network logic. This prevents impossible states and makes error handling obvious.

```

IDLE
│ (user drops file / selects device)
▼
DISCOVERING ← UDP broadcast sent, waiting for reply
│ (target found)
▼
OFFERING ← TCP TRANSFER_OFFER sent, waiting for response
│ (ACCEPT received) │ (REJECT received)
▼ ▼
TRANSFERRING REJECTED (→ IDLE)
│ (all chunks sent)
▼
VERIFYING ← waiting for TRANSFER_ACK
│ (ACK = OK) │ (ACK = FAIL or timeout)
▼ ▼
DONE (→ IDLE) ERROR (→ IDLE, notify user)

````

- [✅] Implement `TransferState` enum class with all states above
- [✅] Implement `TransferSession` class that owns one state machine per active transfer
- [✅] Any network event that arrives in an unexpected state → log + send `ERROR` message + transition to `IDLE`
- [✅] Make `TransferSession` thread-safe: network thread and UI thread both touch it

---

### Block 4 — Network Core
> Goal: Reliable, async file transfer that doesn't block the UI or eat all available RAM.

#### 4a — UDP Discovery
- [✅] Bind a UDP socket to `0.0.0.0:50000`
- [✅] On startup, broadcast `DISCOVERY_BROADCAST` to `255.255.255.255:50000`
- [✅] Listen for `DISCOVERY_BROADCAST` from others → reply with `DISCOVERY_REPLY` including device name and TCP port
- [✅] Re-broadcast every 5 seconds (heartbeat) so late-joining devices appear
- [✅] Track discovered devices in a list with a last-seen timestamp — remove devices not heard from in 15 seconds
- [ ] **Firewall note (Windows):** prompt user for firewall permission on first launch; document the required inbound rule

#### 4b — TCP Transfer (Sender side)
- [✅] Open `asio::ssl::stream<tcp::socket>` to the target's IP and TCP port
- [ ] Complete TLS handshake (verify peer certificate / key fingerprint)
- [✅] Send `TRANSFER_OFFER` — wait for `TRANSFER_ACCEPT` or `TRANSFER_REJECT`
- [✅] Open file in binary read mode
- [ ] If `resume_offset > 0` in the ACCEPT, seek to that offset before reading
- [✅] Loop: read chunk from disk → `async_write` to socket
  - Chunk size: start at 32 KB; tune later based on measured throughput
  - Do **not** buffer more than 2 chunks ahead — prevent unbounded RAM growth if disk > network speed
- [✅] After last chunk, send `TRANSFER_DONE` with SHA-256
- [✅] Wait for `TRANSFER_ACK` → report success or failure to UI

#### 4c — TCP Transfer (Receiver side)
- [✅] `asio::ip::tcp::acceptor` listens on a port in range `50001–50100` (first available)
- [✅] On new connection, complete TLS handshake
- [✅] Read and validate base header → parse `TRANSFER_OFFER`
- [✅] Prompt user (or auto-accept based on settings) → send `TRANSFER_ACCEPT` or `TRANSFER_REJECT`
- [✅] Detect system Downloads folder:
  - Windows: `FOLDERID_Downloads` via `SHGetKnownFolderPath`
  - Linux: `$XDG_DOWNLOAD_DIR` or `$HOME/Downloads` fallback
- [✅] Open destination file for writing (handle name collisions: append ` (1)`, ` (2)`, etc.)
- [✅] Loop: `async_read` chunk → write to disk
- [✅] On `TRANSFER_DONE`: compute SHA-256 of received file → compare → send `TRANSFER_ACK`

#### 4d — Threading Model
- [✅] One dedicated thread runs `io_context.run()` — never block it
- [ ] Use `asio::strand` to serialize callbacks that touch shared state (device list, session map)
- [ ] UI thread communicates with network thread via a thread-safe queue (or `asio::post()` onto the io_context)
- [ ] Implement a 30-second `KEEPALIVE` timer during transfers — abort if no response in 10 seconds

---

### Block 5 — Security
> Goal: Nobody on your Wi-Fi can snoop transfers, spoof devices, or send you malware disguised as a cat photo.

- [ ] **TLS channel (mandatory, not optional):**
  - Use `asio::ssl::context` with `TLSv1.2_or_above`
  - Generate a self-signed certificate + Ed25519 key pair on first launch
  - Store private key in OS keychain (Windows Credential Manager / Linux Secret Service / fallback to `~/.config/landrop/`)
  - On connection, verify peer certificate fingerprint matches the known device's registered fingerprint
- [ ] **Device pairing:**
  - First connection to a new device shows its certificate fingerprint to both users ("Do these match? YES / NO")
  - Store approved fingerprints in a local `trusted_devices.json`
  - Reject connections from unknown fingerprints unless the user explicitly allows them
- [ ] **File validation:**
  - Strip the full path from the filename on the sender side before sending
  - Reject filenames containing `..`, `/`, `\`, or null bytes
  - Reject attempts to write outside the Downloads directory (resolve the full path and assert it starts with the Downloads prefix)
- [ ] **SHA-256 integrity check:** computed on sender before transfer begins, verified on receiver after the last byte lands on disk (see Block 4 above)
- [ ] **No auto-execution:** receiver saves to Downloads only — never opens or runs the file

---

### Block 6 — GUI
> Goal: Drop a file, pick a device, watch a progress bar. That's it.

- [ ] **Framework:** Qt6 (LGPL) — native OS controls, proper drag & drop from file explorer, good on both platforms
- [ ] **Main window layout:**
  - Device list panel (left) — populated by UDP discovery
  - Drop zone (center) — large target area with drag & drop
  - Transfer queue (bottom) — active and completed transfers with progress bars
- [ ] **Drag & Drop:** connect `QDropEvent` → extract file path(s) → pass to network layer
- [ ] **Progress reporting:**
  - Network thread emits `progressUpdate(session_id, bytes_sent, total_bytes)` signal (thread-safe via Qt::QueuedConnection)
  - UI calculates `percent = bytes_sent / total_bytes * 100` and `speed = delta_bytes / delta_time`
- [ ] **Incoming transfer notification:** system tray notification + modal dialog (Accept / Reject)
- [ ] **Settings panel:** device name, auto-accept toggle, trusted devices list, download folder override
- [ ] **System tray:** minimize to tray, keep receiving in background

---

### Block 7 — Testing
> Goal: Find bugs before users do.

- [ ] **Unit tests (GoogleTest):**
  - Serialization round-trip for every message type (serialize → deserialize → assert equal)
  - `magic` / `version` rejection for malformed headers
  - Filename sanitization (path stripping, null byte rejection, collision renaming)
  - SHA-256 computation against known vectors
  - State machine: assert illegal transitions are rejected
- [ ] **Integration tests:**
  - Spin up a sender and receiver in the same process on loopback (`127.0.0.1`)
  - Transfer files of various sizes: 0 bytes, 1 byte, 1 KB, 10 MB, 1 GB
  - Simulate mid-transfer disconnect → assert receiver cleans up partial file
  - Simulate corrupted SHA-256 → assert receiver rejects the file
- [ ] **Stress tests:**
  - Transfer a large file (10 GB+) and assert RAM usage stays flat (no buffer accumulation)
  - Multiple simultaneous transfers using different `session_id` values

---

## Critical Implementation Notes

| Topic | Rule |
|---|---|
| **Endianness** | Always convert to network byte order (big-endian) before writing to socket. Never `memcpy` structs. |
| **Windows Firewall** | Enemy #1. Request inbound rule for UDP 50000 and TCP 50001–50100 on first launch. Document manual steps as fallback. |
| **Timeouts** | A disconnected socket doesn't always know it's disconnected. Implement KEEPALIVE heartbeats every 30 s; abort after 10 s silence. |
| **RAM usage** | Never buffer more than 2 chunks in flight. If disk write is slower than network read, apply backpressure — pause `async_read` until the write queue drains. |
| **Struct padding** | `sizeof(YourHeader)` lies. Use explicit field-by-field serialization functions everywhere. |
| **Filename safety** | Strip path on sender. Validate on receiver. Never trust the remote side. |
| **TLS from day one** | Do not implement plain TCP first and "add TLS later". The socket type changes. Design with `asio::ssl::stream` from the first line. |
| **Resumable transfers** | Include `resume_offset` in the OFFER/ACCEPT handshake from the beginning. Retrofitting it later touches the protocol, the state machine, and the UI simultaneously. |

---

## Port Reference

| Port | Protocol | Purpose |
|---|---|---|
| 50000 | UDP | Device discovery (broadcast) |
| 50001–50100 | TCP | File transfer (first available in range) |

---

## Dependencies

| Library | Version | Purpose | License |
|---|---|---|---|
| [Asio](https://think-async.com/Asio/) | 1.28+ | Async networking | BSL-1.0 |
| [OpenSSL](https://openssl.org) | 3.x | TLS + SHA-256 + Ed25519 | Apache-2.0 |
| [spdlog](https://github.com/gabime/spdlog) | 1.13+ | Logging | MIT |
| [Qt](https://www.qt.io) | 6.x | GUI | LGPL-3.0 |
| [GoogleTest](https://github.com/google/googletest) | 1.14+ | Unit & integration testing | BSD-3 |

---

## Build Instructions

```bash
# Clone and enter
git clone https://github.com/yourname/landrop.git
cd landrop

# Configure (Release build)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Run tests
cd build && ctest --output-on-failure
````

**Windows:** Ensure OpenSSL pre-built binaries are in `third_party/openssl/` or install via `vcpkg install openssl`.  
**Linux:** `sudo apt install libssl-dev` or equivalent.

---

## Current Status

| Block                                 | Status         |
| ------------------------------------- | -------------- |
| Block 0 — Tooling & Project Structure | ⬜ Not started |
| Block 1 — Cross-Platform Foundation   | ⬜ Not started |
| Block 2 — Protocol Definition         | ⬜ Not started |
| Block 3 — State Machine               | ⬜ Not started |
| Block 4 — Network Core                | ⬜ Not started |
| Block 5 — Security                    | ⬜ Not started |
| Block 6 — GUI                         | ⬜ Not started |
| Block 7 — Testing                     | ⬜ Not started |
