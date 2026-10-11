#include <algorithm>
#include <ally/audio/resample.hpp>
#include <cmath>
#include <soxr.h>
#include <stdexcept>
namespace ally::audio {
std::vector<float> resampleStereo(std::span<const float> input, uint32_t from, uint32_t to) {
    if (input.empty() || input.size() % 2 || from < 8000 || from > 384000 || to < 8000 || to > 384000 ||
        !std::all_of(input.begin(), input.end(), [](float v) { return std::isfinite(v); })) {
        throw std::runtime_error("Invalid resampler input");
    }
    const double frames = std::ceil(double(input.size() / 2) * to / from);
    if (frames > 8 * 1024 * 1024) {
        throw std::runtime_error("Prepared audio exceeds 64 MiB source limit");
    }
    if (from == to) {
        return {input.begin(), input.end()};
    }
    std::vector<float> output((size_t(frames) + 32) * 2);
    size_t read = 0, written = 0;
    auto io = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
    auto quality = soxr_quality_spec(SOXR_HQ, 0);
    const auto error = soxr_oneshot(from, to, 2, input.data(), input.size() / 2, &read, output.data(),
                                    output.size() / 2, &written, &io, &quality, nullptr);
    if (error || read != input.size() / 2 || !written) {
        throw std::runtime_error(error ? error : "Incomplete resampler conversion");
    }
    output.resize(written * 2);
    for (auto &v : output) {
        v = std::clamp(v, -1.0f, 1.0f);
    }
    return output;
}
} // namespace ally::audio
