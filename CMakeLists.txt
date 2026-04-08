cmake_minimum_required(VERSION 3.20)
project(NetTransfer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(NetTransfer
    src/main.cpp
    src/core/protocol.cpp
    src/core/discovery.cpp
    # add more source files here as you create them
)

# ── Asio (header-only, standalone) ────────────────────────────────────────────
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
    target_link_libraries(NetTransfer PRIVATE Threads::Threads)
endif()
