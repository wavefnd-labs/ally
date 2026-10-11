#include <algorithm>
#include <ally/media/stream.hpp>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            throw std::runtime_error(#x);                                                                    \
        }                                                                                                    \
    } while (false)
void verify(const std::filesystem::path &root) {
    using namespace ally::media;
    const auto partial = root / "take.partial.wav";
    std::array<float, 512> pcm{};
    for (size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = i % 2 ? -.3f : .2f;
    }
    {
        WavWriter writer(partial, 48000, 2);
        writer.append(pcm);
        // Deliberately leave the header's frame count at zero, simulating interruption before a checkpoint.
    }
    {
        std::ofstream tail(partial, std::ios::binary | std::ios::app);
        tail.put('x');
    }
    CHECK(recoverRecording(partial, root / "recovered.wav") == 256);
    CHECK(std::filesystem::file_size(partial) == 80 + 2048 + 1);
    {
        WavReader reader(root / "recovered.wav");
        CHECK(reader.info().frames == 256 && reader.info().dataOffset == 80);
        CHECK(reader.read(255, 256).samples[1] == -.3f);
    }
    CHECK(readWav(root / "recovered.wav").samples[0] == .2f);
    bool existing = false;
    try {
        recoverRecording(partial, root / "recovered.wav");
    } catch (...) {
        existing = true;
    }
    CHECK(existing);
    // Small RF64 fixture: independent ds64 construction tests both bounded and in-memory parsers.
    std::ifstream source(root / "recovered.wav", std::ios::binary);
    std::vector<unsigned char> bytes(80 + 2048);
    CHECK(bool(source.read(reinterpret_cast<char *>(bytes.data()), bytes.size())));
    source.close();
    auto set = [&](size_t at, uint64_t value, size_t width) {
        for (size_t i = 0; i < width; ++i) {
            bytes[at + i] = uint8_t(value >> (i * 8));
        }
    };
    std::copy_n("RF64", 4, bytes.begin());
    std::copy_n("ds64", 4, bytes.begin() + 12);
    set(4, UINT32_MAX, 4);
    set(20, bytes.size() - 8, 8);
    set(28, 2048, 8);
    set(36, 256, 8);
    set(76, UINT32_MAX, 4);
    auto write = [&](const std::filesystem::path &path) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    };
    write(root / "small-rf64.wav");
    {
        WavReader rf64(root / "small-rf64.wav");
        CHECK(rf64.info().frames == 256);
    }
    CHECK(readWav(root / "small-rf64.wav").samples[511] == -.3f);
    set(36, 257, 8);
    write(root / "bad-rf64.wav");
    bool invalid = false;
    try {
        WavReader bad(root / "bad-rf64.wav");
    } catch (...) {
        invalid = true;
    }
    CHECK(invalid);
    invalid = false;
    try {
        readWav(root / "bad-rf64.wav");
    } catch (...) {
        invalid = true;
    }
    CHECK(invalid);
    set(36, 256, 8);
    set(28, UINT64_MAX, 8);
    write(root / "overflow-rf64.wav");
    invalid = false;
    try {
        WavReader bad(root / "overflow-rf64.wav");
    } catch (...) {
        invalid = true;
    }
    CHECK(invalid);
    std::cout
        << "Checkpoint recovery, partial frame trim, source preservation, RF64 parser and bounds passed\n";
}
int main(int argc, char **argv) {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("ally-media-stream-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directory(root);
        verify(root);
        if (argc > 1 && std::string(argv[1]) == "--rf64-write") {
            // Opt-in full write across 4 GiB; no sparse shortcut. Deleted after verification.
            const uint64_t count = uint64_t(UINT32_MAX) / 8 + 65536;
            {
                ally::media::WavWriter writer(root / "large.wav", 384000, 2);
                std::vector<float> block(65536 * 2, .125f);
                for (uint64_t at = 0; at < count; at += 65536) {
                    writer.append(std::span(block).first(size_t(std::min<uint64_t>(65536, count - at)) * 2));
                }
                writer.finish();
            }
            {
                ally::media::WavReader reader(root / "large.wav");
                CHECK(reader.info().frames == count && reader.info().dataBytes > UINT32_MAX);
                CHECK(reader.read(count - 1, 1).samples[1] == .125f);
            }
            std::ifstream file(root / "large.wav", std::ios::binary);
            std::array<char, 4> tag{};
            file.read(tag.data(), 4);
            CHECK(std::string(tag.data(), 4) == "RF64");
            std::cout << "Actual RF64 write/read passed: " << count << " frames, "
                      << std::filesystem::file_size(root / "large.wav") << " bytes\n";
        }
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " (fixture " << root << ")\n";
        return 1;
    }
}
