#include <algorithm>
#include <ally/audio/engine.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
static thread_local bool forbidNew = false;
void *operator new(std::size_t bytes) {
    if (forbidNew) {
        throw std::bad_alloc();
    }
    if (void *p = std::malloc(bytes ? bytes : 1)) {
        return p;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t bytes) {
    return ::operator new(bytes);
}

void operator delete(void *p) noexcept {
    std::free(p);
}

void operator delete[](void *p) noexcept {
    std::free(p);
}

void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}

void operator delete[](void *p, std::size_t) noexcept {
    std::free(p);
}
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            throw std::runtime_error(#x);                                                                    \
        }                                                                                                    \
    } while (false)
int main() {
    using namespace ally::audio;
    Engine engine;
    Plan p;
    p.loopFrames = 2048;
    p.count = 1;
    p.notes[0] = {128, 512, 69, 0, 1};
    CHECK(engine.submit(p));
    engine.play(true);
    std::array<float, 2048> b{};
    forbidNew = true;
    engine.process(b);
    forbidNew = false;
    CHECK(std::all_of(b.begin(), b.begin() + 258, [](float f) { return f == 0; }));
    CHECK(std::any_of(b.begin() + 260, b.begin() + 1280, [](float f) { return std::abs(f) > 0.01f; }));
    CHECK(std::all_of(b.begin() + 1280, b.end(), [](float f) { return f == 0; }));
    CHECK(engine.position() == 1024);
    CHECK(engine.trackPeak(0) > 0);
    CHECK(engine.trackPeak(0, 0) > engine.trackPeak(0, 1));
    CHECK(engine.peak(0) > engine.peak(1));
    CHECK(engine.peak(2) == 0 && engine.trackPeak(Plan::maxTracks, 0) == 0);
    engine.play(false);
    engine.process(b);
    CHECK(engine.position() == 1024);
    CHECK(std::all_of(b.begin(), b.end(), [](float f) { return f == 0; }));
    engine.rewind();
    engine.process(b);
    CHECK(engine.position() == 0);
    CHECK(engine.midi({60, 100}));
    engine.process(b);
    CHECK(engine.peak() > 0);
    CHECK(engine.midi({60, 0}));
    engine.process(b);
    CHECK(std::all_of(b.begin() + 480, b.end(), [](float f) { return f == 0; }));
    CHECK(engine.audition({60, 2, 100}));
    CHECK(engine.midi({60, 100}));
    forbidNew = true;
    engine.process(b);
    forbidNew = false;
    CHECK(engine.trackPeak(2) > 0 && engine.trackPeak(0) > 0);
    CHECK(engine.audition({60, 2, 0}));
    engine.process(b);
    engine.process(b);
    CHECK(engine.trackPeak(2) == 0 && engine.trackPeak(0) > 0);
    CHECK(engine.midi({60, 0}));
    engine.process(b);
    CHECK(!engine.audition({60, uint8_t(Plan::maxTracks), 100}));
    CHECK(!engine.audition({200, 0, 100}));
    {
        Engine poly;
        Plan chord;
        chord.count = 2;
        chord.notes[0] = {0, 2048, 69, 0, .75f};
        chord.notes[1] = chord.notes[0];
        CHECK(poly.submit(chord));
        poly.play(true);
        forbidNew = true;
        poly.process(b);
        forbidNew = false;
        float measured = 0;
        for (size_t i = 0; i < b.size(); i += 2) {
            measured = std::max(measured, std::abs(b[i]));
        }
        CHECK(std::abs(poly.trackPeak(0, 0) - measured) < 1e-6f);
    }
    p.notes[0].key = 200;
    CHECK(!engine.submit(p));
    p.notes[0].key = 69;
    p.loopFrames = 0;
    CHECK(!engine.submit(p));
    p.loopFrames = 2048;
    CHECK(engine.submit(p));
    CHECK(engine.submit(p));
    CHECK(engine.submit(p));
    CHECK(!engine.submit(p));
    engine.process(b);
    CHECK(engine.submit(p));
    {
        // PCM clips are sample bounded and real stereo, including seek and master gain.
        const std::array<float, 4> texture{.2f, .4f, .1f, .3f};
        Engine pcm(48000, texture);
        Plan clip;
        clip.loopFrames = 16;
        clip.loopEndFrame = 12;
        clip.masterGain = .5f;
        clip.gains[4] = 1;
        clip.sampleCount = 1;
        clip.samples[0] = {2, 4, 4};
        CHECK(pcm.submit(clip));
        pcm.play(true);
        std::array<float, 16> out{};
        forbidNew = true;
        pcm.process(out);
        forbidNew = false;
        CHECK(out[0] == 0 && out[2] == 0 && out[4] == .1f && out[5] == .2f);
        CHECK(out[10] == .05f && out[11] == .15f && out[12] == 0);
        CHECK(pcm.trackPeak(4, 1) == .4f && pcm.peak(1) == .2f);
        pcm.play(false);
        pcm.seek(3);
        pcm.process(out);
        CHECK(pcm.position() == 3);
        pcm.play(true);
        pcm.process(out);
        CHECK(out[0] == .05f && out[1] == .15f);
        pcm.rewind();
        pcm.looping(false);
        pcm.process(out);
        pcm.process(out);
        CHECK(!pcm.playing() && pcm.position() == 15);
        pcm.process(out);
        CHECK(std::all_of(out.begin(), out.end(), [](float f) { return f == 0; }));
        clip.samples[0].duration = 15;
        CHECK(!pcm.submit(clip));
        clip.samples[0].duration = 4;
        clip.masterGain = -1;
        CHECK(!pcm.submit(clip));
        bool rejected = false;
        try {
            Engine invalid(48000, std::span(texture).first(1));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        CHECK(rejected);
    }
    {
        Engine click;
        Plan tempo;
        tempo.loopFrames = 1000;
        tempo.clickCount = 1;
        tempo.clicks[0] = 0;
        CHECK(click.submit(tempo));
        click.metronome(true);
        click.play(true);
        forbidNew = true;
        click.process(b);
        forbidNew = false;
        CHECK(click.peak() > 0 && click.trackPeak(0) == 0);
        click.metronome(false);
        click.process(b);
        CHECK(click.peak() == 0);
    }
    {
        Engine bright, dark;
        Plan filtered;
        filtered.count = 1;
        filtered.notes[0] = {0, 4096, 69, 0, 1};
        CHECK(bright.submit(filtered));
        filtered.cutoffs[0] = 20;
        CHECK(dark.submit(filtered));
        bright.play(true);
        dark.play(true);
        std::array<float, 2048> other{};
        forbidNew = true;
        bright.process(b);
        dark.process(other);
        forbidNew = false;
        CHECK(dark.peak() < bright.peak() * .15f);
    }
    SpscQueue<uint64_t, 128> queue;
    std::jthread producer([&] {
        for (uint64_t i = 0; i < 100000; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });
    for (uint64_t i = 0; i < 100000; ++i) {
        uint64_t value = 0;
        while (!queue.pop(value)) {
            std::this_thread::yield();
        }
        CHECK(value == i);
    }
    std::cout << "sample boundaries, transport, live notes, validation and 100000 SPSC transfers passed\n";
}
