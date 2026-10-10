#pragma once
#include <ally/audio/engine.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
namespace ally::midi {
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

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ally::midi
