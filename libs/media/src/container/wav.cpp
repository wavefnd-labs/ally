#include <algorithm>
#include <ally/media/wav.hpp>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace ally::media {
namespace {
constexpr size_t maxBytes = 512 * 1024 * 1024;
uint64_t read(std::span<const std::byte> b, size_t p, size_t n) {
    if (p > b.size() || n > b.size() - p) {
        throw std::runtime_error("Truncated WAV");
    }
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        v |= uint64_t(std::to_integer<uint8_t>(b[p + i])) << (8 * i);
    }
    return v;
}

bool tag(std::span<const std::byte> b, size_t p, const char *s) {
    return p <= b.size() && b.size() - p >= 4 && std::memcmp(b.data() + p, s, 4) == 0;
}

void append(std::vector<std::byte> &b, uint32_t v, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        b.push_back(std::byte((v >> (8 * i)) & 255));
    }
}

void appendTag(std::vector<std::byte> &b, const char *s) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(std::byte(s[i]));
    }
}
} // namespace

Pcm decodeWav(std::span<const std::byte> b) {
    if (b.size() > maxBytes || b.size() < 12 || (!tag(b, 0, "RIFF") && !tag(b, 0, "RF64")) ||
        !tag(b, 8, "WAVE")) {
        throw std::runtime_error("Expected RIFF/RF64 WAVE (AIFF is not implemented)");
    }
    const bool rf64 = tag(b, 0, "RF64");
    uint64_t end = read(b, 4, 4) + 8, dataBytes = 0, sampleCount = 0;
    if (rf64) {
        if (b.size() < 48 || read(b, 4, 4) != UINT32_MAX || !tag(b, 12, "ds64") || read(b, 16, 4) < 28 ||
            read(b, 44, 4) != 0 || read(b, 20, 8) > maxBytes - 8) {
            throw std::runtime_error("Invalid or unsupported RF64 ds64");
        }
        end = read(b, 20, 8) + 8;
        dataBytes = read(b, 28, 8);
        sampleCount = read(b, 36, 8);
    }
    if (end > b.size() || end < 12) {
        throw std::runtime_error("Invalid RIFF size");
    }
    b = b.first(size_t(end));
    bool haveFmt = false, haveData = false, haveDs64 = false;
    uint32_t rate = 0, byteRate = 0;
    uint16_t format = 0, channels = 0, bits = 0, align = 0;
    std::span<const std::byte> data;
    for (size_t p = 12; p < b.size();) {
        if (b.size() - p < 8) {
            throw std::runtime_error("Truncated chunk header");
        }
        uint64_t length = read(b, p + 4, 4);
        if (rf64 && tag(b, p, "data") && length == UINT32_MAX) {
            length = dataBytes;
        }
        if (length > b.size() - p - 8) {
            throw std::runtime_error("Chunk exceeds RF64 bounds");
        }
        size_t len = size_t(length);
        if (len > b.size() - p - 8) {
            throw std::runtime_error("Chunk exceeds RIFF bounds");
        }
        if (tag(b, p, "ds64")) {
            if (!rf64 || haveDs64 || p != 12) {
                throw std::runtime_error("Invalid ds64 chunk");
            }
            haveDs64 = true;
        } else if (tag(b, p, "fmt ")) {
            if (haveFmt || len < 16) {
                throw std::runtime_error("Invalid fmt chunk");
            }
            format = uint16_t(read(b, p + 8, 2));
            channels = uint16_t(read(b, p + 10, 2));
            rate = uint32_t(read(b, p + 12, 4));
            byteRate = uint32_t(read(b, p + 16, 4));
            align = uint16_t(read(b, p + 20, 2));
            bits = uint16_t(read(b, p + 22, 2));
            haveFmt = true;
        } else if (tag(b, p, "data")) {
            if (haveData) {
                throw std::runtime_error("Multiple data chunks unsupported");
            }
            data = b.subspan(p + 8, len);
            haveData = true;
        }
        const size_t advance = 8 + len + (len & 1);
        if (advance > b.size() - p) {
            throw std::runtime_error("Missing chunk padding");
        }
        p += advance;
    }
    if (!haveFmt || !haveData || channels == 0 || channels > 64 || rate < 8000 || rate > 384000 ||
        !((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
          (format == 3 && bits == 32))) {
        throw std::runtime_error("Unsupported PCM format");
    }
    const size_t width = bits / 8;
    if (align != channels * width || byteRate != uint64_t(rate) * align || data.size() % align) {
        throw std::runtime_error("Invalid PCM alignment or byte rate");
    }
    if (rf64 && (!haveDs64 || data.size() != dataBytes || data.size() / align != sampleCount)) {
        throw std::runtime_error("RF64 data size/sample count mismatch");
    }
    Pcm pcm{rate, channels, {}};
    pcm.samples.resize(data.size() / width);
    for (size_t i = 0; i < pcm.samples.size(); ++i) {
        uint32_t v = uint32_t(read(data, i * width, width));
        float f;
        if (format == 3) {
            f = std::bit_cast<float>(v);
        } else if (bits == 8) {
            f = (float(v) - 128.0f) / 128.0f;
        } else {
            const int64_t signedValue =
                (v & (uint32_t(1) << (bits - 1))) ? int64_t(v) - (int64_t(1) << bits) : int64_t(v);
            f = float(double(signedValue) / double(int64_t(1) << (bits - 1)));
        }
        if (!std::isfinite(f)) {
            throw std::runtime_error("Non-finite PCM sample");
        }
        pcm.samples[i] = f;
    }
    return pcm;
}

Pcm readWav(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("Cannot open WAV");
    }
    auto n = f.tellg();
    if (n < 0 || uint64_t(n) > maxBytes) {
        throw std::runtime_error("WAV exceeds 512 MiB limit");
    }
    std::vector<std::byte> bytes(static_cast<size_t>(n));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()))) {
        throw std::runtime_error("Cannot read WAV");
    }
    return decodeWav(bytes);
}

std::vector<std::byte> encodeWav(const Pcm &p) {
    if (p.channels == 0 || p.channels > 64 || p.sampleRate < 8000 || p.sampleRate > 384000 ||
        p.samples.size() % p.channels || p.samples.size() > (maxBytes - 44) / 2) {
        throw std::runtime_error("Invalid PCM for WAV export");
    }
    std::vector<std::byte> b;
    b.reserve(44 + p.samples.size() * 2);
    appendTag(b, "RIFF");
    append(b, uint32_t(36 + p.samples.size() * 2), 4);
    appendTag(b, "WAVE");
    appendTag(b, "fmt ");
    append(b, 16, 4);
    append(b, 1, 2);
    append(b, p.channels, 2);
    append(b, p.sampleRate, 4);
    append(b, p.sampleRate * p.channels * 2, 4);
    append(b, p.channels * 2, 2);
    append(b, 16, 2);
    appendTag(b, "data");
    append(b, uint32_t(p.samples.size() * 2), 4);
    for (float v : p.samples) {
        if (!std::isfinite(v)) {
            throw std::runtime_error("Non-finite PCM sample");
        }
        auto s = static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        append(b, static_cast<uint16_t>(s), 2);
    }
    return b;
}

void writeWav(const std::filesystem::path &path, const Pcm &p) {
    auto b = encodeWav(p);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()))) {
        throw std::runtime_error("Cannot write WAV");
    }
}
} // namespace ally::media
