#include <ally/midi.hpp>
#include <atomic>
#include <libremidi/libremidi.hpp>
namespace ally::midi {
struct Input::Impl {
    audio::Engine &engine;
    std::unique_ptr<libremidi::observer> observer;
    std::unique_ptr<libremidi::midi_in> input;
    std::atomic<uint64_t> dropped{0};
    explicit Impl(audio::Engine &e) : engine(e) {}
};
Input::Input(audio::Engine &engine) : impl_(std::make_unique<Impl>(engine)) {}
Input::~Input() = default;
std::string Input::connectFirst() {
    try {
        impl_->input.reset();
        // Choose the desktop backend explicitly: default auto-fallback can enumerate
        // ALSA Raw devices through a noexcept API when no sound cards are present.
        impl_->observer = std::make_unique<libremidi::observer>(
            libremidi::observer_configuration{},
            libremidi::observer_configuration_for(libremidi::midi1::default_api()));
        auto ports = impl_->observer->get_input_ports();
        if (ports.empty()) {
            return "MIDI: no input device";
        }
        libremidi::input_configuration config;
        config.on_message = [this](const libremidi::message &message) {
            auto m = parse({message.bytes.data(), message.bytes.size()}, impl_->engine.position());
            if (!m) {
                return;
            }
            auto type = m->status & 0xf0;
            if (type == 0x90 || type == 0x80) {
                if (!impl_->engine.midi({m->data1, uint8_t(type == 0x80 ? 0 : m->data2)})) {
                    impl_->dropped.fetch_add(1);
                }
            }
        };
        impl_->input = std::make_unique<libremidi::midi_in>(
            config, libremidi::midi_in_configuration_for(*impl_->observer));
        impl_->input->open_port(ports.front());
        if (!impl_->input->is_port_connected()) {
            return "MIDI: connection failed";
        }
        return "MIDI: " + ports.front().display_name;
    } catch (const std::exception &e) {
        return "MIDI unavailable: " + std::string(e.what());
    }
}
} // namespace ally::midi
