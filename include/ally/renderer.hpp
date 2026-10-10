#pragma once
#include <memory>
#include <string>
struct SDL_Window;
namespace ally::ui {
class Workspace;
}
namespace ally::graphics {
class Renderer {
  public:
    explicit Renderer(SDL_Window *window);
    ~Renderer();
    void verifyInteractions(ui::Workspace &workspace);
    void draw(ui::Workspace &workspace);
    void screenshot(const std::string &path);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ally::graphics
