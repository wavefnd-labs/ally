#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <ally/audio/capture.hpp>
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
std::vector<AudioDeviceID> inputDevices() {
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
        auto key = address(kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeInput);
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
class CoreAudioCapture final : public Capture {
  public:
    ~CoreAudioCapture() override {
        stop();
    }
    std::vector<Device> devices(std::string &error) override {
        std::vector<Device> result{{"default", "System default input"}};
        for (auto id : inputDevices()) {
            auto uid = stringProperty(id, kAudioDevicePropertyDeviceUID);
            if (!uid.empty()) {
                result.push_back({uid, stringProperty(id, kAudioObjectPropertyName)});
            }
        }
        if (result.size() == 1) {
            error = "No Core Audio input device enumerated";
        }
        return result;
    }
    bool start(CaptureSink &sink, const CaptureConfig &config, std::string &error) override {
        stop();
        if (config.bufferFrames < 32 || config.bufferFrames > 2048 || config.sampleRate != 48000 ||
            !config.channels || config.channels > 8 || config.deviceId.empty()) {
            error = "Invalid Core Audio buffer";
            return false;
        }
        OSStatus code = noErr;
        if (config.deviceId == "default") {
            code = property(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice, device_);
        } else {
            device_ = kAudioObjectUnknown;
            for (auto id : inputDevices()) {
                if (stringProperty(id, kAudioDevicePropertyDeviceUID) == config.deviceId) {
                    device_ = id;
                    break;
                }
            }
        }
        if (code != noErr || device_ == kAudioObjectUnknown) {
            error = "Core Audio input device not found";
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
        UInt32 enabled = 1, disabled = 0;
        code = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1,
                                    &enabled, sizeof(enabled));
        if (code != noErr) {
            return fail(code);
        }
        code = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0,
                                    &disabled, sizeof(disabled));
        if (code != noErr) {
            return fail(code);
        }
        Float64 inputRate = 0;
        property(device_, kAudioDevicePropertyNominalSampleRate, inputRate);
        if (inputRate != 48000) {
            error = "Core Audio input requires device configured at 48 kHz (Audio MIDI Setup)";
            stop();
            return false;
        }
        code = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                    &device_, sizeof(device_));
        if (code != noErr) {
            return fail(code);
        }
        AudioStreamBasicDescription format{};
        format.mSampleRate = config.sampleRate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags =
            kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
        format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float) * config.channels;
        format.mFramesPerPacket = 1;
        format.mChannelsPerFrame = config.channels;
        format.mBitsPerChannel = 32;
        code = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1,
                                    &format, sizeof(format));
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
        Float64 hardwareRate = config.sampleRate;
        property(device_, kAudioDevicePropertyNominalSampleRate, hardwareRate);
        UInt32 latency = 0, safety = 0;
        property(device_, kAudioDevicePropertyLatency, latency, kAudioDevicePropertyScopeInput);
        property(device_, kAudioDevicePropertySafetyOffset, safety, kAudioDevicePropertyScopeInput);
        rate_ = config.sampleRate;
        latencyMs_ = double(latency + safety + buffer_) * 1000 / hardwareRate;
        UInt32 maximum = std::max<UInt32>(4096, buffer_ * 4);
        code = AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global,
                                    0, &maximum, sizeof(maximum));
        if (code != noErr) {
            return fail(code);
        }
        AURenderCallbackStruct callback{render, this};
        code = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global,
                                    0, &callback, sizeof(callback));
        if (code != noErr) {
            return fail(code);
        }
        code = AudioUnitInitialize(unit_);
        if (code != noErr) {
            return fail(code);
        }
        sink_ = &sink;
        channels_ = config.channels;
        capture_.resize(size_t(maximum) * channels_);
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
        sink_ = nullptr;
    }
    BackendStats stats() const noexcept override {
        return {rate_,         buffer_,       latencyMs_,    frames_.load(),
                xruns_.load(), maxNs_.load(), error_.load(), active_.load()};
    }

  private:
    static OSStatus overload(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *user) {
        static_cast<CoreAudioCapture *>(user)->xruns_.fetch_add(1);
        return noErr;
    }
    static OSStatus alive(AudioObjectID id, UInt32, const AudioObjectPropertyAddress *, void *user) {
        auto &self = *static_cast<CoreAudioCapture *>(user);
        UInt32 state = 0;
        if (property(id, kAudioDevicePropertyDeviceIsAlive, state) != noErr || !state) {
            self.error_ = kAudioHardwareBadDeviceError;
            self.active_ = false;
        }
        return noErr;
    }
    static OSStatus render(void *user, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32,
                           UInt32 frames, AudioBufferList *) {
        auto &self = *static_cast<CoreAudioCapture *>(user);
        if (!self.active_.load()) {
            return noErr;
        }
        if (size_t(frames) * self.channels_ > self.capture_.size()) {
            self.error_ = kAudio_ParamError;
            self.active_ = false;
            return kAudio_ParamError;
        }
        AudioBufferList data{};
        data.mNumberBuffers = 1;
        data.mBuffers[0].mNumberChannels = self.channels_;
        data.mBuffers[0].mDataByteSize = frames * self.channels_ * sizeof(float);
        data.mBuffers[0].mData = self.capture_.data();
        OSStatus code = AudioUnitRender(self.unit_, flags, time, 1, frames, &data);
        if (code != noErr) {
            self.error_ = code;
            self.active_ = false;
            return code;
        }
        self.sink_->capture(std::span(self.capture_).first(size_t(frames) * self.channels_), self.channels_);
        self.frames_.fetch_add(frames);
        return noErr;
    }
    AudioDeviceID device_ = kAudioObjectUnknown;
    AudioUnit unit_ = nullptr;
    CaptureSink *sink_ = nullptr;
    std::vector<float> capture_;
    uint16_t channels_ = 2;
    uint32_t rate_ = 0, buffer_ = 0;
    double latencyMs_ = 0;
    bool listening_ = false, overloadListening_ = false;
    std::atomic<bool> active_{false};
    std::atomic<uint64_t> frames_{0}, xruns_{0}, maxNs_{0};
    std::atomic<int64_t> error_{0};
};
std::unique_ptr<Capture> makeNativeCapture() {
    return std::make_unique<CoreAudioCapture>();
}
} // namespace ally::audio
