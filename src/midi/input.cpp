#include <ally/midi.hpp>
#include <atomic>
#include <libremidi/libremidi.hpp>
namespace ally::midi {
struct Input::Impl {
    audio::Engine &engine;
    std::unique_ptr<libremidi::observer> observer;
    std::unique_ptr<libremidi::midi_in> input;
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint8_t> track{0};
    audio::SpscQueue<Message, 1024> captured;
    std::array<std::array<int, 128>, 16> held{};
    explicit Impl(audio::Engine &e) : engine(e) {
        for (auto &channel : held) {
            channel.fill(-1);
        }
    }
};
Input::Input(audio::Engine &engine) : impl_(std::make_unique<Impl>(engine)) {}
Input::~Input() = default;
std::string Input::connectFirst() {
    return connect(0);
}
std::vector<Port> Input::ports() {
    if (!impl_->observer) {
        impl_->observer = std::make_unique<libremidi::observer>(
            libremidi::observer_configuration{},
            libremidi::observer_configuration_for(libremidi::midi1::default_api()));
    }
    std::vector<Port> result;
    const auto ports = impl_->observer->get_input_ports();
    for (size_t i = 0; i < ports.size(); ++i) {
        result.push_back({i, ports[i].display_name});
    }
    return result;
}
void Input::route(uint8_t track) noexcept {
    impl_->track.store(track);
}
bool Input::poll(Message &message) noexcept {
    return impl_->captured.pop(message);
}
uint64_t Input::dropped() const noexcept {
    return impl_->dropped.load();
}
std::string Input::connect(size_t index) {
    try {
        impl_->input.reset();
        for (auto &channel : impl_->held) {
            for (size_t key = 0; key < channel.size(); ++key) {
                if (channel[key] >= 0) {
                    impl_->engine.midi({uint8_t(key), 0, uint8_t(channel[key]), 0});
                    channel[key] = -1;
                }
            }
        }
        // Choose the desktop backend explicitly: default auto-fallback can enumerate
        // ALSA Raw devices through a noexcept API when no sound cards are present.
        impl_->observer = std::make_unique<libremidi::observer>(
            libremidi::observer_configuration{},
            libremidi::observer_configuration_for(libremidi::midi1::default_api()));
        auto ports = impl_->observer->get_input_ports();
        if (index >= ports.size()) {
            return "MIDI: no input device";
        }
        libremidi::input_configuration config;
        config.on_message = [this](const libremidi::message &message) {
            auto m = parse({message.bytes.data(), message.bytes.size()}, impl_->engine.timestamp());
            if (!m) {
                return;
            }
            auto type = m->status & 0xf0;
            if (type == 0x90 || type == 0x80) {
                auto &held = impl_->held[m->status & 15][m->data1];
                uint8_t track = type == 0x80 && held >= 0 ? uint8_t(held) : impl_->track.load();
                held = type == 0x80 ? -1 : track;
                if (!impl_->engine.midi(
                        {m->data1, uint8_t(type == 0x80 ? 0 : m->data2), track, m->sampleTime})) {
                    impl_->dropped.fetch_add(1);
                }
            }
            if (!impl_->captured.push(*m)) {
                impl_->dropped.fetch_add(1);
            }
        };
        impl_->input = std::make_unique<libremidi::midi_in>(
            config, libremidi::midi_in_configuration_for(*impl_->observer));
        impl_->input->open_port(ports[index]);
        if (!impl_->input->is_port_connected()) {
            return "MIDI: connection failed";
        }
        return "MIDI: " + ports[index].display_name;
    } catch (const std::exception &e) {
        return "MIDI unavailable: " + std::string(e.what());
    }
}
} // namespace ally::midi
