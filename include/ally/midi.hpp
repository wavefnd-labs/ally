#pragma once
#include <ally/audio/engine.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
namespace ally::midi {
struct Port {
    size_t index = 0;
    std::string name;
};
struct Message {
    uint64_t sampleTime = 0;
    uint8_t status = 0, data1 = 0, data2 = 0;
};
std::optional<Message> parse(std::span<const uint8_t> bytes, uint64_t sampleTime) noexcept;
class Input {
  public:
    explicit Input(audio::Engine &engine);
    ~Input();
    std::string connectFirst();
    std::vector<Port> ports();
    std::string connect(size_t index);
    void route(uint8_t track) noexcept;
    bool poll(Message &message) noexcept;
    uint64_t dropped() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ally::midi
