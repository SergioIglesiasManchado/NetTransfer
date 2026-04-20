## TODO list

specify the firewall rules needed for both windows and linux (integrated on windows, explain for linux in readme)
add resumable transfers (already defined, need to implement payload and sending)
give UI more specific reasons for failures 
drag and drop
settings pannel
Keepalive heartbeats

## Things to take into account

for compiling on windows:
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/Users/AriochGuerrero/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release


### Block 6 — GUI
> Goal: Drop a file, pick a device, watch a progress bar. That's it.

- [Done] **Framework:** Qt6 (LGPL) — native OS controls, proper drag & drop from file explorer, good on both platforms
- [ ] **Main window layout:**
  - Device list panel (left) — populated by UDP discovery
  - Drop zone (center) — large target area with drag & drop
  - Transfer queue (bottom) — active and completed transfers with progress bars
- [ ] **Drag & Drop:** connect `QDropEvent` → extract file path(s) → pass to network layer
- [ ] **Progress reporting:**
  - Network thread emits `progressUpdate(session_id, bytes_sent, total_bytes)` signal (thread-safe via Qt::QueuedConnection)
  - UI calculates `percent = bytes_sent / total_bytes * 100` and `speed = delta_bytes / delta_time`
- [Done] **Incoming transfer notification:** system tray notification + modal dialog (Accept / Reject)
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
