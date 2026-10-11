#include <algorithm>
#include <ally/audio/engine.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            throw std::runtime_error(#x);                                                                    \
        }                                                                                                    \
    } while (false)
using namespace ally::audio;
std::vector<float> sampleRender(Strip strip, Strip bus = {}, float busGain = 1) {
    Engine engine(8000);
    auto sample = std::make_shared<const std::vector<float>>(std::vector<float>(4096, .8f));
    Plan plan;
    plan.loopFrames = 4096;
    plan.masterGain = 1;
    plan.gains[0] = 1;
    plan.sourcesBound = true;
    plan.sources[0] = engine.prepareSample(sample);
    plan.strips[0] = strip;
    plan.buses[0] = bus;
    plan.busGains[0] = busGain;
    plan.sampleCount = 1;
    plan.samples[0] = {0, 2048, 0, 0, 0, false};
    CHECK(engine.submit(plan));
    engine.looping(false);
    engine.play(true);
    std::vector<float> output(4096);
    engine.process(output);
    return output;
}
int main() {
    Strip bypass;
    auto plain = sampleRender(bypass);
    CHECK(plain[1024] == .8f);
    Strip pan;
    pan.pan = 1;
    auto right = sampleRender(pan);
    CHECK(right[1024] == 0 && right[1025] == .8f);
    Strip routed;
    routed.route = 0;
    auto bus = sampleRender(routed, {}, .5f);
    CHECK(bus[1024] == .4f);
    routed.sends[0] = .25f;
    auto send = sampleRender(routed, {}, .5f);
    CHECK(std::abs(send[1024] - .5f) < 1e-6f);
    Strip eq;
    eq.lowDb = -6;
    auto filtered = sampleRender(eq);
    CHECK(filtered[2048] < .41f && filtered[2048] > .39f);
    Strip compressor;
    compressor.thresholdDb = -24;
    compressor.ratio = 4;
    auto compressed = sampleRender(compressor);
    CHECK(compressed[2048] < .15f && compressed[2048] > .05f);
    {
        Engine engine(8000);
        Plan plan;
        plan.loopFrames = 1024;
        plan.sourcesBound = true;
        plan.gains[0] = 1;
        auto sample = std::make_shared<const std::vector<float>>(std::vector<float>{.5f, .5f, 0, 0});
        plan.sources[0] = engine.prepareSample(sample);
        plan.sampleCount = 1;
        plan.samples[0] = {0, 2, 0, 0, 0, false};
        plan.strips[0].route = 0;
        plan.buses[0].delayWet = 1;
        plan.buses[0].delaySeconds = .01f;
        plan.buses[0].feedback = 0;
        CHECK(engine.submit(plan));
        engine.play(true);
        std::array<float, 512> output{};
        engine.process(output);
        CHECK(output[0] == .5f && output[160] == .5f && output[158] == 0 && output[162] == 0);
        engine.rewind();
        engine.process(std::span(output).first(40));
        engine.seek(16);
        engine.process(output);
        CHECK(std::all_of(output.begin(), output.end(), [](float v) { return v == 0; }));
        plan.buses[1].route = 0;
        CHECK(!engine.submit(plan));
    }
    {
        Engine engine(8000);
        Plan plan;
        plan.loopFrames = 2048;
        plan.gains[0] = 1;
        CHECK(engine.submit(plan));
        CHECK(engine.midi({69, 127, 0, 128}));
        std::array<float, 512> output{};
        engine.process(output);
        CHECK(std::all_of(output.begin(), output.begin() + 258, [](float v) { return v == 0; }));
        CHECK(std::any_of(output.begin() + 260, output.end(), [](float v) { return v != 0; }));
        CHECK(engine.audioClock() == 256 && engine.position() == 0);
        CHECK(engine.midi({69, 0, 0, 300}));
        engine.process(output);
        CHECK(engine.audioClock() == 512);
    }
    {
        Engine engine(8000);
        Plan plan;
        plan.loopFrames = 2048;
        plan.gains[0] = 1;
        plan.sourcesBound = true;
        auto sample = std::make_shared<const std::vector<float>>(std::vector<float>(512, .5f));
        plan.sources[0] = engine.prepareSample(sample);
        plan.instruments[0] = Instrument::Drum;
        plan.notes[0] = {0, 1, 36, 0, 1};
        plan.count = 1;
        CHECK(engine.submit(plan));
        engine.play(true);
        std::array<float, 1024> out{};
        engine.process(out);
        CHECK(out[200] > .1f && out[512] == 0); // One-shot ignores note's short gate.
        plan.instruments[0] = Instrument::Sampler;
        plan.roots[0] = 60;
        plan.notes[0] = {0, 512, 72, 0, 1};
        CHECK(engine.submit(plan));
        engine.rewind();
        engine.process(out);
        CHECK(out[200] > 0 && out[260] == 0); // Octave plays at twice the rate.
    }
    std::cout << "Prepared graph routing/sends, EQ, compressor, delay, pan, timed MIDI, sampler and drum "
                 "one-shots passed\n";
}
