#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace ally::audio {
class Engine;
struct Device {
    std::string id, name;
};
struct BackendConfig {
    std::string deviceId = "default";
    uint32_t bufferFrames = 256;
};
struct BackendStats {
    uint32_t sampleRate = 0, bufferFrames = 0;
    double outputLatencyMs = 0;
    uint64_t processedFrames = 0, underruns = 0, maxProcessNs = 0;
    int64_t errorCode = 0;
    bool active = false;
    double inputLatencyMs = 0;     // Capture estimate; outputLatencyMs is zero for capture backends.
    uint32_t realtimePriority = 0; // Actual native thread priority; zero means not realtime/unreported.
};
class Backend {
  public:
    virtual ~Backend() = default;
    virtual std::vector<Device> devices(std::string &error) = 0;
    virtual bool start(Engine &engine, const BackendConfig &config, std::string &error) = 0;
    bool start(Engine &engine, std::string &error) {
        return start(engine, {}, error);
    }
    virtual void stop() noexcept = 0;
    virtual std::string name() const = 0;
    virtual BackendStats stats() const noexcept = 0;
};
std::unique_ptr<Backend> makeNativeBackend();
} // namespace ally::audio
