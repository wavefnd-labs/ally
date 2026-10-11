#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <ally/audio/capture.hpp>
#include <ally/audio/engine.hpp>
#include <array>
#include <atomic>
#include <audioclient.h>
#include <chrono>
#include <propkeydef.h>

#include <functiondiscoverykeys_devpkey.h>
#include <future>
#include <mmdeviceapi.h>
#include <thread>
namespace ally::audio {
namespace {
template <class T> struct ComPtr {
    T *value = nullptr;
    ~ComPtr() {
        if (value) {
            value->Release();
        }
    }
    T **put() {
        return &value;
    }
    T *operator->() const {
        return value;
    }
};
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() {
        if (SUCCEEDED(result)) {
            CoUninitialize();
        }
    }
};
std::string utf8(const wchar_t *value) {
    int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
std::wstring wide(const std::string &value) {
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, result.data(), size);
    result.pop_back();
    return result;
}
std::string failure(HRESULT code) {
    return "WASAPI HRESULT " + std::to_string(int64_t(code));
}
} // namespace
class WasapiCapture final : public Capture {
  public:
    ~WasapiCapture() override {
        stop();
    }
    std::vector<Device> devices(std::string &error) override {
        std::vector<Device> result{{"default", "System default input"}};
        ComScope apartment;
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr =
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                             reinterpret_cast<void **>(enumerator.put()));
        ComPtr<IMMDeviceCollection> collection;
        if (SUCCEEDED(hr)) {
            hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, collection.put());
        }
        UINT count = 0;
        if (SUCCEEDED(hr)) {
            hr = collection->GetCount(&count);
        }
        if (FAILED(hr)) {
            error = failure(hr);
            return result;
        }
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            ComPtr<IPropertyStore> properties;
            LPWSTR id = nullptr;
            if (FAILED(collection->Item(i, device.put())) || FAILED(device->GetId(&id))) {
                continue;
            }
            std::string label = utf8(id);
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.put()))) {
                PROPVARIANT value;
                PropVariantInit(&value);
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
                    value.vt == VT_LPWSTR) {
                    label = utf8(value.pwszVal);
                }
                PropVariantClear(&value);
            }
            result.push_back({utf8(id), label});
            CoTaskMemFree(id);
        }
        return result;
    }
    bool start(CaptureSink &sink, const CaptureConfig &config, std::string &error) override {
        stop();
        if (config.bufferFrames < 32 || config.bufferFrames > 2048 || config.deviceId.empty() ||
            config.sampleRate != 48000 || !config.channels || config.channels > 8) {
            error = "Invalid WASAPI configuration";
            return false;
        }
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        audioEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!stopEvent_ || !audioEvent_) {
            error = "Cannot create WASAPI events";
            stop();
            return false;
        }
        frames_ = xruns_ = maxNs_ = 0;
        error_ = 0;
        auto ready = std::make_shared<std::promise<HRESULT>>();
        auto result = ready->get_future();
        thread_ = std::jthread([this, &sink, config, ready] {
            ComScope apartment;
            ComPtr<IMMDeviceEnumerator> enumerator;
            ComPtr<IMMDevice> device;
            ComPtr<IAudioClient> client;
            ComPtr<IAudioCaptureClient> capture;
            HRESULT hr = apartment.result;
            if (SUCCEEDED(hr)) {
                hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void **>(enumerator.put()));
            }
            if (SUCCEEDED(hr)) {
                if (config.deviceId == "default") {
                    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, device.put());
                } else {
                    const auto id = wide(config.deviceId);
                    hr = id.empty() ? E_INVALIDARG : enumerator->GetDevice(id.c_str(), device.put());
                }
            }
            if (SUCCEEDED(hr)) {
                hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void **>(client.put()));
            }
            WAVEFORMATEX format{};
            format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            format.nChannels = config.channels;
            format.nSamplesPerSec = config.sampleRate;
            format.wBitsPerSample = 32;
            format.nBlockAlign = config.channels * 4;
            format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
            if (SUCCEEDED(hr)) {
                hr = client->Initialize(
                    AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_NOPERSIST,
                    REFERENCE_TIME(uint64_t(config.bufferFrames) * 4 * 10000000 / config.sampleRate), 0,
                    &format, nullptr);
            }
            UINT32 bufferSize = 0;
            if (SUCCEEDED(hr)) {
                hr = client->GetBufferSize(&bufferSize);
            }
            if (SUCCEEDED(hr)) {
                hr = client->SetEventHandle(audioEvent_);
            }
            if (SUCCEEDED(hr)) {
                hr = client->GetService(__uuidof(IAudioCaptureClient),
                                        reinterpret_cast<void **>(capture.put()));
            }
            REFERENCE_TIME latency = 0;
            if (SUCCEEDED(hr)) {
                client->GetStreamLatency(&latency);
            }
            BYTE *data = nullptr;
            if (SUCCEEDED(hr)) {
                hr = client->Start();
            }
            if (FAILED(hr)) {
                error_ = hr;
                ready->set_value(hr);
                return;
            }
            rate_ = config.sampleRate;
            buffer_ = bufferSize;
            latencyMs_ = double(latency) / 10000;
            active_ = true;
            ready->set_value(S_OK);
            HANDLE events[]{stopEvent_, audioEvent_};
            while (active_.load()) {
                DWORD wait = WaitForMultipleObjects(2, events, FALSE, 2000);
                if (wait == WAIT_OBJECT_0) {
                    break;
                }
                if (wait != WAIT_OBJECT_0 + 1) {
                    error_ = wait;
                    break;
                }
                UINT32 packet = 0;
                hr = capture->GetNextPacketSize(&packet);
                while (SUCCEEDED(hr) && packet) {
                    UINT32 count = 0;
                    DWORD flags = 0;
                    hr = capture->GetBuffer(&data, &count, &flags, nullptr, nullptr);
                    if (FAILED(hr)) {
                        break;
                    }
                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                        xruns_.fetch_add(1);
                    }
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        std::array<float, 2048> silence{};
                        for (UINT32 at = 0; at < count;) {
                            UINT32 n = std::min<UINT32>(256, count - at);
                            sink.capture(std::span(silence).first(size_t(n) * config.channels),
                                         config.channels);
                            at += n;
                        }
                    } else {
                        sink.capture({reinterpret_cast<const float *>(data), size_t(count) * config.channels},
                                     config.channels);
                    }
                    frames_.fetch_add(count);
                    hr = capture->ReleaseBuffer(count);
                    if (SUCCEEDED(hr)) {
                        hr = capture->GetNextPacketSize(&packet);
                    }
                }
                if (FAILED(hr)) {
                    error_ = hr;
                    break;
                }
            }
            client->Stop();
            active_ = false;
        });
        const auto hr = result.get();
        if (FAILED(hr)) {
            error = failure(hr);
            stop();
            return false;
        }
        return true;
    }
    void stop() noexcept override {
        if (stopEvent_) {
            SetEvent(stopEvent_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        active_ = false;
        if (audioEvent_) {
            CloseHandle(audioEvent_);
            audioEvent_ = nullptr;
        }
        if (stopEvent_) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
    }
    BackendStats stats() const noexcept override {
        return {rate_,         buffer_,       latencyMs_,    frames_.load(),
                xruns_.load(), maxNs_.load(), error_.load(), active_.load()};
    }

  private:
    HANDLE stopEvent_ = nullptr, audioEvent_ = nullptr;
    std::jthread thread_;
    uint32_t rate_ = 0, buffer_ = 0;
    double latencyMs_ = 0;
    std::atomic<bool> active_{false};
    std::atomic<uint64_t> frames_{0}, xruns_{0}, maxNs_{0};
    std::atomic<int64_t> error_{0};
};
std::unique_ptr<Capture> makeNativeCapture() {
    return std::make_unique<WasapiCapture>();
}
} // namespace ally::audio
