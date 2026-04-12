#include <cstdint>
#include <ios>
#ifdef _WIN32
#include <shlobj.h>
#endif

#include "protocol.h"
#include "transfer.h"
#include <random>

std::filesystem::path getDownloadsFolder();
std::filesystem::path resolveFilePath(const std::filesystem::path &dir,
                                      const std::string &filename);

TransferSender::TransferSender(asio::io_context &io,
                               asio::ssl::context &ssl_ctx,
                               std::string target_ip, uint16_t target_port,
                               std::string file_path, std::string device_name)
    : io_context(io), socket(io, ssl_ctx), target_ip(target_ip),
      target_port(target_port), file_path(file_path), device_name(device_name) {

  bytes_sent = 0;

  // generate random number for session id
  std::random_device rd;
  std::mt19937_64 eng(rd());
  std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);
  session_id = dist(eng);
}

bool TransferSender::start() {

  // open the file
  file.open(file_path, std::fstream::binary | std::fstream::in);
  if (file.is_open()) {
    file.seekg(0, std::ios::end);
    file_size = file.tellg();
    file.seekg(0, std::ios::beg);
  } else {
    std::cout << "file not opened\n";
    return false;
  }

  // compute the sha256
  unsigned char sha256[32];
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
  uint8_t sha_buffer[MAX_BUFFER_SIZE];
  while (file) {
    file.read(reinterpret_cast<char *>(sha_buffer), MAX_BUFFER_SIZE);
    std::streamsize bytes_read = file.gcount();
    if (bytes_read > 0) {
      EVP_DigestUpdate(ctx, sha_buffer, bytes_read);
    }
  }
  unsigned int length = 0;
  EVP_DigestFinal_ex(ctx, sha256, &length);
  EVP_MD_CTX_free(ctx);
  memcpy(this->sha256, sha256, 32); // sent sha to rest of the class
  file.clear();                     // delete weird EOF flags
  file.seekg(0, std::ios::beg);

  // connect to target device
  asio::ip::tcp::endpoint endpoint(asio::ip::make_address(target_ip),
                                   target_port);

  // TODO remove couts, just for DEBUG
  try {
    std::cout << "connecting to " << target_ip << ":" << target_port << "\n";
    socket.lowest_layer().connect(endpoint);
    std::cout << "connected, starting handshake\n";

    socket.handshake(asio::ssl::stream_base::client);
    if (onValidateCert && !onValidateCert(socket.native_handle(), device_name)) {
        std::cerr << "peer cert validation failed\n";
        return false;
    }
    std::cout << "handshake done, sending offer\n";
  } catch (std::exception &e) {
    std::cerr << "connection failed: " << e.what() << "\n";
    unsigned long err;
    while ((err = ERR_get_error()) != 0) {
      char buf[256];
      ERR_error_string_n(err, buf, sizeof(buf));
      std::cerr << "OpenSSL detail: " << buf << "\n";
    }
    return false;
  }

  // send a transfer offer
  BaseHeader header;
  header.magic = NT_MAGIC;
  header.version = NT_VERSION;
  header.msg_type = MessageType::TRANSFER_OFFER;
  header.session_id = session_id;
  header.header_crc = 0;

  OfferPayload payload;
  payload.file_size = file_size;
  payload.resume_offset = 0;
  memcpy(payload.sha256, sha256, 32);
  payload.file_name = std::filesystem::path(file_path).filename().string();
  payload.device_name = device_name;

  std::vector<uint8_t> serializedPayload = serializeOffer(payload);
  header.payload_len = static_cast<uint32_t>(serializedPayload.size());
  std::vector<uint8_t> message = serializeHeader(header);
  message.insert(message.end(), serializedPayload.begin(),
                 serializedPayload.end());

  asio::write(socket, asio::buffer(message));

  // DEBUG TODO delete this
  std::cout << "offer sent, waiting for response\n";
  // wait for response (async wait)
  asio::async_read(
      socket, asio::buffer(buffer, HEADER_SIZE),
      [this](std::error_code ec, size_t bytes) {
        // DEBUG TODO delete this
        std::cout << "got response\n";
        if (ec) {
          std::cerr << ec.message() << "\n";
          onComplete(false);
          return;
        }
        // only header was read, check header
        BaseHeader header;
        deserializeHeader(buffer, header);
        if (header.msg_type == MessageType::TRANSFER_ACCEPT) {
          if (header.payload_len == 0) {
            sendNextChunk();
          } else {
            // clear buffer in case of noise
            asio::async_read(
                socket, asio::buffer(buffer, header.payload_len),
                [this](std::error_code ec, size_t bytes) { sendNextChunk(); });
          }
        } else if (header.msg_type == MessageType::TRANSFER_REJECT) {
          asio::async_read(socket, asio::buffer(buffer, header.payload_len),
          [this, header](std::error_code ec, size_t bytes) {
            if (ec) {
              std::cerr << ec.message() << "\n";
              onComplete(false);
              return;
            }
            RejectPayload rp;
            deserializeReject(buffer, header.payload_len,
                              rp); // we have reason for reject
            std::cerr << "transfer rejected: "
                      << static_cast<int>(rp.reason)
                      << "\n"; // for now logged
            onComplete(false);
            return;
          });
        } else {
          std::cerr << "unexpected header type\n";
          return;
        }
      });
  return true;
}

bool TransferSender::stop() {

  try {
    if (socket.lowest_layer().is_open()) {
      socket.lowest_layer().close();
    }
    if (file.is_open()) {
      file.close();
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << "\n";
    return false;
  }
  return true;
}

void TransferSender::setOnProgress(
    std::function<void(uint64_t, uint64_t)> callback) {
  onProgress = callback;
}

void TransferSender::setOnComplete(std::function<void(bool)> callback) {
  onComplete = callback;
}

void TransferSender::setOnValidateCert(std::function<bool(SSL*, std::string)> callback) {
    onValidateCert = callback;
}

void TransferSender::sendNextChunk() {

  file.read(reinterpret_cast<char *>(buffer), MAX_BUFFER_SIZE - HEADER_SIZE);
  std::streamsize bytes_read = file.gcount();
  if (bytes_read > 0) {
    // keep on transfering
    BaseHeader header;
    header.magic = NT_MAGIC;
    header.version = NT_VERSION;
    header.msg_type = MessageType::DATA_CHUNK;
    header.session_id = session_id;
    header.payload_len = static_cast<uint32_t>(bytes_read);
    header.header_crc = 0;

    auto message =
        std::make_shared<std::vector<uint8_t>>(serializeHeader(header));
    message->insert(message->end(), buffer, buffer + bytes_read);

    asio::async_write(socket, asio::buffer(*message),
    [this, message](std::error_code ec, size_t bytes) {
      if (ec) {
        std::cerr << ec.message() << "\n";
        onComplete(false);
        return;
      }
      bytes_sent += bytes - HEADER_SIZE;
      onProgress(bytes_sent, file_size);
      sendNextChunk();
    });
  } else {
    // finished transfer, sending done
    DonePayload pd;
    memcpy(pd.sha256, sha256, 32);
    std::vector<uint8_t> payload = serializeDone(pd);

    BaseHeader header;
    header.magic = NT_MAGIC;
    header.version = NT_VERSION;
    header.msg_type = MessageType::TRANSFER_DONE;
    header.session_id = session_id;
    header.payload_len = static_cast<uint32_t>(payload.size());
    header.header_crc = 0;
    std::vector<uint8_t> message = serializeHeader(header);
    message.insert(message.end(), payload.begin(), payload.end());
    asio::write(socket, asio::buffer(message));

    // waiting for ack
    asio::async_read(socket, asio::buffer(buffer, HEADER_SIZE),
    [this](std::error_code ec, size_t bytes) {
      if (ec) {
        std::cerr << ec.message() << "\n";
        onComplete(false);
        return;
      }
      BaseHeader header;
      deserializeHeader(buffer, header);
      if (header.msg_type == MessageType::TRANSFER_ACK) {
        asio::async_read(
        socket, asio::buffer(buffer, header.payload_len),
        [this, header](std::error_code ec, size_t bytes) {
          if (ec) {
            std::cerr << ec.message() << "\n";
            onComplete(false);
            return;
          }
          AckPayload ap;
          deserializeAck(buffer, header.payload_len, ap);
          if (ap.checksum_ok) {
            onComplete(true);
          } else {
            std::cerr << "checksum invalid\n";
          }
        });
      } else {
        std::cerr << "Expected ackwnoledge, received other\n";
      }
    });
  }
}

TransferReceiver::TransferReceiver(asio::io_context &io,
                                   asio::ssl::context &ssl_ctx,
                                   uint16_t listen_port)
    : io_context(io), ssl_context(ssl_ctx), acceptor(io),
      listen_port(listen_port) {

  bytes_sent = 0;
}

bool TransferReceiver::start() {
  try {
    bool bound = false;
    for (uint16_t port = listen_port; port <= TCP_PORT_MAX; port++) {
      try {
        acceptor.open(asio::ip::tcp::v4());
        acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port));
        acceptor.listen();
        listen_port = port;
        bound = true;
        std::cout << "listening on port " << port << "\n";
        break;
      } catch (std::exception &) {
        acceptor.close();
        continue;
      }
    }
    if (!bound) {
      std::cerr << "no available ports in range\n";
      return false;
    }
    listenForConnections();
  } catch (std::exception &e) {
    std::cerr << "couldn't start receiver:\n" << e.what() << "\n";
    return false;
  }
  return true;
}

bool TransferReceiver::stop() {

  try {
    acceptor.close();
    if (socket && socket->lowest_layer().is_open()) {
      socket->lowest_layer().close();
    }
    file.close();
  } catch (std::exception &e) {
    std::cerr << e.what() << "\n";
    return false;
  }
  return true;
}

void TransferReceiver::listenForConnections() {

  // new socket for the next connection
  socket = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(
      io_context, ssl_context);

  // acceptor async
  acceptor.async_accept(socket->lowest_layer(), [this](std::error_code ec) {
    if (ec) {
      std::cerr << ec.message() << "\n";
      return;
    }

    socket->async_handshake(
    asio::ssl::stream_base::server, [this](std::error_code ec) {

      // check for lambda error
      if (ec) {
        std::cerr << "handshake error: " << ec.message() << "\n";
        unsigned long err;
        while ((err = ERR_get_error()) != 0) {
          char buf[256];
          ERR_error_string_n(err, buf, sizeof(buf));
          std::cerr << "OpenSSL detail: " << buf << "\n";
        }
        return;
      }

      // expecting header, transfer offer
      asio::async_read(
      *socket, asio::buffer(buffer, HEADER_SIZE),
      [this](std::error_code ec, size_t bytes) {

        // lambda error handling
        if (ec) {
          std::cerr << ec.message() << "\n";
          return;
        }

        // getting header data
        BaseHeader header;
        deserializeHeader(buffer, header);
        if (header.msg_type != MessageType::TRANSFER_OFFER) {
          // not expected
          std::cerr << "received other than TRANSFER_OFFER\n";
          return;
        }
        if (header.payload_len > MAX_BUFFER_SIZE - HEADER_SIZE) {
          std::cerr << "payload too large\n";
          return;
        }
        pending_session_id = header.session_id;

        // reading payload
        asio::async_read(
        *socket,
        asio::buffer(buffer + HEADER_SIZE, header.payload_len),
        [this, header](std::error_code ec, size_t bytes) {

          // lambda error handling
          if (ec) {
            std::cerr << ec.message() << "\n";
            return;
          }

          // reading payload, storing offer
          OfferPayload op;
          deserializeOffer(buffer + HEADER_SIZE, header.payload_len,
                            op);
          pending_offer = op;
          file_size = pending_offer.file_size;

          // validation
          if (onValidateCert && !onValidateCert(socket->native_handle(), op.device_name)) {
              socket->lowest_layer().close();
              listenForConnections();
              return;
          }
          onOffer(op);
        });
      });
    });
  });
}

void TransferReceiver::accept(uint64_t resume_offset) {
  // TODO: send resume_offset in payload when resumable transfers are
  // implemented

  BaseHeader header;
  header.magic = NT_MAGIC;
  header.version = NT_VERSION;
  header.msg_type = MessageType::TRANSFER_ACCEPT;
  header.session_id = pending_session_id;
  header.payload_len = 0;
  header.header_crc = 0;

  auto message =
      std::make_shared<std::vector<uint8_t>>(serializeHeader(header));
  asio::async_write(*socket, asio::buffer(*message),
  [this, message](std::error_code ec, size_t bytes) {
    // DEBUG TODO delete this
    std::cout << "accept sent\n";
    if (ec) {
      std::cerr << "accept write error: " << ec.message()
                << "\n";
      return;
    }
    std::filesystem::path downloads = getDownloadsFolder();
    std::filesystem::path filepath =
        resolveFilePath(downloads, pending_offer.file_name);
    file_path = filepath.string();
    file.open(filepath, std::ios::binary | std::ios::out);
    if (!file.is_open()) {
      std::cerr << "file couldn't be created\n";
      return;
    }
    receiveNextChunk();
  });
}

void TransferReceiver::reject(RejectReason reason) {

  RejectPayload rp;
  rp.reason = reason;
  std::vector<uint8_t> payload = serializeReject(rp);

  BaseHeader header;
  header.magic = NT_MAGIC;
  header.version = NT_VERSION;
  header.msg_type = MessageType::TRANSFER_REJECT;
  header.session_id = pending_session_id;
  header.payload_len = static_cast<uint32_t>(payload.size());
  header.header_crc = 0;

  auto message =
      std::make_shared<std::vector<uint8_t>>(serializeHeader(header));
  message->insert(message->end(), payload.begin(), payload.end());

  asio::async_write(*socket, asio::buffer(*message),
  [this, message](std::error_code ec, size_t bytes) {
    if (ec) {
      std::cerr << "accept write error: " << ec.message()
                << "\n";
      return;
    }
    socket->shutdown();
    socket->lowest_layer().close();
    listenForConnections(); // resume listening
  });
}

void TransferReceiver::receiveNextChunk() {

  asio::async_read(
  *socket, asio::buffer(buffer, HEADER_SIZE),
  [this](std::error_code ec, size_t bytes) {
    if (ec) {
      std::cerr << ec.message() << "\n";
      return;
    }
    BaseHeader header;
    deserializeHeader(buffer, header);
    if (header.msg_type == MessageType::DATA_CHUNK) {
      uint32_t to_read = header.payload_len;
      // keep on receiving
      asio::async_read(*socket, asio::buffer(buffer + HEADER_SIZE, to_read),
      [this](std::error_code ec, size_t bytes) {
        if (ec) {
          std::cerr << ec.message() << "\n";
          return;
        }
        file.write(
            reinterpret_cast<char *>(buffer + HEADER_SIZE),
            bytes);
        bytes_sent += bytes;
        onProgress(bytes_sent, file_size);
        receiveNextChunk();
      });
    } else if (header.msg_type == MessageType::TRANSFER_DONE) {
      // nice, finished, close file and exit
      file.close();
      file.open(file_path, std::ios::binary | std::ios::in);
      file.seekg(0, std::ios::beg);
      
      // compare sha256 key
      unsigned char sha256[32];
      EVP_MD_CTX *ctx = EVP_MD_CTX_new();
      EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
      uint8_t sha_buffer[MAX_BUFFER_SIZE];
      while (file) {
        file.read(reinterpret_cast<char *>(sha_buffer), MAX_BUFFER_SIZE);
        std::streamsize bytes_read = file.gcount();
        if (bytes_read > 0) {
          EVP_DigestUpdate(ctx, sha_buffer, bytes_read);
        }
      }
      unsigned int length = 0;
      EVP_DigestFinal_ex(ctx, sha256, &length);
      EVP_MD_CTX_free(ctx);

      AckPayload ap;
      ap.checksum_ok = (memcmp(sha256, pending_offer.sha256, 32) == 0);

      // send ack to sender
      BaseHeader ackHeader;
      ackHeader.magic = NT_MAGIC;
      ackHeader.version = NT_VERSION;
      ackHeader.msg_type = MessageType::TRANSFER_ACK;
      ackHeader.session_id = pending_session_id;
      std::vector<uint8_t> ackPayload = serializeAck(ap);
      ackHeader.header_crc = 0;
      ackHeader.payload_len = static_cast<uint32_t>(ackPayload.size());
      std::vector<uint8_t> ackMessage = serializeHeader(ackHeader);
      ackMessage.insert(ackMessage.end(), ackPayload.begin(),
                        ackPayload.end());
      asio::write(*socket, asio::buffer(ackMessage));

      // notify UI
      onComplete(ap.checksum_ok);
      file.close();

      listenForConnections(); // resume listening
    } else {
      std::cerr << "unexpected msg when receiving packet\n";
      return;
    }
  });
}

std::filesystem::path getDownloadsFolder() {
#ifdef _WIN32
  PWSTR path = nullptr;
  SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path);
  std::filesystem::path downloads(path);
  CoTaskMemFree(path); // must free after use
  return downloads;
#else
  const char *xdg = getenv("XDG_DOWNLOAD_DIR");
  if (xdg)
    return std::filesystem::path(xdg);
  const char *home = getenv("HOME");
  if (home)
    return std::filesystem::path(home) / "Downloads";
  return std::filesystem::path("."); // fallback, shouldn't happen
#endif
}

std::filesystem::path resolveFilePath(const std::filesystem::path &dir,
                                      const std::string &filename) {
  std::filesystem::path candidate = dir / filename;
  if (!std::filesystem::exists(candidate))
    return candidate;

  std::string stem = candidate.stem().string();
  std::string ext = candidate.extension().string();
  int counter = 1;
  while (std::filesystem::exists(candidate)) {
    candidate = dir / (stem + " (" + std::to_string(counter++) + ")" + ext);
  }
  return candidate;
}

void TransferReceiver::setOnProgress(
    std::function<void(uint64_t, uint64_t)> callback) {
  onProgress = callback;
}

void TransferReceiver::setOnComplete(std::function<void(bool)> callback) {
  onComplete = callback;
}

void TransferReceiver::setOnOffer(std::function<void(OfferPayload)> callback) {
  onOffer = callback;
}

void TransferReceiver::setOnValidateCert(std::function<bool(SSL*, std::string)> callback) {
    onValidateCert = callback;
}

uint16_t TransferReceiver::getPort() { return listen_port; }
