#pragma once
#include <memory>
#include <string>
namespace ally::audio {
class Engine;
class Backend {
  public:
    virtual ~Backend() = default;
    virtual bool start(Engine &engine, std::string &error) = 0;
    virtual void stop() noexcept = 0;
    virtual std::string name() const = 0;
};
std::unique_ptr<Backend> makeNativeBackend();
} // namespace ally::audio
