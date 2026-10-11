#include <SDL3/SDL.h>
#include <algorithm>
#include <ally/audio/backend.hpp>
#include <ally/file.hpp>
#include <ally/media/wav.hpp>
#include <ally/midi.hpp>
#include <ally/project.hpp>
#include <ally/renderer.hpp>
#include <ally/workspace.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {
struct DialogResult {
    std::string action, error;
    std::filesystem::path path;
};
struct DialogMailbox {
    std::mutex mutex;
    std::deque<DialogResult> results;
};
struct DialogContext {
    std::shared_ptr<DialogMailbox> mailbox;
    std::string action;
};
void SDLCALL fileDialogResult(void *userdata, const char *const *files, int) {
    std::unique_ptr<DialogContext> context(static_cast<DialogContext *>(userdata));
    DialogResult result;
    result.action = context->action;
    if (!files) {
        result.error = SDL_GetError();
    } else if (files[0]) {
        result.path = ally::textPath(files[0]);
    }
    std::lock_guard lock(context->mailbox->mutex);
    context->mailbox->results.push_back(std::move(result));
}
int ask(SDL_Window *window, const char *title, const char *message, const char *first, const char *second) {
    const SDL_MessageBoxButtonData buttons[]{
        {0, 1, first},
        {0, 2, second},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Cancel"}};
    const SDL_MessageBoxData data{SDL_MESSAGEBOX_INFORMATION, window, title, message, 3, buttons, nullptr};
    int choice = 0;
    if (!SDL_ShowMessageBox(&data, &choice)) {
        return 0;
    }
    return choice;
}
} // namespace

int main(int argc, char **argv) {
    try {
        bool smoke = false, offline = false, autoplay = false, listDevices = false, noRecovery = false;
        ally::audio::BackendConfig audioConfig;
        int frameLimit = 0, windowWidth = 1440, windowHeight = 900;
        std::string initialView = "piano";
        std::string capture, render, projectPath, inspect;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--list-devices") {
                listDevices = true;
            } else if (arg == "--no-recovery") {
                noRecovery = true;
            } else if (arg == "--device" && i + 1 < argc) {
                audioConfig.deviceId = argv[++i];
            } else if (arg == "--buffer" && i + 1 < argc) {
                audioConfig.bufferFrames = uint32_t(std::stoul(argv[++i]));
                if (audioConfig.bufferFrames < 32 || audioConfig.bufferFrames > 2048) {
                    throw std::runtime_error("Buffer must be 32..2048 frames");
                }
            } else if (arg == "--smoke") {
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
                       "    [--window-size width height] [--list-devices] [--device id] [--buffer frames] "
                       "[--no-recovery]\n";
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
        if (listDevices) {
            auto backend = ally::audio::makeNativeBackend();
            std::string error;
            for (const auto &device : backend->devices(error)) {
                std::cout << device.id << " : " << device.name << '\n';
            }
            if (!error.empty()) {
                std::cerr << error << '\n';
            }
            return error.empty() ? 0 : 1;
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
        std::filesystem::path recovery;
        std::unique_ptr<ally::FileLease> recoveryLease;
        bool recovered = false;
        if (!smoke && !noRecovery && frameLimit == 0) {
            char *pref = SDL_GetPrefPath("wavefnd-labs", "Ally");
            std::filesystem::path directory;
            if (pref) {
                directory = ally::textPath(pref) / "recovery";
                SDL_free(pref);
            }
            if (!directory.empty()) {
                std::filesystem::create_directories(directory);
                std::vector<std::filesystem::path> snapshots;
                for (const auto &entry : std::filesystem::directory_iterator(directory)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".ally") {
                        snapshots.push_back(entry.path());
                    }
                }
                std::sort(snapshots.begin(), snapshots.end(), [](const auto &a, const auto &b) {
                    return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b);
                });
                recovery =
                    directory /
                    ("session-" + std::to_string(SDL_GetTicksNS()) + "-" +
                     std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".ally");
                auto lockPath = recovery;
                lockPath += ".lock";
                recoveryLease = std::make_unique<ally::FileLease>(lockPath);
                if (!recoveryLease->acquired()) {
                    throw std::runtime_error("Cannot lock recovery session");
                }
                for (const auto &snapshot : snapshots) {
                    lockPath = snapshot;
                    lockPath += ".lock";
                    ally::FileLease candidate(lockPath);
                    if (!candidate.acquired()) {
                        continue;
                    }
                    const int choice =
                        ask(nullptr, "Ally recovery", "An unsaved recovery snapshot is available.", "Recover",
                            "Discard snapshot");
                    if (!choice) {
                        return 0;
                    }
                    if (choice == 1) {
                        auto restored = ally::Project::load(snapshot);
                        restored.save(recovery);
                        project = std::move(restored);
                        projectPath.clear();
                        recovered = true;
                    }
                    std::filesystem::remove(snapshot);
                    if (recovered) {
                        break;
                    }
                }
            }
        }
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
        ally::audio::Engine engine(48000);
        ally::ui::Workspace workspace(project, engine);
        workspace.setDocument(ally::textPath(projectPath), recovery, recovered);
        workspace.action(initialView);
        workspace.sync();
        auto backend = ally::audio::makeNativeBackend();
        bool device = false;
        if (!offline) {
            std::string error;
            device = backend->start(engine, audioConfig, error);
            workspace.backendStatus = device ? backend->name() : "Audio unavailable";
            if (!device) {
                workspace.status = "Audio unavailable: " + error;
                std::cerr << workspace.status << '\n';
            }
        } else {
            workspace.backendStatus = "Offline preview (no device)";
        }
        workspace.audioConfig = audioConfig;
        std::string enumerationError;
        if (!offline) {
            workspace.audioDevices = backend->devices(enumerationError);
        }
        auto mailbox = std::make_shared<DialogMailbox>();
        workspace.requestFile = [&](const std::string &action) {
            if (action == "load" && workspace.modified()) {
                const auto choice =
                    ask(window.get(), "Ally", "Current project has unsaved changes. Open another project?",
                        "Discard changes and open", "Cancel open");
                if (choice != 1) {
                    workspace.fileSelected(action, {});
                    return;
                }
            }
            static const SDL_DialogFileFilter projectFilters[]{{"Ally project", "ally"}};
            static const SDL_DialogFileFilter wavFilters[]{{"WAV audio", "wav"}};
            auto *context = new DialogContext{mailbox, action};
            const bool wav = action == "import" || action == "relink" || action == "export";
            const auto *filter = wav ? wavFilters : projectFilters;
            if (action == "save" || action == "saveas" || action == "export") {
                SDL_ShowSaveFileDialog(fileDialogResult, context, window.get(), filter, 1,
                                       action == "export" ? "render.wav" : "session.ally");
            } else {
                SDL_ShowOpenFileDialog(fileDialogResult, context, window.get(), filter, 1, nullptr, false);
            }
        };
        workspace.requestAudio = [&](const ally::audio::BackendConfig &config) {
            if (smoke) {
                audioConfig = config;
                return;
            }
            // stop/join transfers the sole Engine::process owner before restarting or offline preview.
            backend->stop();
            std::string error;
            device = backend->start(engine, config, error);
            workspace.backendStatus = device ? backend->name() : "Audio unavailable";
            workspace.status = device ? "Audio output changed" : "Audio start failed: " + error;
            audioConfig = config;
        };
        if (!project.sourceWarnings().empty()) {
            workspace.status = project.sourceWarnings();
        }
        ally::midi::Input midi(engine);
        workspace.midiStatus = offline ? "MIDI device probing disabled in offline mode" : midi.connectFirst();
        std::cout << "Renderer: SDL3 / Skia Ganesh OpenGL\nAudio: " << workspace.backendStatus << '\n'
                  << workspace.midiStatus << '\n';
        if (autoplay) {
            engine.play(true);
        }
        float observedPeak = 0;
        bool running = true, textActive = false, closePending = false, closeAfterSave = false;
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
                    closePending = true;
                } else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
                    auto path = ally::textPath(event.drop.data);
                    auto extension = ally::pathText(path.extension());
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                                   [](unsigned char c) { return char(std::tolower(c)); });
                    if (extension == ".wav") {
                        workspace.fileSelected("import", path);
                    } else if (extension == ".ally") {
                        if (!workspace.modified() ||
                            ask(window.get(), "Ally", "Open dropped project and discard current changes?",
                                "Open", "Cancel open") == 1) {
                            workspace.fileSelected("load", path);
                        }
                    } else {
                        workspace.status = "Drop a WAV audio or Ally project file";
                    }
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
                            workspace.action(event.key.mod & SDL_KMOD_SHIFT ? "saveas" : "save");
                        }
                        if (key == SDLK_O) {
                            workspace.action("load");
                        }
                        if (key == SDLK_I) {
                            workspace.action("import");
                        }
                        if (key == SDLK_COMMA) {
                            workspace.action("audio-settings");
                        }
                        if (key == SDLK_E) {
                            workspace.action("export");
                        }
                    }
                }
            }
            std::deque<DialogResult> selectedFiles;
            {
                std::lock_guard lock(mailbox->mutex);
                selectedFiles.swap(mailbox->results);
            }
            for (auto &result : selectedFiles) {
                if (!result.path.empty() &&
                    (result.action == "save" || result.action == "saveas" || result.action == "export") &&
                    result.path.extension().empty()) {
                    result.path += result.action == "export" ? ".wav" : ".ally";
                }
                workspace.fileSelected(result.action, result.path);
                if (!result.error.empty()) {
                    workspace.status = "File dialog failed: " + result.error;
                }
            }
            workspace.tick(double(SDL_GetTicks()) / 1000);
            workspace.audioStats = backend->stats();
            if (device && !workspace.audioStats.active) {
                backend->stop();
                device = false;
                workspace.backendStatus = "Audio unavailable";
                workspace.status =
                    "Audio device stopped; native error " + std::to_string(workspace.audioStats.errorCode);
            }
            if (closeAfterSave && !workspace.busy()) {
                if (!workspace.modified()) {
                    running = false;
                }
                closeAfterSave = false;
            }
            if (closePending && !workspace.busy()) {
                closePending = false;
                if (!workspace.modified() || smoke) {
                    workspace.discardRecovery();
                    running = false;
                } else {
                    const auto choice =
                        ask(window.get(), "Ally", "Save changes before closing?", "Save", "Discard");
                    if (choice == 1) {
                        workspace.action("save");
                        closeAfterSave = true;
                    } else if (choice == 2) {
                        workspace.discardRecovery();
                        running = false;
                    }
                }
            }
            SDL_SetWindowTitle(window.get(), workspace.title().c_str());
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
        const auto diagnostics = backend->stats();
        backend->stop();
        if (device) {
            std::cout << "Audio diagnostics: buffer=" << diagnostics.bufferFrames
                      << " frames, latency=" << diagnostics.outputLatencyMs
                      << " ms, processed=" << diagnostics.processedFrames
                      << ", xruns=" << diagnostics.underruns
                      << ", process_max=" << double(diagnostics.maxProcessNs) / 1e6 << " ms\n";
        }
        if (autoplay) {
            std::cout << "Observed playback peak: " << observedPeak << '\n';
        }
        if (smoke && !closeVerified) {
            throw std::runtime_error("SDL native window close event failed");
        }
        if (smoke) {
            std::cout
                << "UI smoke passed: GPU render, note editing, velocity, arrangement clips, "
                   "undo/redo, master/channel faders, devices, search, loop/metronome, mute/solo, BPM, "
                   "independent patterns, WAV import/trim/loop, save/open/export workers, audio settings, "
                   "SDL transport, native window close\n";
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "ally: " << error.what() << '\n';
        return 1;
    }
}
