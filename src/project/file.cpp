#include <algorithm>
#include <ally/file.hpp>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <system_error>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace ally {
std::string pathText(const std::filesystem::path &path) {
    auto value = path.u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}
std::filesystem::path textPath(std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
struct FileLease::State {
#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
};
FileLease::FileLease(const std::filesystem::path &path) : state_(std::make_unique<State>()) {
#if defined(_WIN32)
    state_->handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    state_->handle = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (state_->handle >= 0 && ::flock(state_->handle, LOCK_EX | LOCK_NB) != 0) {
        ::close(state_->handle);
        state_->handle = -1;
    }
#endif
}
FileLease::~FileLease() {
#if defined(_WIN32)
    if (acquired()) {
        CloseHandle(state_->handle);
    }
#else
    if (acquired()) {
        ::close(state_->handle);
    }
#endif
}
bool FileLease::acquired() const noexcept {
#if defined(_WIN32)
    return state_->handle != INVALID_HANDLE_VALUE;
#else
    return state_->handle >= 0;
#endif
}
void replaceFile(const std::filesystem::path &temporary, const std::filesystem::path &destination) {
#if defined(_WIN32)
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::system_error(int(GetLastError()), std::system_category(), "Open render temporary");
    }
    bool ok = FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), destination.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::system_error(int(GetLastError()), std::system_category(), "Replace rendered file");
    }
#else
    int file = ::open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
    if (file < 0) {
        throw std::system_error(errno, std::generic_category(), "Open render temporary");
    }
    int rc = ::fsync(file);
    int error = errno;
    ::close(file);
    if (rc) {
        throw std::system_error(error, std::generic_category(), "Flush rendered file");
    }
    std::filesystem::rename(temporary, destination);
#endif
}
void atomicWrite(const std::filesystem::path &path, std::string_view data) {
    static std::atomic<uint64_t> sequence{0};
    auto temp = path;
    temp += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(sequence.fetch_add(1));
    try {
#if defined(_WIN32)
        HANDLE file =
            CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            throw std::system_error(int(GetLastError()), std::system_category(),
                                    "Create project temporary file");
        }
        bool ok = true;
        for (size_t at = 0; at < data.size() && ok;) {
            DWORD written = 0;
            ok = WriteFile(file, data.data() + at, DWORD(std::min<size_t>(data.size() - at, 1024 * 1024)),
                           &written, nullptr) &&
                 written;
            at += written;
        }
        ok = ok && FlushFileBuffers(file);
        const auto error = GetLastError();
        CloseHandle(file);
        if (!ok) {
            throw std::system_error(int(error), std::system_category(), "Flush project");
        }
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::system_error(int(GetLastError()), std::system_category(), "Replace project");
        }
#else
        int file = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (file < 0) {
            throw std::system_error(errno, std::generic_category(), "Create project temporary file");
        }
        int error = 0;
        for (size_t at = 0; at < data.size();) {
            const auto n = ::write(file, data.data() + at, data.size() - at);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                error = n < 0 ? errno : EIO;
                break;
            }
            at += size_t(n);
        }
        if (!error && ::fsync(file) != 0) {
            error = errno;
        }
        if (::close(file) != 0 && !error) {
            error = errno;
        }
        if (error) {
            throw std::system_error(error, std::generic_category(), "Flush project");
        }
        std::filesystem::rename(temp, path);
        auto parent = path.parent_path();
        if (parent.empty()) {
            parent = ".";
        }
        int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory >= 0) {
            ::fsync(directory);
            ::close(directory);
        }
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw;
    }
}
} // namespace ally
