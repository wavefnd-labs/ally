#include <SDL3/SDL.h>
#include <ally/audio/backend.hpp>
#include <ally/media/wav.hpp>
#include <ally/midi.hpp>
#include <ally/project.hpp>
#include <ally/renderer.hpp>
#include <ally/workspace.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
    try {
        bool smoke = false, offline = false, autoplay = false;
        int frameLimit = 0, windowWidth = 1440, windowHeight = 900;
        std::string initialView = "piano";
        std::string capture, render, projectPath, inspect;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--smoke") {
                smoke = true;
                frameLimit = 24;
                offline = true;
            } else if (arg == "--view" && i + 1 < argc) {
                initialView = argv[++i];
                if (initialView != "piano" && initialView != "mixer" && initialView != "devices") {
                    throw std::runtime_error("Expected piano, devices or mixer view");
                }
            } else if (arg == "--window-size" && i + 2 < argc) {
                windowWidth = std::stoi(argv[++i]);
                windowHeight = std::stoi(argv[++i]);
                if (windowWidth < 870 || windowWidth > 3840 || windowHeight < 650 || windowHeight > 2160) {
                    throw std::runtime_error("Window size must be 870..3840 x 650..2160");
                }
            } else if (arg == "--play") {
                autoplay = true;
            } else if (arg == "--offline") {
                offline = true;
            } else if (arg == "--frames" && i + 1 < argc) {
                frameLimit = std::stoi(argv[++i]);
            } else if (arg == "--screenshot" && i + 1 < argc) {
                capture = argv[++i];
            } else if (arg == "--render" && i + 1 < argc) {
                render = argv[++i];
            } else if (arg == "--project" && i + 1 < argc) {
                projectPath = argv[++i];
            } else if (arg == "--inspect" && i + 1 < argc) {
                inspect = argv[++i];
            } else if (arg == "--help") {
                std::cout
                    << "ally [--play] [--offline] [--project file] [--render file.wav] [--inspect file.wav]\n"
                       "    [--smoke] [--frames N] [--screenshot file.bmp] [--view "
                       "piano|devices|mixer]\n"
                       "    [--window-size width height]\n";
                return 0;
            } else {
                throw std::runtime_error("Unknown or incomplete argument: " + arg);
            }
        }
        if (!inspect.empty()) {
            auto pcm = ally::media::readWav(inspect);
            std::cout << pcm.sampleRate << " Hz / " << pcm.channels << " channels / " << pcm.frames()
                      << " frames\n";
            return 0;
        }
        auto project = projectPath.empty() ? ally::Project::demo() : ally::Project::load(projectPath);
        if (!render.empty()) {
            ally::renderProject(project, render);
            std::cout << "Rendered " << render << '\n';
            return 0;
        }
#if defined(ALLY_PREFER_X11_WITHOUT_LIBDECOR)
        // Without libdecor, GNOME Wayland cannot provide the native title bar.
        // Use the OS-managed X11 frame via XWayland; respect explicit driver settings.
        if (std::getenv("DISPLAY") && !std::getenv("SDL_VIDEODRIVER") && !std::getenv("SDL_VIDEO_DRIVER")) {
            SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11,wayland", SDL_HINT_DEFAULT);
        }
#endif
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            throw std::runtime_error(SDL_GetError());
        }
        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#if defined(__APPLE__)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
        auto window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>(
            SDL_CreateWindow("Ally | Music workspace", windowWidth, windowHeight,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
            SDL_DestroyWindow);
        if (!window) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_SetWindowMinimumSize(window.get(), 870, 650);
        ally::graphics::Renderer renderer(window.get());
        auto texture = ally::makeTexture();
        ally::audio::Engine engine(48000, texture);
        ally::ui::Workspace workspace(project, engine, texture);
        workspace.action(initialView);
        workspace.sync();
        auto backend = ally::audio::makeNativeBackend();
        bool device = false;
        if (!offline) {
            std::string error;
            device = backend->start(engine, error);
            workspace.backendStatus = device ? backend->name() : "Audio unavailable";
            if (!device) {
                workspace.status = "Audio unavailable: " + error;
                std::cerr << workspace.status << '\n';
            }
        } else {
            workspace.backendStatus = "Offline preview (no device)";
        }
        ally::midi::Input midi(engine);
        workspace.midiStatus = offline ? "MIDI device probing disabled in offline mode" : midi.connectFirst();
        std::cout << "Renderer: SDL3 / Skia Ganesh OpenGL\nAudio: " << workspace.backendStatus << '\n'
                  << workspace.midiStatus << '\n';
        if (autoplay) {
            engine.play(true);
        }
        float observedPeak = 0;
        bool running = true, textActive = false;
        int frames = 0;
        bool closeVerified = false;
        auto last = SDL_GetTicksNS();
        double pendingFrames = 0;
        std::array<float, 512> scratch{};
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    closeVerified = true;
                    running = false;
                } else if (event.type == SDL_EVENT_TEXT_INPUT) {
                    workspace.textInput(event.text.text);
                } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    workspace.release();
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    workspace.press(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT);
                } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    workspace.move(event.motion.x, event.motion.y);
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    workspace.release();
                } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    workspace.wheel(event.wheel.y, SDL_GetModState() & SDL_KMOD_CTRL,
                                    SDL_GetModState() & SDL_KMOD_SHIFT);
                } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    auto key = event.key.key;
                    if (workspace.textEditing()) {
                        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                            workspace.editKey("enter");
                        } else if (key == SDLK_ESCAPE) {
                            workspace.editKey("escape");
                        } else if (key == SDLK_BACKSPACE) {
                            workspace.editKey("backspace");
                        }
                    } else if (key == SDLK_SPACE) {
                        workspace.action("play");
                    } else if (key == SDLK_ESCAPE) {
                        workspace.action("stop");
                    } else if (key == SDLK_F5) {
                        workspace.action("piano");
                    } else if (key == SDLK_F8) {
                        workspace.action("browser");
                    } else if (key == SDLK_F9) {
                        workspace.action("mixer");
                    } else if (event.key.mod & SDL_KMOD_CTRL) {
                        if (key == SDLK_Z) {
                            workspace.action(event.key.mod & SDL_KMOD_SHIFT ? "redo" : "undo");
                        }
                        if (key == SDLK_Y) {
                            workspace.action("redo");
                        }
                        if (key == SDLK_S) {
                            workspace.action("save");
                        }
                        if (key == SDLK_O) {
                            workspace.action("load");
                        }
                        if (key == SDLK_E) {
                            workspace.action("export");
                        }
                    }
                }
            }
            if (workspace.textEditing() != textActive) {
                textActive = workspace.textEditing();
                if (textActive) {
                    SDL_StartTextInput(window.get());
                } else {
                    SDL_StopTextInput(window.get());
                }
            }
            workspace.sync();
            if (!device) {
                auto now = SDL_GetTicksNS();
                pendingFrames += double(now - last) * engine.sampleRate() / 1e9;
                last = now;
                pendingFrames = std::min(pendingFrames, double(engine.sampleRate()) * 0.1);
                while (pendingFrames >= 256) {
                    engine.process(scratch);
                    pendingFrames -= 256;
                }
            }
            renderer.draw(workspace);
            if (frames == 1) {
                int top = 0, left = 0, bottom = 0, right = 0;
                if (SDL_GetWindowBordersSize(window.get(), &top, &left, &bottom, &right)) {
                    std::cout << "Window: OS native / " << SDL_GetCurrentVideoDriver() << " / title bar "
                              << top << " px\n";
                }
            }
            observedPeak = std::max(observedPeak, engine.peak());
            if (smoke && frames == 1) {
                renderer.verifyInteractions(workspace);
                SDL_Event key{};
                key.type = SDL_EVENT_KEY_DOWN;
                key.key.key = SDLK_SPACE;
                SDL_PushEvent(&key);
            }
            if (smoke && frames == 3 && !engine.playing()) {
                throw std::runtime_error("SDL keyboard transport failed");
            }
            if (smoke && frames == 10) {
                SDL_Event close{};
                close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
                close.window.windowID = SDL_GetWindowID(window.get());
                SDL_PushEvent(&close);
            }
            ++frames;
            if (frameLimit > 0 && frames >= frameLimit) {
                running = false;
            }
            SDL_Delay(8);
        }
        if (!capture.empty()) {
            renderer.screenshot(capture);
            std::cout << "GPU screenshot: " << capture << '\n';
        }
        backend->stop();
        if (autoplay) {
            std::cout << "Observed playback peak: " << observedPeak << '\n';
        }
        if (smoke && !closeVerified) {
            throw std::runtime_error("SDL native window close event failed");
        }
        if (smoke) {
            std::cout << "UI smoke passed: GPU render, note editing, velocity, arrangement clips, "
                         "undo/redo, master/channel faders, devices, search, loop/metronome, mute/solo, BPM, "
                         "SDL transport, native window close\n";
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "ally: " << error.what() << '\n';
        return 1;
    }
}
