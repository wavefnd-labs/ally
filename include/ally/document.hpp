#pragma once
#include <ally/project.hpp>
#include <filesystem>
namespace ally {
class DocumentSession {
  public:
    DocumentSession(const Project &project, std::filesystem::path path = {},
                    std::filesystem::path recovery = {});
    bool dirty(const Project &project) const;
    const std::filesystem::path &path() const {
        return path_;
    }
    const std::filesystem::path &recoveryPath() const {
        return recovery_;
    }
    void saved(const Project &project, const std::filesystem::path &path);
    void opened(const Project &project, const std::filesystem::path &path);
    void recovered();
    bool autosave(const Project &project, double seconds);
    void discardRecovery();

  private:
    Project saved_, autosaved_;
    std::filesystem::path path_, recovery_;
    double lastAutosave_ = 0;
    bool recovered_ = false, haveAutosave_ = false;
};
} // namespace ally
