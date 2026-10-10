#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/gl/GrGLAssembleInterface.h"
#include "include/gpu/gl/GrGLInterface.h"
#include <SDL3/SDL.h>
#include <ally/renderer.hpp>
#include <ally/workspace.hpp>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace ally::graphics {
struct Renderer::Impl {
    SDL_Window *window;
    SDL_GLContext gl = nullptr;
    sk_sp<GrDirectContext> context;
    sk_sp<SkSurface> surface;
    int width = 0, height = 0;
    explicit Impl(SDL_Window *w) : window(w) {}
    ~Impl() {
        surface.reset();
        context.reset();
        if (gl) {
            SDL_GL_DestroyContext(gl);
        }
    }
};
Renderer::Renderer(SDL_Window *window) : impl_(std::make_unique<Impl>(window)) {
    impl_->gl = SDL_GL_CreateContext(window);
    if (!impl_->gl) {
        throw std::runtime_error(SDL_GetError());
    }
    auto interface = GrGLMakeAssembledInterface(nullptr, [](void *, const char *name) -> GrGLFuncPtr {
        // GLX may return a dispatch stub for unknown EGL names. This renderer
        // wraps a GL framebuffer and needs only GL entry points.
        if (!std::string_view(name).starts_with("gl")) {
            return nullptr;
        }
        return reinterpret_cast<GrGLFuncPtr>(SDL_GL_GetProcAddress(name));
    });
    if (!interface) {
        throw std::runtime_error("Cannot assemble Skia GL interface");
    }
    impl_->context = GrDirectContexts::MakeGL(interface);
    if (!impl_->context) {
        throw std::runtime_error("Cannot initialize Skia Ganesh GL");
    }
    SDL_GL_SetSwapInterval(1);
    using GlString = const unsigned char *(*)(unsigned int);
    auto glString = reinterpret_cast<GlString>(SDL_GL_GetProcAddress("glGetString"));
    if (glString) {
        std::cout << "OpenGL device: " << glString(0x1f01) << '\n';
    }
}

Renderer::~Renderer() = default;
void Renderer::draw(ui::Workspace &workspace) {
    int w = 0, h = 0, logicalW = 0, logicalH = 0;
    SDL_GetWindowSizeInPixels(impl_->window, &w, &h);
    SDL_GetWindowSize(impl_->window, &logicalW, &logicalH);
    if (w <= 0 || h <= 0 || logicalW <= 0 || logicalH <= 0) {
        return;
    }
    if (!impl_->surface || w != impl_->width || h != impl_->height) {
        impl_->surface.reset();
        using GetInteger = void (*)(unsigned int, int *);
        auto getInteger = reinterpret_cast<GetInteger>(SDL_GL_GetProcAddress("glGetIntegerv"));
        if (!getInteger) {
            throw std::runtime_error("Missing glGetIntegerv");
        }
        int fbo = 0, stencil = 0, samples = 0;
        getInteger(0x8ca6, &fbo);
        getInteger(0x0d57, &stencil);
        getInteger(0x80a9, &samples);
        GrGLFramebufferInfo info{static_cast<GrGLuint>(fbo), 0x8058}; // GL_RGBA8
        auto target = GrBackendRenderTargets::MakeGL(w, h, samples, stencil, info);
        impl_->surface =
            SkSurfaces::WrapBackendRenderTarget(impl_->context.get(), target, kBottomLeft_GrSurfaceOrigin,
                                                kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
        if (!impl_->surface) {
            throw std::runtime_error("Cannot wrap SDL OpenGL framebuffer in Skia");
        }
        impl_->width = w;
        impl_->height = h;
    }
    auto *canvas = impl_->surface->getCanvas();
    canvas->save();
    canvas->scale(float(w) / logicalW, float(h) / logicalH);
    workspace.draw(*canvas, float(logicalW), float(logicalH));
    canvas->restore();
    impl_->context->flushAndSubmit();
    if (!SDL_GL_SwapWindow(impl_->window)) {
        throw std::runtime_error(SDL_GetError());
    }
}

void Renderer::verifyInteractions(ui::Workspace &workspace) {
    if (!impl_->surface) {
        throw std::runtime_error("Layout must be rendered before UI verification");
    }
    int w = 0, h = 0;
    SDL_GetWindowSize(impl_->window, &w, &h);
    auto *canvas = impl_->surface->getCanvas();
    canvas->save();
    canvas->scale(float(impl_->width) / w, float(impl_->height) / h);
    workspace.verifyInteractions(*canvas, float(w), float(h));
    canvas->restore();
}

void Renderer::screenshot(const std::string &path) {
    if (!impl_->surface) {
        throw std::runtime_error("No rendered frame to capture");
    }
    const int w = impl_->width, h = impl_->height;
    std::vector<uint32_t> pixels(size_t(w) * h);
    auto info = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    if (!impl_->surface->readPixels(info, pixels.data(), size_t(w) * 4, 0, 0)) {
        throw std::runtime_error("GPU framebuffer readPixels failed");
    }
    auto *image = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4);
    if (!image) {
        throw std::runtime_error(SDL_GetError());
    }
    bool saved = SDL_SaveBMP(image, path.c_str());
    SDL_DestroySurface(image);
    if (!saved) {
        throw std::runtime_error(SDL_GetError());
    }
}
} // namespace ally::graphics
