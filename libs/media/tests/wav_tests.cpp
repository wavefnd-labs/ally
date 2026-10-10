#include <algorithm>
#include <ally/media/wav.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            throw std::runtime_error(#x);                                                                    \
        }                                                                                                    \
    } while (false)
int main() {
    using namespace ally::media;
    Pcm p{48000, 2, {-1, 1, 0, 0.5f, -0.5f, 0.25f}};
    auto bytes = encodeWav(p);
    auto q = decodeWav(bytes);
    CHECK(q.sampleRate == 48000 && q.channels == 2 && q.frames() == 3);
    for (size_t i = 0; i < p.samples.size(); ++i) {
        CHECK(std::abs(p.samples[i] - q.samples[i]) < 0.00004f);
    }
    auto rejects = [](auto &&b) {
        try {
            decodeWav(b);
        } catch (const std::runtime_error &) {
            return true;
        }
        return false;
    };
    for (size_t i = 0; i < bytes.size(); ++i) {
        CHECK(rejects(std::span(bytes).first(i)));
    }
    auto corrupt = bytes;
    corrupt[40] = std::byte{255};
    CHECK(rejects(corrupt));
    corrupt = bytes;
    corrupt[32] = std::byte{3};
    CHECK(rejects(corrupt));
    // Unknown odd-sized chunk with correct padding is safely skipped.
    std::vector<std::byte> extra = bytes;
    const std::array<std::byte, 10> chunk{std::byte{'J'}, std::byte{'U'}, std::byte{'N'}, std::byte{'K'},
                                          std::byte{1},   std::byte{0},   std::byte{0},   std::byte{0},
                                          std::byte{42},  std::byte{0}};
    extra.insert(extra.begin() + 12, chunk.begin(), chunk.end());
    extra[4] = std::byte(extra.size() - 8);
    CHECK(decodeWav(extra).frames() == 3);
    std::mt19937 random(1234);
    for (int i = 0; i < 4000; ++i) {
        std::vector<std::byte> fuzz(random() % 512);
        for (auto &byte : fuzz) {
            byte = std::byte(random() & 255);
        }
        try {
            decodeWav(fuzz);
        } catch (const std::runtime_error &) {
        }
    }
    // Build individual sample formats using a known RIFF header, avoiding a shared decode implementation.
    auto sampleFile = [&](uint16_t format, uint16_t bits, uint32_t sample) {
        auto file = encodeWav(Pcm{48000, 1, {0}});
        const size_t width = bits / 8;
        file.resize(44 + width + (width & 1));
        auto set = [&](size_t at, uint32_t value, size_t count) {
            for (size_t j = 0; j < count; ++j) {
                file[at + j] = std::byte((value >> (j * 8)) & 255);
            }
        };
        set(4, uint32_t(file.size() - 8), 4);
        set(20, format, 2);
        set(28, uint32_t(48000 * width), 4);
        set(32, uint32_t(width), 2);
        set(34, bits, 2);
        set(40, uint32_t(width), 4);
        set(44, sample, width);
        return file;
    };
    CHECK(decodeWav(sampleFile(1, 8, 0)).samples[0] == -1);
    CHECK(decodeWav(sampleFile(1, 24, 0x800000)).samples[0] == -1);
    CHECK(decodeWav(sampleFile(1, 32, 0x80000000)).samples[0] == -1);
    CHECK(decodeWav(sampleFile(3, 32, std::bit_cast<uint32_t>(0.25f))).samples[0] == 0.25f);
    CHECK(rejects(sampleFile(3, 32, 0x7fc00000))); // NaN.
    std::cout << "PCM roundtrip, all truncations, corrupt chunks, padding and 4000 deterministic malformed "
                 "inputs passed\n";
}
