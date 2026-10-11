#include <algorithm>
#include <ally/media/stream.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ally::media {
namespace {
uint64_t integer(const unsigned char *p, size_t n = 4) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        v |= uint64_t(p[i]) << (i * 8);
    }
    return v;
}
void put(std::ostream &out, uint64_t v, size_t n = 4) {
    for (size_t i = 0; i < n; ++i) {
        out.put(char((v >> (i * 8)) & 255));
    }
}
std::string header(uint32_t rate, uint16_t channels, uint64_t bytes) {
    std::ostringstream out;
    const bool large = bytes > UINT32_MAX - 72;
    out.write(large ? "RF64" : "RIFF", 4);
    put(out, large ? UINT32_MAX : 72 + bytes);
    out.write("WAVE", 4);
    out.write(large ? "ds64" : "JUNK", 4);
    put(out, 28);
    put(out, large ? bytes + 72 : 0, 8);
    put(out, large ? bytes : 0, 8);
    put(out, large ? bytes / (channels * 4) : 0, 8);
    put(out, 0);
    out.write("fmt ", 4);
    put(out, 16);
    put(out, 3, 2);
    put(out, channels, 2);
    put(out, rate);
    put(out, rate * channels * 4);
    put(out, channels * 4, 2);
    put(out, 32, 2);
    out.write("data", 4);
    put(out, large ? UINT32_MAX : bytes);
    return out.str();
}
void seek(std::FILE *file, uint64_t at) {
#ifdef _WIN32
    const int rc = _fseeki64(file, int64_t(at), SEEK_SET);
#else
    const int rc = fseeko(file, off_t(at), SEEK_SET);
#endif
    if (rc != 0) {
        throw std::runtime_error("Recording seek failed");
    }
}
void sync(std::FILE *file) {
    if (std::fflush(file) != 0) {
        throw std::runtime_error("Recording flush failed");
    }
#ifdef _WIN32
    const int rc = _commit(_fileno(file));
#else
    const int rc = fsync(fileno(file));
#endif
    if (rc != 0) {
        throw std::runtime_error("Recording disk sync failed");
    }
}
void checkpoint(std::FILE *file, uint32_t rate, uint16_t channels, uint64_t frames, bool durable) {
    // Flush PCM before advertising it in the header.
    if (std::fflush(file) != 0) {
        throw std::runtime_error("Recording data flush failed");
    }
    seek(file, 0);
    const auto bytes = header(rate, channels, frames * channels * 4);
    if (std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
        throw std::runtime_error("Recording checkpoint failed");
    }
    if (durable) {
        sync(file);
    } else if (std::fflush(file) != 0) {
        throw std::runtime_error("Recording header flush failed");
    }
    seek(file, 80 + frames * channels * 4);
}
} // namespace
WavReader::WavReader(const std::filesystem::path &path) : file_(path, std::ios::binary) {
    auto exact = [&](unsigned char *p, size_t n) {
        if (!file_.read(reinterpret_cast<char *>(p), std::streamsize(n))) {
            throw std::runtime_error("Truncated WAV stream");
        }
    };
    std::array<unsigned char, 28> b{};
    exact(b.data(), 12);
    const bool rf64 = !std::memcmp(b.data(), "RF64", 4);
    const uint64_t fileBytes = std::filesystem::file_size(path);
    if ((!rf64 && std::memcmp(b.data(), "RIFF", 4)) || std::memcmp(b.data() + 8, "WAVE", 4)) {
        throw std::runtime_error("Streaming requires RIFF/RF64 WAVE");
    }
    uint64_t end = integer(b.data() + 4) + 8, largeData = 0, largeFrames = 0;
    bool ds64 = false;
    if (rf64) {
        if (integer(b.data() + 4) != UINT32_MAX) {
            throw std::runtime_error("Invalid RF64 sentinel");
        }
        exact(b.data(), 8);
        const auto length = integer(b.data() + 4);
        if (std::memcmp(b.data(), "ds64", 4) || length < 28 || length > fileBytes ||
            fileBytes < 20 + length) {
            throw std::runtime_error("Missing RF64 ds64");
        }
        exact(b.data(), 28);
        const auto riff = integer(b.data(), 8);
        largeData = integer(b.data() + 8, 8);
        largeFrames = integer(b.data() + 16, 8);
        // Multiple oversized chunks and ds64 tables are explicitly unsupported.
        if (riff > INT64_MAX - 8 || integer(b.data() + 24) != 0) {
            throw std::runtime_error("Unsupported RF64 table or size");
        }
        end = riff + 8;
    }
    if (end < 12 || end > fileBytes || end > INT64_MAX) {
        throw std::runtime_error("Invalid stream RIFF size");
    }
    bool fmt = false, data = false;
    for (uint64_t at = 12; at < end;) {
        if (end - at < 8) {
            throw std::runtime_error("Truncated stream chunk");
        }
        file_.seekg(std::streamoff(at));
        exact(b.data(), 8);
        uint64_t size = integer(b.data() + 4);
        if (rf64 && !std::memcmp(b.data(), "data", 4) && size == UINT32_MAX) {
            size = largeData;
        }
        if (size > end - at - 8 || (size & 1) > end - at - 8 - size) {
            throw std::runtime_error("Stream chunk exceeds RIFF");
        }
        uint64_t next = at + 8 + size + (size & 1);
        if (next > end) {
            throw std::runtime_error("Stream chunk exceeds RIFF");
        }
        if (!std::memcmp(b.data(), "ds64", 4)) {
            if (!rf64 || ds64 || at != 12) {
                throw std::runtime_error("Invalid duplicate ds64");
            }
            ds64 = true;
        } else if (!std::memcmp(b.data(), "fmt ", 4)) {
            if (fmt || size < 16) {
                throw std::runtime_error("Invalid stream format chunk");
            }
            exact(b.data(), 16);
            fmt = true;
            info_.format = uint16_t(integer(b.data(), 2));
            info_.channels = uint16_t(integer(b.data() + 2, 2));
            info_.sampleRate = uint32_t(integer(b.data() + 4));
            info_.bits = uint16_t(integer(b.data() + 14, 2));
            if (!info_.channels || info_.channels > 64 || info_.sampleRate < 8000 ||
                info_.sampleRate > 384000 ||
                !((info_.format == 1 &&
                   (info_.bits == 8 || info_.bits == 16 || info_.bits == 24 || info_.bits == 32)) ||
                  (info_.format == 3 && info_.bits == 32)) ||
                integer(b.data() + 12, 2) != uint32_t(info_.channels) * info_.bits / 8 ||
                integer(b.data() + 8) != uint64_t(info_.sampleRate) * info_.channels * info_.bits / 8) {
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
    if (rf64 && (!ds64 || info_.dataBytes != largeData || info_.frames != largeFrames)) {
        throw std::runtime_error("RF64 size/sample count mismatch");
    }
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
        auto value = uint32_t(integer(bytes.data() + i * width, width));
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
    : rate_(rate), channels_(channels) {
    if (rate < 8000 || rate > 384000 || !channels || channels > 64) {
        throw std::runtime_error("Invalid recording format");
    }
#ifdef _WIN32
    file_ = _wfopen(path.c_str(), L"wb");
#else
    file_ = std::fopen(path.c_str(), "wb");
#endif
    if (!file_) {
        throw std::runtime_error("Cannot create recording WAV");
    }
    try {
        bytes_.resize(65536 * channels * 4);
        checkpoint(file_, rate_, channels_, 0, true);
    } catch (...) {
        std::fclose(file_);
        file_ = nullptr;
        throw;
    }
}
WavWriter::~WavWriter() {
    if (file_) {
        std::fclose(file_);
    }
}
void WavWriter::append(std::span<const float> samples) {
    if (finished_ || samples.size() % channels_ || samples.size() > bytes_.size() / 4 ||
        frames_ > (uint64_t(INT64_MAX) - 80) / (channels_ * 4) - samples.size() / channels_) {
        throw std::runtime_error("Invalid recording append or file size limit");
    }
    for (size_t i = 0; i < samples.size(); ++i) {
        float f = samples[i];
        if (!std::isfinite(f)) {
            throw std::runtime_error("Non-finite recorded PCM");
        }
        auto bits = std::bit_cast<uint32_t>(std::clamp(f, -1.0f, 1.0f));
        for (size_t byte = 0; byte < 4; ++byte) {
            bytes_[i * 4 + byte] = uint8_t((bits >> (byte * 8)) & 255);
        }
    }
    if (std::fwrite(bytes_.data(), 4, samples.size(), file_) != samples.size()) {
        throw std::runtime_error("Recording write failed; partial preserved");
    }
    const auto previous = frames_;
    frames_ += samples.size() / channels_;
    if (frames_ / rate_ != previous / rate_) {
        checkpoint(file_, rate_, channels_, frames_, frames_ / (rate_ * 5) != previous / (rate_ * 5));
    }
}
void WavWriter::finish() {
    if (finished_) {
        return;
    }
    checkpoint(file_, rate_, channels_, frames_, true);
    auto *file = file_;
    file_ = nullptr;
    if (std::fclose(file) != 0) {
        throw std::runtime_error("Recording close failed");
    }
    finished_ = true;
}
uint64_t recoverRecording(const std::filesystem::path &partial, const std::filesystem::path &destination) {
    if (std::filesystem::exists(destination) || partial == destination) {
        throw std::runtime_error("Recovery destination must be a new file");
    }
    std::ifstream input(partial, std::ios::binary);
    std::array<unsigned char, 80> h{};
    if (!input.read(reinterpret_cast<char *>(h.data()), h.size()) ||
        (std::memcmp(h.data(), "RIFF", 4) && std::memcmp(h.data(), "RF64", 4)) ||
        std::memcmp(h.data() + 8, "WAVE", 4) ||
        (std::memcmp(h.data() + 12, "JUNK", 4) && std::memcmp(h.data() + 12, "ds64", 4)) ||
        integer(h.data() + 16) != 28 || std::memcmp(h.data() + 48, "fmt ", 4) ||
        integer(h.data() + 52) != 16 || integer(h.data() + 56, 2) != 3 || integer(h.data() + 70, 2) != 32 ||
        std::memcmp(h.data() + 72, "data", 4)) {
        throw std::runtime_error("Recovery accepts only Ally recording partials");
    }
    const auto channels = uint16_t(integer(h.data() + 58, 2));
    const auto rate = uint32_t(integer(h.data() + 60));
    if (!channels || channels > 64 || rate < 8000 || rate > 384000 ||
        integer(h.data() + 68, 2) != channels * 4 || integer(h.data() + 64) != rate * channels * 4) {
        throw std::runtime_error("Malformed recording partial format");
    }
    const auto bytes = std::filesystem::file_size(partial) - 80;
    const uint64_t frames = bytes / (channels * 4);
    // Read and validate PCM incrementally. A failed recovery retains both source and incomplete copy.
    WavWriter writer(destination, rate, channels);
    std::array<unsigned char, 65536> buffer{};
    std::vector<float> pcm((buffer.size() / (channels * 4)) * channels);
    uint64_t at = 0;
    while (at < frames) {
        const size_t count = size_t(std::min<uint64_t>(pcm.size() / channels, frames - at));
        if (!input.read(reinterpret_cast<char *>(buffer.data()), std::streamsize(count * channels * 4))) {
            throw std::runtime_error("Recording partial changed during recovery");
        }
        for (size_t i = 0; i < count * channels; ++i) {
            pcm[i] = std::bit_cast<float>(uint32_t(integer(buffer.data() + i * 4)));
        }
        writer.append(std::span(pcm).first(count * channels));
        at += count;
    }
    writer.finish();
    return frames;
}
} // namespace ally::media
