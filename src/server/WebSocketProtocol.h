#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Stream type identifiers for WebSocket binary frames
enum WSStreamType : uint8_t {
    WS_STREAM_SPECTRUM  = 0x01,
    WS_STREAM_WATERFALL = 0x02,
    WS_STREAM_AUDIO     = 0x03,
    WS_STREAM_IQ        = 0x04
};

// Payload data format
enum WSDataFormat : uint8_t {
    WS_FORMAT_FLOAT32         = 0x01,
    WS_FORMAT_COMPLEX_FLOAT32 = 0x02
};

static const uint32_t WS_MAGIC = 0x43534452; // "CSDR"

// Binary message header — 24 bytes, packed, little-endian.
// NOTE: All multi-byte fields are transmitted in host byte order (assumed little-endian).
// The JavaScript client reads with getUint32(..., true) for little-endian.
//
// Wire layout:
//   [0..3]   magic        uint32_t   0x43534452
//   [4]      stream_type  uint8_t
//   [5]      data_format  uint8_t
//   [6..7]   reserved     uint16_t   0
//   [8..15]  center_freq  int64_t    Hz
//   [16..19] sample_rate  uint32_t   Hz
//   [20..23] num_samples  uint32_t
#pragma pack(push, 1)
struct WSDataHeader {
    uint32_t magic;
    uint8_t  stream_type;
    uint8_t  data_format;
    uint16_t reserved;
    int64_t  center_freq;
    uint32_t sample_rate;
    uint32_t num_samples;
};
#pragma pack(pop)

static_assert(sizeof(WSDataHeader) == 24, "WSDataHeader must be exactly 24 bytes");

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------

// Serialize a binary message whose payload is an array of floats.
inline std::string serializeBinaryMessage(WSStreamType streamType,
                                          WSDataFormat format,
                                          int64_t      centerFreq,
                                          uint32_t     sampleRate,
                                          const float* data,
                                          uint32_t     numSamples) {
    size_t payloadBytes = static_cast<size_t>(numSamples) * sizeof(float);

    std::string buf;
    buf.resize(sizeof(WSDataHeader) + payloadBytes);

    WSDataHeader hdr;
    hdr.magic       = WS_MAGIC;
    hdr.stream_type = static_cast<uint8_t>(streamType);
    hdr.data_format = static_cast<uint8_t>(format);
    hdr.reserved    = 0;
    hdr.center_freq = centerFreq;
    hdr.sample_rate = sampleRate;
    hdr.num_samples = numSamples;

    std::memcpy(&buf[0], &hdr, sizeof(hdr));
    if (payloadBytes > 0) {
        std::memcpy(&buf[sizeof(hdr)], data, payloadBytes);
    }
    return buf;
}

// Generic version: payload is raw bytes of arbitrary format.
inline std::string serializeBinaryMessage(WSStreamType streamType,
                                          WSDataFormat format,
                                          int64_t      centerFreq,
                                          uint32_t     sampleRate,
                                          const void*  data,
                                          size_t       dataBytes,
                                          uint32_t     numSamples) {
    std::string buf;
    buf.resize(sizeof(WSDataHeader) + dataBytes);

    WSDataHeader hdr;
    hdr.magic       = WS_MAGIC;
    hdr.stream_type = static_cast<uint8_t>(streamType);
    hdr.data_format = static_cast<uint8_t>(format);
    hdr.reserved    = 0;
    hdr.center_freq = centerFreq;
    hdr.sample_rate = sampleRate;
    hdr.num_samples = numSamples;

    std::memcpy(&buf[0], &hdr, sizeof(hdr));
    if (dataBytes > 0) {
        std::memcpy(&buf[sizeof(hdr)], data, dataBytes);
    }
    return buf;
}

// Build a simple JSON text message: {"type":"<type>", ...body... }
inline std::string buildJsonMessage(const std::string& type,
                                    const std::string& body) {
    std::string msg = "{\"type\":\"" + type + "\"";
    if (!body.empty()) {
        msg += "," + body;
    }
    msg += "}";
    return msg;
}

// Build a subscribe-response JSON message with stream metadata.
inline std::string buildSubscribeResponse(const std::string& stream,
                                          int64_t            centerFreq,
                                          uint32_t           sampleRate,
                                          uint32_t           fftSize,
                                          const std::string& format) {
    std::string body;
    body += "\"stream\":\"" + stream + "\"";
    body += ",\"center_freq\":" + std::to_string(centerFreq);
    body += ",\"sample_rate\":" + std::to_string(sampleRate);
    body += ",\"fft_size\":"   + std::to_string(fftSize);
    body += ",\"format\":\""   + format + "\"";
    return buildJsonMessage("subscribe_ok", body);
}
