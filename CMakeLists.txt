cmake_minimum_required(VERSION 3.20)
project(NetTransfer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENSSL_USE_STATIC_LIBS TRUE)

# ── Platform-specific Definitions ──────────────────────────────────────────────
if(WIN32)
    # Target Windows 10 or later (0x0A00)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

# ── Sources ──────────────────────────────────────────────────────────────
add_executable(NetTransfer
    src/main.cpp
    src/core/protocol.cpp
    src/core/discovery.cpp
    src/core/transfer.cpp
    src/core/NetTransfer.cpp
    # add more source files here as you create them
)
add_executable(uninstall_rules src/uninstall_rules.cpp)

# ── Asio (header-only, standalone) ─────────────────────────────────────────────
target_include_directories(NetTransfer PRIVATE
  third_party/asio/include
  include
)
target_compile_definitions(NetTransfer PRIVATE ASIO_STANDALONE)

# ── Platform-specific linking ─────────────────────────────────────────────────
if(WIN32)
  target_link_libraries(NetTransfer PRIVATE ws2_32 mswsock)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(NetTransfer PRIVATE
      Threads::Threads
      OpenSSL::SSL
      OpenSSL::Crypto
      dl
    )
endif()

# ── Openssl linking ────────────────────────────────────────────────────────
# Define the path to your local OpenSSL
find_package(OpenSSL REQUIRED)
target_link_libraries(NetTransfer PRIVATE OpenSSL::SSL OpenSSL::Crypto)

