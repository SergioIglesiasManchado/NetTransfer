#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ── Magic & Version

static constexpr uint32_t NT_MAGIC = 0x4E544652; // "LDRP"
static constexpr uint8_t NT_VERSION = 1;

// ── Ports

static constexpr uint16_t UDP_DISCOVERY_PORT = 50000;
static constexpr uint16_t TCP_PORT_MIN = 50001;
static constexpr uint16_t TCP_PORT_MAX = 50100;

// ── Message Types

enum class MessageType : uint8_t {
  DISCOVERY_BROADCAST = 0x01,
  DISCOVERY_REPLY = 0x02,
  TRANSFER_OFFER = 0x10,
  TRANSFER_ACCEPT = 0x11,
  TRANSFER_REJECT = 0x12,
  TRANSFER_DONE = 0x13,
  TRANSFER_ACK = 0x14,
  DATA_CHUNK = 0x20,
  KEEPALIVE = 0x30,
  ERROR_MSG = 0x40,
};

// ── Reject / Error Reason Codes

enum class RejectReason : uint8_t {
  USER_DECLINED = 0x01,
  DISK_FULL = 0x02,
  INVALID_FILENAME = 0x03,
  UNKNOWN = 0xFF,
};

enum class ErrorCode : uint8_t {
  CHECKSUM_MISMATCH = 0x01,
  TIMEOUT = 0x02,
  PROTOCOL_VIOLATION = 0x03,
  UNKNOWN = 0xFF,
};

// ── Base Header
//
//  Every TCP message starts with this 22-byte prefix (packed, no padding).
//
//  [ magic : 4 ][ version : 1 ][ msg_type : 1 ][ session_id : 8 ]
//  [ payload_len : 4 ][ header_crc : 4 ]
//
//  header_crc is CRC32 of the first 18 bytes (everything before the crc field).

static constexpr size_t HEADER_SIZE = 22;

struct BaseHeader {
  uint32_t magic;
  uint8_t version;
  MessageType msg_type;
  uint64_t session_id;
  uint32_t payload_len;
  uint32_t header_crc;
};

// ── Payloads

struct OfferPayload {
  uint64_t file_size;
  uint64_t resume_offset;
  uint8_t sha256[32];
  std::string file_name; // UTF-8, only name, no path
                         // when serializing, 2b for filename size, then raw
                         // bytes for the name itself
};

struct RejectPayload {
  RejectReason reason;
};

struct DonePayload {
  uint8_t sha256[32];
};

struct AckPayload {
  bool checksum_ok;
};

struct ErrorPayload {
  ErrorCode code;
  std::string message; // UTF-8
};

struct DiscoveryPayload {
  std::string device_name; // UTF-8
  uint16_t tcp_port;
};

// ── Serialization / Deserialization

// Serializes a BaseHeader into exactly HEADER_SIZE bytes (network byte order).
// Computes and writes the CRC automatically — pass header_crc = 0 before
// calling.
std::vector<uint8_t> serializeHeader(BaseHeader &header);

// Deserializes HEADER_SIZE bytes into a BaseHeader.
// Returns false if magic, version, or CRC are invalid.
bool deserializeHeader(const uint8_t *data, BaseHeader &out);

// Payload serializers — return raw bytes ready to send after the header.
std::vector<uint8_t> serializeOffer(const OfferPayload &p);
std::vector<uint8_t> serializeReject(const RejectPayload &p);
std::vector<uint8_t> serializeDone(const DonePayload &p);
std::vector<uint8_t> serializeAck(const AckPayload &p);
std::vector<uint8_t> serializeError(const ErrorPayload &p);
std::vector<uint8_t> serializeDiscovery(const DiscoveryPayload &p);

// Payload deserializers — read from raw bytes into the struct.
// Return false if the data is malformed or too short.
bool deserializeOffer(const uint8_t *data, size_t len, OfferPayload &out);
bool deserializeReject(const uint8_t *data, size_t len, RejectPayload &out);
bool deserializeDone(const uint8_t *data, size_t len, DonePayload &out);
bool deserializeAck(const uint8_t *data, size_t len, AckPayload &out);
bool deserializeError(const uint8_t *data, size_t len, ErrorPayload &out);
bool deserializeDiscovery(const uint8_t *data, size_t len,
                          DiscoveryPayload &out);

// ── CRC32

uint32_t crc32(const uint8_t *data, size_t len);
