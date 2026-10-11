#include <algorithm>
#include <ally/audio/resample.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            throw std::runtime_error(#x);                                                                    \
        }                                                                                                    \
    } while (false)
std::vector<float> convert(ally::audio::StreamingResampler &converter, const std::vector<float> &input) {
    std::vector<float> output;
    std::array<float, 222> block{};
    size_t read = 0;
    while (read < input.size() / 2) {
        const auto count = std::min<size_t>(137, input.size() / 2 - read);
        auto result = converter.process(std::span(input).subspan(read * 2, count * 2), block);
        CHECK(result.inputFrames || result.outputFrames);
        read += result.inputFrames;
        output.insert(output.end(), block.begin(), block.begin() + result.outputFrames * 2);
    }
    CHECK(converter.bufferedFrames() >= 0);
    while (true) {
        auto result = converter.process({}, block, true);
        CHECK(result.inputFrames == 0);
        if (!result.outputFrames) {
            break;
        }
        output.insert(output.end(), block.begin(), block.begin() + result.outputFrames * 2);
    }
    return output;
}
int main() {
    for (uint32_t rate : {8000u, 44100u, 48000u, 96000u, 192000u, 384000u}) {
        std::vector<float> input(rate / 3 * 2);
        for (size_t i = 0; i < input.size() / 2; ++i) {
            input[i * 2] = float(.3 * std::sin(2 * std::numbers::pi * 1000 * double(i) / rate));
            input[i * 2 + 1] = float(.2 * std::sin(2 * std::numbers::pi * 1700 * double(i) / rate));
        }
        ally::audio::StreamingResampler converter(rate, 48000);
        auto actual = convert(converter, input);
        auto expected = ally::audio::resampleStereo(input, rate, 48000);
        CHECK(actual.size() == expected.size());
        double error = 0;
        for (size_t i = 0; i < actual.size(); ++i) {
            error = std::max(error, std::abs(double(actual[i]) - expected[i]));
        }
        CHECK(error < 2e-6);
        converter.reset();
        CHECK(convert(converter, input) == actual);
        std::cout << rate << " -> 48000: " << actual.size() / 2 << " frames, maximum chunk error=" << error
                  << '\n';
    }
    std::vector<float> tone(96000 * 2);
    for (size_t i = 0; i < tone.size() / 2; ++i) {
        tone[i * 2] = tone[i * 2 + 1] =
            float(.5 * std::sin(2 * std::numbers::pi * 32000 * double(i) / 96000));
    }
    ally::audio::StreamingResampler converter(96000, 48000);
    auto filtered = convert(converter, tone);
    double rms = 0;
    for (size_t i = 8192; i < filtered.size() - 8192; ++i) {
        rms += double(filtered[i]) * filtered[i];
    }
    rms = std::sqrt(rms / double(filtered.size() - 16384));
    CHECK(rms < 1e-5);
    std::cout << "32 kHz stopband at 96 -> 48 kHz: RMS=" << rms << " / reset and EOF flushing passed\n";
}
