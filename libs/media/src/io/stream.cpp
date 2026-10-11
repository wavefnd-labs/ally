#include <algorithm>
#include <ally/media/stream.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace ally::media {
namespace {
uint32_t u32(const unsigned char *p, size_t n = 4) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        v |= uint32_t(p[i]) << (i * 8);
    }
    return v;
}
void put(std::ostream &out, uint32_t v, size_t n = 4) {
    for (size_t i = 0; i < n; ++i) {
        out.put(char((v >> (i * 8)) & 255));
    }
}
void header(std::ostream &out, uint32_t rate, uint16_t channels, uint32_t bytes) {
    out.write("RIFF", 4);
    put(out, 36 + bytes);
    out.write("WAVEfmt ", 8);
    put(out, 16);
    put(out, 3, 2);
    put(out, channels, 2);
    put(out, rate);
    put(out, rate * channels * 4);
    put(out, channels * 4, 2);
    put(out, 32, 2);
    out.write("data", 4);
    put(out, bytes);
}
} // namespace
WavReader::WavReader(const std::filesystem::path &path) : file_(path, std::ios::binary) {
    auto exact = [&](unsigned char *p, size_t n) {
        if (!file_.read(reinterpret_cast<char *>(p), std::streamsize(n))) {
            throw std::runtime_error("Truncated WAV stream");
        }
    };
    std::array<unsigned char, 16> b{};
    exact(b.data(), 12);
    if (std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) {
        throw std::runtime_error("Streaming requires RIFF/WAVE");
    }
    const uint64_t end = uint64_t(u32(b.data() + 4)) + 8;
    if (end < 12 || end > std::filesystem::file_size(path)) {
        throw std::runtime_error("Invalid stream RIFF size");
    }
    bool fmt = false, data = false;
    for (uint64_t at = 12; at < end;) {
        if (end - at < 8) {
            throw std::runtime_error("Truncated stream chunk");
        }
        file_.seekg(std::streamoff(at));
        exact(b.data(), 8);
        uint64_t size = u32(b.data() + 4), next = at + 8 + size + (size & 1);
        if (next > end) {
            throw std::runtime_error("Stream chunk exceeds RIFF");
        }
        if (!std::memcmp(b.data(), "fmt ", 4)) {
            if (fmt || size < 16) {
                throw std::runtime_error("Invalid stream format chunk");
            }
            exact(b.data(), 16);
            fmt = true;
            info_.format = uint16_t(u32(b.data(), 2));
            info_.channels = uint16_t(u32(b.data() + 2, 2));
            info_.sampleRate = u32(b.data() + 4);
            info_.bits = uint16_t(u32(b.data() + 14, 2));
            if (!info_.channels || info_.channels > 64 || info_.sampleRate < 8000 ||
                info_.sampleRate > 384000 ||
                !((info_.format == 1 &&
                   (info_.bits == 8 || info_.bits == 16 || info_.bits == 24 || info_.bits == 32)) ||
                  (info_.format == 3 && info_.bits == 32)) ||
                u32(b.data() + 12, 2) != uint32_t(info_.channels) * info_.bits / 8 ||
                u32(b.data() + 8) != uint64_t(info_.sampleRate) * info_.channels * info_.bits / 8) {
                throw std::runtime_error("Unsupported or malformed streaming PCM");
            }
        } else if (!std::memcmp(b.data(), "data", 4)) {
            if (data) {
                throw std::runtime_error("Duplicate stream data chunk");
            }
            info_.dataOffset = at + 8;
            info_.dataBytes = size;
            data = true;
        }
        at = next;
    }
    if (!fmt || !data || !info_.dataBytes || info_.dataBytes % (info_.channels * info_.bits / 8)) {
        throw std::runtime_error("Missing or misaligned streaming PCM");
    }
    info_.frames = info_.dataBytes / (info_.channels * info_.bits / 8);
}
Pcm WavReader::read(uint64_t first, size_t count) {
    if (count > 65536 || first > info_.frames) {
        throw std::runtime_error("Invalid streaming read range");
    }
    count = size_t(std::min<uint64_t>(count, info_.frames - first));
    const auto width = size_t(info_.bits / 8);
    std::vector<unsigned char> bytes(count * info_.channels * width);
    file_.clear();
    file_.seekg(std::streamoff(info_.dataOffset + first * info_.channels * width));
    if (!file_.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()))) {
        throw std::runtime_error("Streaming file changed or truncated");
    }
    Pcm pcm{info_.sampleRate, info_.channels, {}};
    pcm.samples.resize(count * info_.channels);
    for (size_t i = 0; i < pcm.samples.size(); ++i) {
        auto value = u32(bytes.data() + i * width, width);
        float f = 0;
        if (info_.format == 3) {
            f = std::bit_cast<float>(value);
        } else if (info_.bits == 8) {
            f = (float(value) - 128) / 128;
        } else {
            const int64_t signedValue = value & (uint32_t(1) << (info_.bits - 1))
                                            ? int64_t(value) - (int64_t(1) << info_.bits)
                                            : value;
            f = float(double(signedValue) / double(int64_t(1) << (info_.bits - 1)));
        }
        if (!std::isfinite(f)) {
            throw std::runtime_error("Non-finite streaming sample");
        }
        pcm.samples[i] = f;
    }
    return pcm;
}
WavWriter::WavWriter(const std::filesystem::path &path, uint32_t rate, uint16_t channels)
    : file_(path, std::ios::binary | std::ios::trunc), rate_(rate), channels_(channels) {
    if (!file_ || rate < 8000 || rate > 384000 || !channels || channels > 64) {
        throw std::runtime_error("Cannot create recording WAV");
    }
    header(file_, rate, channels, 0);
}
WavWriter::~WavWriter() = default;
void WavWriter::append(std::span<const float> samples) {
    if (finished_ || samples.size() % channels_ ||
        frames_ * channels_ * 4 + samples.size() * 4 > UINT32_MAX - 36) {
        throw std::runtime_error("RIFF recording limit reached");
    }
    std::vector<char> bytes(samples.size() * 4);
    for (size_t i = 0; i < samples.size(); ++i) {
        float f = samples[i];
        if (!std::isfinite(f)) {
            throw std::runtime_error("Non-finite recorded PCM");
        }
        auto bits = std::bit_cast<uint32_t>(std::clamp(f, -1.0f, 1.0f));
        for (size_t byte = 0; byte < 4; ++byte) {
            bytes[i * 4 + byte] = char((bits >> (byte * 8)) & 255);
        }
    }
    file_.write(bytes.data(), std::streamsize(bytes.size()));
    if (!file_) {
        throw std::runtime_error("Recording write failed");
    }
    const auto previous = frames_;
    frames_ += samples.size() / channels_;
    if (frames_ / rate_ != previous / rate_) {
        file_.seekp(0);
        header(file_, rate_, channels_, uint32_t(frames_ * channels_ * 4));
        file_.seekp(0, std::ios::end);
        file_.flush();
        if (!file_) {
            throw std::runtime_error("Recording checkpoint failed");
        }
    }
}
void WavWriter::finish() {
    if (finished_) {
        return;
    }
    file_.seekp(0);
    header(file_, rate_, channels_, uint32_t(frames_ * channels_ * 4));
    file_.flush();
    if (!file_) {
        throw std::runtime_error("Recording finalize failed");
    }
    file_.close();
    finished_ = true;
}
} // namespace ally::media
