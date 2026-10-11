#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
namespace ally {
std::string pathText(const std::filesystem::path &path);
std::filesystem::path textPath(std::string_view value);
// Same-directory unique temporary file, flush to storage, then OS replacement.
void atomicWrite(const std::filesystem::path &path, std::string_view data);
class FileLease {
  public:
    explicit FileLease(const std::filesystem::path &path);
    ~FileLease();
    bool acquired() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace ally
