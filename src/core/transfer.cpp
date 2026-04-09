
#include "transfer.h"
#include "protocol.h"
#include <random>

TransferSender::TransferSender(asio::io_context &io, asio::ssl::context& ssl_ctx, std::string target_ip, uint16_t target_port, std::string file_path)
    : io_context(io), socket(io, ssl_ctx), target_ip(target_ip), target_port(target_port), file_path(file_path) {

    bytes_sent = 0;
    
    // generate random number for session id
    std::random_device rd;
    std::mt19937_64 eng(rd());
    std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);
    session_id = dist(eng);
}

bool TransferSender::start() {

    // open the file
    file.open(file_path, std::ios::binary);
    if (file.is_open()) {
        file.seekg(0, std::ios::end);
        file_size = file.tellg();
        file.seekg(0, std::ios::beg);
    } else {
        return false;
    }

    // compute the sha256
    unsigned char sha256[32];
    SHA256_CTX c;
    if (SHA256_Init(&c) != 1) return false;
    uint8_t sha_buffer[MAX_BUFFER_SIZE];
    while (file) {
        file.read(reinterpret_cast<char*>(sha_buffer), MAX_BUFFER_SIZE);
        std::streamsize bytes_read = file.gcount();  // how many bytes actually read
        if (bytes_read > 0) {
            SHA256_Update(&c, sha_buffer, bytes_read);
        }
    }
    SHA256_Final(sha256, &c);
    memcpy(this->sha256, sha256, 32); // sent sha to rest of the class
    file.seekg(0, std::ios::beg); // rewind for the actual transfer later

    // connect to target device
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(target_ip), target_port);
    socket.lowest_layer().connect(endpoint);
    socket.handshake(asio::ssl::stream_base::client);

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

    std::vector<uint8_t> serializedPayload = serializeOffer(payload);
    header.payload_len = static_cast<uint32_t>(serializedPayload.size());
    std::vector<uint8_t> message = serializeHeader(header);
    message.insert(message.end(), serializedPayload.begin(), serializedPayload.end());

    asio::write(socket, asio::buffer(message));

    // wait for response (async wait)
    asio::async_read(socket, asio::buffer(buffer, HEADER_SIZE),
    [this](std::error_code ec, size_t bytes) {
        
        if (ec) { std::cerr << ec.message() << "\n"; onComplete(false); return; }
        // only header was read, check header
        BaseHeader header;
        deserializeHeader(buffer, header);
        if (header.msg_type == MessageType::TRANSFER_ACCEPT) {
            if (header.payload_len == 0) {
                sendNextChunk();
            } else {
                // clear buffer in case of noise
                asio::async_read(socket, asio::buffer(buffer, header.payload_len),
                    [this](std::error_code ec, size_t bytes) {
                        sendNextChunk();
                    });
            }
        } else if (header.msg_type == MessageType::TRANSFER_REJECT) {
            asio::async_read(socket, asio::buffer(buffer, header.payload_len),
            [this](std::error_code ec, size_t bytes) {
                if (ec) { std::cerr << ec.message() << "\n"; onComplete(false); return; }
                RejectPayload rp;
                deserializeReject(buffer, header.payload_len, rp); // we have reason for reject
                std::cerr << "transfer rejected: " << static_cast<int>(rp.reason) << "\n"; // for now logged
                onComplete(false);
                return;
            });
        } else {
            std::cerr << "unexpected header type\n";
        }

    });

}

bool TransferSender::stop() {

    try {
        socket.shutdown();
        socket.lowest_layer().close();
        file.close();
    } catch (std::exception& e) {
        std::cerr << e.what() << "\n";
        return false;
    }
    return true;
}

void TransferSender::setOnProgress(std::function<void(uint64_t bytes_sent, uint64_t total)> callback) {
    onProgress = callback;
}

void TransferSender::setOnComplete(std::function<void(bool)> callback) {
    onComplete = callback;
}

void TransferSender::sendNextChunk() {

    file.read(reinterpret_cast<char*>(buffer), MAX_BUFFER_SIZE);
    std::streamsize bytes_read = file.gcount();
    if (bytes_read > 0) {
        // keep on transfering
        asio::async_write(socket, asio::buffer(buffer, bytes_read),
        [this](std::error_code ec, size_t bytes) {
            if (ec) { std::cerr << ec.message() << "\n"; onComplete(false); return; }
            bytes_sent += bytes;
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
            [this, header](std::error_code ec, size_t bytes) {
                if (ec) { std::cerr << ec.message() << "\n"; onComplete(false); return; }
                BaseHeader header;
                deserializeHeader(buffer, header);
                if (header.msg_type == MessageType::TRANSFER_ACK) {
                    asio::async_read(socket, asio::buffer(buffer, header.payload_len),
                    [this](std::error_code ec, size_t bytes) {
                        if (ec) { std::cerr << ec.message() << "\n"; onComplete(false); return; }
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
