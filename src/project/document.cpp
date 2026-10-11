#include <ally/document.hpp>
namespace ally {
DocumentSession::DocumentSession(const Project &project, std::filesystem::path path,
                                 std::filesystem::path recovery)
    : saved_(project), path_(std::move(path)), recovery_(std::move(recovery)) {}
bool DocumentSession::dirty(const Project &project) const {
    return recovered_ || project != saved_;
}
void DocumentSession::discardRecovery() {
    if (!recovery_.empty()) {
        std::error_code ignored;
        std::filesystem::remove(recovery_, ignored);
    }
    haveAutosave_ = false;
}
void DocumentSession::saved(const Project &project, const std::filesystem::path &path) {
    saved_ = project;
    path_ = std::filesystem::absolute(path).lexically_normal();
    recovered_ = false;
    discardRecovery();
}
void DocumentSession::opened(const Project &project, const std::filesystem::path &path) {
    saved(project, path);
}
void DocumentSession::recovered() {
    path_.clear();
    recovered_ = true;
}
bool DocumentSession::autosave(const Project &project, double seconds) {
    if (!dirty(project)) {
        if (haveAutosave_) {
            discardRecovery();
        }
        return false;
    }
    if (recovery_.empty() || seconds - lastAutosave_ < 30 || !dirty(project) ||
        (haveAutosave_ && project == autosaved_)) {
        return false;
    }
    std::filesystem::create_directories(recovery_.parent_path());
    project.save(recovery_);
    autosaved_ = project;
    haveAutosave_ = true;
    lastAutosave_ = seconds;
    return true;
}
} // namespace ally
