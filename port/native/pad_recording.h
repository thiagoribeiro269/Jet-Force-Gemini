#pragma once
#include "original_input.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace jfg_native {

// Recording of a play session: one record per attempt of a host tick. It
// holds the N64 pad state handed to joyRead and the host commands issued
// before that tick. An attempt that stops (NotPortedError) is retried by the
// next record at the same host tick, so replaying the records through
// NativeSession::step reproduces the session, stops included.
struct PadRecord { PadState pad; uint8_t flags = 0; };
constexpr uint8_t RecordRestart = 1, RecordPauseToggle = 2, RecordFlags = 3;
struct PadRecordingHeader { uint64_t dataDigest = 0; uint8_t controlMode = 0; };
struct PadRecording { PadRecordingHeader header; std::vector<PadRecord> records; };

// FNV-1a 64: identifies the converted data a recording was made with.
inline uint64_t fnv1a(const std::vector<uint8_t> &bytes, uint64_t hash = 1469598103934665603ull) {
    for (uint8_t byte : bytes) hash = (hash ^ byte) * 1099511628211ull;
    return hash;
}

// File layout: "JFGPAD1\0", u64 data digest, u8 control mode, 7 zero bytes,
// then 8-byte records until the end: u16 buttons, s8 stick x, s8 stick y,
// u8 flags, 3 zero bytes. Little endian.
class PadRecorder {
    std::ofstream out_;
public:
    PadRecorder(const std::filesystem::path &path, const PadRecordingHeader &header) : out_(path, std::ios::binary | std::ios::trunc) {
        uint8_t bytes[24] = {'J', 'F', 'G', 'P', 'A', 'D', '1', 0};
        for (int i = 0; i < 8; ++i) bytes[8 + i] = uint8_t(header.dataDigest >> (8 * i));
        bytes[16] = header.controlMode;
        out_.write(reinterpret_cast<const char *>(bytes), sizeof bytes);
        if (!out_) throw std::runtime_error("Cannot create the pad recording");
    }
    void append(const PadRecord &record) {
        const uint8_t bytes[8] = {uint8_t(record.pad.button), uint8_t(record.pad.button >> 8), uint8_t(record.pad.stickX),
                                  uint8_t(record.pad.stickY), record.flags, 0, 0, 0};
        out_.write(reinterpret_cast<const char *>(bytes), sizeof bytes);
    }
    void flush() {
        out_.flush();
        if (!out_) throw std::runtime_error("Cannot write the pad recording");
    }
};

inline PadRecording readPadRecording(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.size() < 24 || std::memcmp(bytes.data(), "JFGPAD1\0", 8) || (bytes.size() - 24) % 8)
        throw std::runtime_error("Wrong pad recording format");
    PadRecording recording;
    for (int i = 0; i < 8; ++i) recording.header.dataDigest |= uint64_t(bytes[8 + i]) << (8 * i);
    recording.header.controlMode = bytes[16];
    if (recording.header.controlMode > 1) throw std::runtime_error("Unknown control mode in the pad recording");
    for (size_t i = 17; i < 24; ++i) if (bytes[i]) throw std::runtime_error("Nonzero pad recording header padding");
    for (size_t at = 24; at < bytes.size(); at += 8) {
        PadRecord record;
        record.pad.button = uint16_t(bytes[at] | bytes[at + 1] << 8);
        record.pad.stickX = int8_t(bytes[at + 2]);
        record.pad.stickY = int8_t(bytes[at + 3]);
        record.flags = bytes[at + 4];
        if ((record.flags & ~RecordFlags) || bytes[at + 5] || bytes[at + 6] || bytes[at + 7])
            throw std::runtime_error("Invalid pad record");
        validatePad(record.pad);
        recording.records.push_back(record);
    }
    return recording;
}
}
