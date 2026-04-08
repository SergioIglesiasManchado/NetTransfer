#include "protocol.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#define POLYNOMIAL 0xEDB88320

static uint32_t crc_table[256];
static bool crc_table_ready = false;

/**
 * CRC32 function.
 *
 * This function acts like a hash function, whatever you put into,
 *  it will throw an uint32_t back. if you introduce the same thing,
 *  it will always return the same thing.
 */
uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;

  if (!crc_table_ready) {
    for (int i = 0; i < 256; i++) {
      uint32_t stored_crc = i;
      for (int j = 0; j < 8; j++) {
        if (stored_crc & 1) {
          stored_crc = stored_crc >> 1;
          stored_crc = stored_crc ^ POLYNOMIAL;
        } else {
          stored_crc = stored_crc >> 1;
        }
      }
      crc_table[i] = stored_crc;
    }
  }

  for (int i = 0; i < len; i++) {
    uint8_t index = (crc ^ data[i]) & 0xFF;
    crc = (crc >> 8) ^ crc_table[index];
  }

  return crc ^ 0xFFFFFFFF;
}

/**
 * This function takes a reference to a header and serializes it to a vector of
 * bytes
 * @return el vector de bytes (uint8_t), la cabezera serializada
 */
std::vector<uint8_t> serializeHeader(BaseHeader &header) {

  std::vector<uint8_t> buffer;
  buffer.reserve(HEADER_SIZE);

  // introducir el numero magico
  for (int i = 0; i < 4; i++) {
    buffer.push_back((header.magic >> (24 - 8 * i)) & 0xFF);
  }

  // introducir la version
  buffer.push_back(header.version);

  // introducir el tipo de mensaje
  buffer.push_back(static_cast<uint8_t>(header.msg_type));

  // introducir el id de sesion
  for (int i = 0; i < 8; i++) {
    buffer.push_back((header.session_id >> (56 - 8 * i)) & 0xFF);
  }

  // introducir el tamaño del payload
  for (int i = 0; i < 4; i++) {
    buffer.push_back((header.payload_len >> (24 - 8 * i)) & 0xFF);
  }

  uint32_t crc = crc32(buffer.data(), 18);

  // introducir el crc
  for (int i = 0; i < 4; i++) {
    buffer.push_back((crc >> (24 - 8 * i)) & 0xFF);
  }

  return buffer;
}

/**
 * This function will take a reference to the raw data and a reference to an
 * empty Header struct it will extract the data, and if it's correct, it will
 * pass the data to the struct
 * @return true if all went well, false if it couldn't finish or there was a
 * problem
 */

bool deserializeHeader(const uint8_t *data, BaseHeader &out) {

  int counter = 0;

  // extract the magic number
  uint32_t magic = 0;
  for (int i = 0; i < 4; i++) {
    uint32_t temp = static_cast<uint32_t>(data[i]);
    temp <<= (24 - 8 * i);
    magic = magic | temp;
    counter++;
  }

  // extract the version
  uint8_t version = data[counter++];

  // extract the msg_type
  MessageType msg_type = static_cast<MessageType>(data[counter++]);

  // extract the session_id
  uint64_t session_id = 0;
  for (int i = 0; i < 8; i++) {
    uint64_t temp = static_cast<uint64_t>(data[counter++]);
    temp <<= (56 - 8 * i);
    session_id = session_id | temp;
  }

  // extract the payload_len
  uint32_t payload_len = 0;
  for (int i = 0; i < 4; i++) {
    uint32_t temp = static_cast<uint32_t>(data[counter++]);
    temp <<= (24 - 8 * i);
    payload_len = payload_len | temp;
  }

  // extract the header_crc
  uint32_t header_crc = 0;
  for (int i = 0; i < 4; i++) {
    uint32_t temp = static_cast<uint32_t>(data[counter++]);
    temp <<= (24 - 8 * i);
    header_crc = header_crc | temp;
  }

  // checking the input
  if (magic != NT_MAGIC) {
    return false;
  } else if (version != NT_VERSION) {
    return false;
  } else if (header_crc != crc32(data, 18)) {
    return false;
  }

  out.magic = magic;
  out.version = version;
  out.msg_type = msg_type;
  out.session_id = session_id;
  out.payload_len = payload_len;
  out.header_crc = header_crc;

  return true;
}

/**
 * Function that takes a reference to an OfferPayload and serializes it
 * @return a vector of uint8_t with the payload serialized
 */
std::vector<uint8_t> serializeOffer(const OfferPayload &p) {

  std::vector<uint8_t> buffer;

  // push back the file_size
  for (int i = 0; i < 8; i++) {
    buffer.push_back((p.file_size >> (56 - 8 * i)) & 0xFF);
  }

  // push back the resume_offset
  for (int i = 0; i < 8; i++) {
    buffer.push_back((p.resume_offset >> (56 - 8 * i)) & 0xFF);
  }

  // push back the sha256
  for (int i = 0; i < 32; i++) {
    buffer.push_back(p.sha256[i]);
  }

  // push back the file_name length
  for (int i = 0; i < 2; i++) {
    buffer.push_back((p.file_name.length() >> (8 - 8 * i)) & 0xFF);
  }

  // push back the file_name itself
  for (int i = 0; i < p.file_name.length(); i++) {
    buffer.push_back(p.file_name[i]);
  }

  return buffer;
}

bool deserializeOffer(const uint8_t *data, size_t len, OfferPayload &out) {

  // minimum requiered = 50B, + variables bytes for file_name
  if (len < 50) {
    return false;
  }

  int counter = 0;

  // extract the file_size
  uint64_t file_size = 0;
  for (int i = 0; i < 8; i++) {
    uint64_t temp = static_cast<uint64_t>(data[counter++]);
    temp <<= (56 - 8 * i);
    file_size = file_size | temp;
  }

  // extract the resume_offset
  uint64_t resume_offset = 0;
  for (int i = 0; i < 8; i++) {
    uint64_t temp = static_cast<uint64_t>(data[counter++]);
    temp <<= (56 - 8 * i);
    resume_offset = resume_offset | temp;
  }

  // extract the sha256
  uint8_t sha256[32];
  for (int i = 0; i < 32; i++) {
    sha256[i] = data[counter++];
  }

  // extract the file_name length
  uint16_t file_name_length = 0;
  for (int i = 0; i < 2; i++) {
    uint16_t temp = static_cast<uint16_t>(data[counter++]);
    temp <<= (8 - 8 * i);
    file_name_length = file_name_length | temp;
  }

  // check for out of bounds
  if (counter + file_name_length > len) {
    return false;
  }

  // extract the file_name
  std::string file_name;
  for (int i = 0; i < file_name_length; i++) {
    uint8_t temp = data[counter++];
    file_name.push_back(temp);
  }

  out.file_size = file_size;
  out.resume_offset = resume_offset;
  memcpy(out.sha256, sha256, 32);
  out.file_name = file_name;

  return true;
}

std::vector<uint8_t> serializeReject(const RejectPayload &p) {

  std::vector<uint8_t> buffer;
  buffer.push_back(static_cast<uint8_t>(p.reason));

  return buffer;
}

bool deserializeReject(const uint8_t *data, size_t len, RejectPayload &out) {

  if (len < 1) {
    return false;
  }
  out.reason = static_cast<RejectReason>(data[0]);

  return true;
}

std::vector<uint8_t> serializeDone(const DonePayload &p) {

  std::vector<uint8_t> buffer;
  buffer.reserve(32);

  for (int i = 0; i < 32; i++) {
    buffer.push_back(p.sha256[i]);
  }

  return buffer;
}

bool deserializeDone(const uint8_t *data, size_t len, DonePayload &out) {

  if (len != 32) {
    return false;
  }

  for (int i = 0; i < 32; i++) {
    out.sha256[i] = data[i];
  }

  return true;
}

std::vector<uint8_t> serializeAck(const AckPayload &p) {

  std::vector<uint8_t> buffer;
  buffer.push_back(static_cast<uint8_t>(p.checksum_ok));

  return buffer;
}

bool deserializeAck(const uint8_t *data, size_t len, AckPayload &out) {

  if (len < 1) {
    return false;
  }

  out.checksum_ok = static_cast<bool>(data[0]);
  return true;
}

std::vector<uint8_t> serializeError(const ErrorPayload &p) {

  std::vector<uint8_t> buffer;

  buffer.push_back(static_cast<uint8_t>(p.code));

  uint16_t message_lenght = p.message.length();
  for (int i = 0; i < 2; i++) {
    buffer.push_back((message_lenght >> (8 - 8 * i)) & 0xFF);
  }

  for (int i = 0; i < message_lenght; i++) {
    buffer.push_back(p.message[i]);
  }

  return buffer;
}

bool deserializeError(const uint8_t *data, size_t len, ErrorPayload &out) {

  if (len < 3) {
    return false;
  }

  int counter = 0;
  ErrorCode code = static_cast<ErrorCode>(data[counter++]);

  uint16_t message_lenght = 0;
  for (int i = 0; i < 2; i++) {
    uint16_t temp = static_cast<uint16_t>(data[counter++]);
    temp <<= (8 - 8 * i);
    message_lenght = message_lenght | temp;
  }

  if (counter + message_lenght > len) {
    return false;
  }

  std::string message;
  for (int i = 0; i < message_lenght; i++) {
    message.push_back(data[counter++]);
  }

  out.code = code;
  out.message = message;
  return true;
}

std::vector<uint8_t> serializeDiscovery(const DiscoveryPayload &p) {

  std::vector<uint8_t> buffer;

  for (int i = 0; i < 2; i++) {
    buffer.push_back((p.tcp_port >> (8 - 8 * i)) & 0xFF);
  }

  uint16_t device_name_length = static_cast<uint16_t>(p.device_name.length());
  for (int i = 0; i < 2; i++) {
    buffer.push_back((device_name_length >> (8 - 8 * i)) & 0xFF);
  }

  for (int i = 0; i < device_name_length; i++) {
    buffer.push_back(p.device_name[i]);
  }

  return buffer;
}

bool deserializeDiscovery(const uint8_t *data, size_t len,
                          DiscoveryPayload &out) {

  if (len < 4) {
    return false;
  }

  int counter = 0;

  uint16_t tcp_port = 0;
  for (int i = 0; i < 2; i++) {
    uint16_t temp = static_cast<uint16_t>(data[counter++]);
    temp <<= (8 - 8 * i);
    tcp_port = tcp_port | temp;
  }

  uint16_t device_name_length = 0;
  for (int i = 0; i < 2; i++) {
    uint16_t temp = static_cast<uint16_t>(data[counter++]);
    temp <<= (8 - 8 * i);
    device_name_length = device_name_length | temp;
  }

  if (counter + device_name_length > len) {
    return false;
  }

  std::string device_name;
  for (int i = 0; i < device_name_length; i++) {
    device_name.push_back(data[counter++]);
  }

  out.tcp_port = tcp_port;
  out.device_name = device_name;
  return true;
}
