#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <ally/audio/backend.hpp>
#include <ally/audio/engine.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
namespace ally::audio {
namespace {
AudioObjectPropertyAddress address(AudioObjectPropertySelector selector,
                                   AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return {selector, scope, kAudioObjectPropertyElementMain};
}
template <class T>
OSStatus property(AudioObjectID id, AudioObjectPropertySelector selector, T &value,
                  AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    auto key = address(selector, scope);
    UInt32 size = sizeof(T);
    return AudioObjectGetPropertyData(id, &key, 0, nullptr, &size, &value);
}
std::string stringProperty(AudioObjectID id, AudioObjectPropertySelector selector) {
    CFStringRef value = nullptr;
    if (property(id, selector, value) != noErr || !value) {
        return {};
    }
    std::vector<char> bytes(
        size_t(CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8)) + 1);
    const bool ok = CFStringGetCString(value, bytes.data(), CFIndex(bytes.size()), kCFStringEncodingUTF8);
    CFRelease(value);
    return ok ? std::string(bytes.data()) : std::string{};
}
std::vector<AudioDeviceID> outputDevices() {
    auto key = address(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &key, 0, nullptr, &size) != noErr) {
        return {};
    }
    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &key, 0, nullptr, &size, ids.data()) != noErr) {
        return {};
    }
    std::erase_if(ids, [](AudioDeviceID id) {
        auto key = address(kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput);
        UInt32 bytes = 0;
        if (AudioObjectGetPropertyDataSize(id, &key, 0, nullptr, &bytes) != noErr ||
            bytes < sizeof(AudioBufferList)) {
            return true;
        }
        std::vector<std::byte> storage(bytes);
        if (AudioObjectGetPropertyData(id, &key, 0, nullptr, &bytes, storage.data()) != noErr) {
            return true;
        }
        const auto *list = reinterpret_cast<const AudioBufferList *>(storage.data());
        for (UInt32 i = 0; i < list->mNumberBuffers; ++i) {
            if (list->mBuffers[i].mNumberChannels) {
                return false;
            }
        }
        return true;
    });
    return ids;
}
} // namespace
class CoreAudioBackend final : public Backend {
  public:
    ~CoreAudioBackend() override {
        stop();
    }
    std::string name() const override {
        return "Core Audio HAL / stereo float32 / 48 kHz";
    }
    std::vector<Device> devices(std::string &error) override {
        std::vector<Device> result{{"default", "System default output"}};
        for (auto id : outputDevices()) {
            auto uid = stringProperty(id, kAudioDevicePropertyDeviceUID);
            if (!uid.empty()) {
                result.push_back({uid, stringProperty(id, kAudioObjectPropertyName)});
            }
        }
        if (result.size() == 1) {
            error = "No Core Audio output device enumerated";
        }
        return result;
    }
    bool start(Engine &engine, const BackendConfig &config, std::string &error) override {
        stop();
        if (config.bufferFrames < 32 || config.bufferFrames > 2048) {
            error = "Invalid Core Audio buffer";
            return false;
        }
        OSStatus code = noErr;
        if (config.deviceId == "default") {
            code = property(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, device_);
        } else {
            device_ = kAudioObjectUnknown;
            for (auto id : outputDevices()) {
                if (stringProperty(id, kAudioDevicePropertyDeviceUID) == config.deviceId) {
                    device_ = id;
                    break;
                }
            }
        }
        if (code != noErr || device_ == kAudioObjectUnknown) {
            error = "Core Audio output device not found";
            return false;
        }
        auto fail = [&](OSStatus value) {
            error = "Core Audio OSStatus " + std::to_string(value);
            error_ = value;
            stop();
            return false;
        };
        AudioComponentDescription description{};
        description.componentType = kAudioUnitType_Output;
        description.componentSubType = kAudioUnitSubType_HALOutput;
        description.componentManufacturer = kAudioUnitManufacturer_Apple;
        auto component = AudioComponentFindNext(nullptr, &description);
        if (!component) {
            return fail(kAudio_ParamError);
        }
        code = AudioComponentInstanceNew(component, &unit_);
        if (code != noErr) {
            return fail(code);
        }
        code = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                    &device_, sizeof(device_));
        if (code != noErr) {
            return fail(code);
        }
        AudioStreamBasicDescription format{};
        format.mSampleRate = engine.sampleRate();
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags =
            kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
        format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float) * 2;
        format.mFramesPerPacket = 1;
        format.mChannelsPerFrame = 2;
        format.mBitsPerChannel = 32;
        code = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format,
                                    sizeof(format));
        if (code != noErr) {
            return fail(code);
        }
        UInt32 requested = config.bufferFrames;
        AudioValueRange range{};
        if (property(device_, kAudioDevicePropertyBufferFrameSizeRange, range) == noErr) {
            requested = UInt32(std::clamp(double(requested), range.mMinimum, range.mMaximum));
        }
        auto bufferKey = address(kAudioDevicePropertyBufferFrameSize);
        code = AudioObjectSetPropertyData(device_, &bufferKey, 0, nullptr, sizeof(requested), &requested);
        if (code != noErr) {
            return fail(code);
        }
        property(device_, kAudioDevicePropertyBufferFrameSize, buffer_);
        Float64 hardwareRate = engine.sampleRate();
        property(device_, kAudioDevicePropertyNominalSampleRate, hardwareRate);
        UInt32 latency = 0, safety = 0;
        property(device_, kAudioDevicePropertyLatency, latency, kAudioDevicePropertyScopeOutput);
        property(device_, kAudioDevicePropertySafetyOffset, safety, kAudioDevicePropertyScopeOutput);
        rate_ = engine.sampleRate();
        latencyMs_ = double(latency + safety + buffer_) * 1000 / hardwareRate;
        UInt32 maximum = std::max<UInt32>(4096, buffer_ * 4);
        code = AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global,
                                    0, &maximum, sizeof(maximum));
        if (code != noErr) {
            return fail(code);
        }
        AURenderCallbackStruct callback{render, this};
        code = AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                                    &callback, sizeof(callback));
        if (code != noErr) {
            return fail(code);
        }
        code = AudioUnitInitialize(unit_);
        if (code != noErr) {
            return fail(code);
        }
        engine_ = &engine;
        frames_ = xruns_ = maxNs_ = 0;
        error_ = 0;
        active_ = true;
        auto aliveKey = address(kAudioDevicePropertyDeviceIsAlive);
        listening_ = AudioObjectAddPropertyListener(device_, &aliveKey, alive, this) == noErr;
        auto overloadKey = address(kAudioDeviceProcessorOverload);
        overloadListening_ = AudioObjectAddPropertyListener(device_, &overloadKey, overload, this) == noErr;
        code = AudioOutputUnitStart(unit_);
        if (code != noErr) {
            return fail(code);
        }
        return true;
    }
    void stop() noexcept override {
        active_ = false;
        if (unit_) {
            AudioOutputUnitStop(unit_);
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
        }
        if (listening_) {
            auto key = address(kAudioDevicePropertyDeviceIsAlive);
            AudioObjectRemovePropertyListener(device_, &key, alive, this);
            listening_ = false;
        }
        if (overloadListening_) {
            auto key = address(kAudioDeviceProcessorOverload);
            AudioObjectRemovePropertyListener(device_, &key, overload, this);
            overloadListening_ = false;
        }
        engine_ = nullptr;
    }
    BackendStats stats() const noexcept override {
        return {rate_,         buffer_,       latencyMs_,    frames_.load(),
                xruns_.load(), maxNs_.load(), error_.load(), active_.load()};
    }

  private:
    static OSStatus overload(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *user) {
        static_cast<CoreAudioBackend *>(user)->xruns_.fetch_add(1);
        return noErr;
    }
    static OSStatus alive(AudioObjectID id, UInt32, const AudioObjectPropertyAddress *, void *user) {
        auto &self = *static_cast<CoreAudioBackend *>(user);
        UInt32 state = 0;
        if (property(id, kAudioDevicePropertyDeviceIsAlive, state) != noErr || !state) {
            self.error_ = kAudioHardwareBadDeviceError;
            self.active_ = false;
        }
        return noErr;
    }
    static OSStatus render(void *user, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32,
                           UInt32 frames, AudioBufferList *data) {
        auto &self = *static_cast<CoreAudioBackend *>(user);
        if (!data) {
            return kAudio_ParamError;
        }
        if (!self.active_.load() || data->mNumberBuffers != 1 || data->mBuffers[0].mNumberChannels != 2 ||
            !data->mBuffers[0].mData ||
            data->mBuffers[0].mDataByteSize < size_t(frames) * 2 * sizeof(float)) {
            for (UInt32 i = 0; i < data->mNumberBuffers; ++i) {
                if (data->mBuffers[i].mData) {
                    std::memset(data->mBuffers[i].mData, 0, data->mBuffers[i].mDataByteSize);
                }
            }
            if (self.active_.load()) {
                self.error_ = kAudio_ParamError;
                self.active_ = false;
            }
            return noErr;
        }
        const auto begin = std::chrono::steady_clock::now();
        self.engine_->process({static_cast<float *>(data->mBuffers[0].mData), size_t(frames) * 2});
        const auto ns = uint64_t(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin)
                .count());
        self.maxNs_.store(std::max(self.maxNs_.load(), ns));
        self.frames_.fetch_add(frames);
        return noErr;
    }
    AudioDeviceID device_ = kAudioObjectUnknown;
    AudioUnit unit_ = nullptr;
    Engine *engine_ = nullptr;
    uint32_t rate_ = 0, buffer_ = 0;
    double latencyMs_ = 0;
    bool listening_ = false, overloadListening_ = false;
    std::atomic<bool> active_{false};
    std::atomic<uint64_t> frames_{0}, xruns_{0}, maxNs_{0};
    std::atomic<int64_t> error_{0};
};
std::unique_ptr<Backend> makeNativeBackend() {
    return std::make_unique<CoreAudioBackend>();
}
} // namespace ally::audio
