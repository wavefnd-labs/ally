#include <SDL3/SDL.h>
#include <algorithm>
#include <ally/audio/backend.hpp>
#include <ally/audio/capture.hpp>
#include <ally/file.hpp>
#include <ally/media/stream.hpp>
#include <ally/media/wav.hpp>
#include <ally/midi.hpp>
#include <ally/project.hpp>
#include <ally/recording.hpp>
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
        int recordSeconds = 0;
        int frameLimit = 0, windowWidth = 1440, windowHeight = 900;
        std::string initialView = "piano";
        std::string capture, render, projectPath, inspect, recordProbe, inputDevice, recoverInput,
            recoverOutput;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--recover-recording" && i + 2 < argc) {
                recoverInput = argv[++i];
                recoverOutput = argv[++i];
            } else if (arg == "--record-probe" && i + 1 < argc) {
                recordProbe = argv[++i];
            } else if (arg == "--record-seconds" && i + 1 < argc) {
                recordSeconds = std::stoi(argv[++i]);
                if (recordSeconds < 1 || recordSeconds > 3600) {
                    throw std::runtime_error("Record probe duration must be 1..3600 seconds");
                }
            } else if (arg == "--input-device" && i + 1 < argc) {
                inputDevice = argv[++i];
            } else if (arg == "--list-devices") {
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
                       "[--recover-recording partial.wav recovered.wav] [--no-recovery] [--input-device id] "
                       "[--record-probe take.wav --frames N | --record-seconds N]\n";
                return 0;
            } else {
                throw std::runtime_error("Unknown or incomplete argument: " + arg);
            }
        }
        if (!recordProbe.empty()) {
            noRecovery = true; // Automated capture probes must not open another session's recovery dialog.
        }
        if (recordSeconds && recordProbe.empty()) {
            throw std::runtime_error("--record-seconds requires --record-probe");
        }
        if (!recoverInput.empty()) {
            std::cout << "Recovered "
                      << ally::media::recoverRecording(ally::textPath(recoverInput),
                                                       ally::textPath(recoverOutput))
                      << " complete frames; source preserved\n";
            return 0;
        }
        if (!inspect.empty()) {
            ally::media::WavReader reader(ally::textPath(inspect));
            const auto &pcm = reader.info();
            std::cout << pcm.sampleRate << " Hz / " << pcm.channels << " channels / " << pcm.frames
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
        auto project = projectPath.empty()
                           ? (recordProbe.empty() ? ally::Project::demo() : ally::Project::empty())
                           : ally::Project::load(projectPath);
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
        ally::Recorder recorder(engine);
        auto input = ally::audio::makeNativeCapture();
        std::string inputError;
        if (!offline) {
            workspace.inputDevices = input->devices(inputError);
        }
        double takeStartBeat = 0;
        uint64_t takeStartFrame = 0, takeStartClock = 0;
        uint32_t takeGraphLatency = 0;
        double takeInputLatency = 0, takeOutputLatency = 0;
        if (!inputDevice.empty()) {
            workspace.inputConfig.deviceId = inputDevice;
        }
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
            if (action == "record-audio" && !recordProbe.empty()) {
                workspace.fileSelected(action, ally::textPath(recordProbe));
                return;
            }
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
            const bool wav =
                action == "import" || action == "relink" || action == "export" || action == "record-audio";
            const auto *filter = wav ? wavFilters : projectFilters;
            if (action == "save" || action == "saveas" || action == "export" || action == "record-audio") {
                SDL_ShowSaveFileDialog(fileDialogResult, context, window.get(), filter, 1,
                                       action == "record-audio" ? "take.wav"
                                       : action == "export"     ? "render.wav"
                                                                : "session.ally");
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
        workspace.requestMonitor = [&] {
            if (workspace.recordingAudio) {
                workspace.status = "Stop recording before changing monitoring";
                return;
            }
            input->stop();
            workspace.monitoring = !workspace.monitoring;
            recorder.monitor(workspace.monitoring ? workspace.selectedTrack() : -1,
                             workspace.selectedInputChannel());
            if (workspace.monitoring) {
                takeStartFrame = engine.position();
                takeStartClock = engine.timestamp();
                takeGraphLatency = ally::audio::planLatencyFrames(project.compile(engine.sampleRate()));
                takeOutputLatency = backend->stats().outputLatencyMs;
                workspace.audioRecordingState(true);
                engine.play(true);
                std::string error;
                if (!input->start(recorder, workspace.inputConfig, error)) {
                    workspace.audioRecordingState(false);
                    engine.play(false);
                    workspace.monitoring = false;
                    recorder.monitor(-1);
                    workspace.status = "Input unavailable: " + error;
                } else {
                    workspace.status = "Input monitoring enabled";
                }
            } else {
                workspace.status = "Input monitoring disabled";
            }
        };
        workspace.requestInput = [&](const ally::audio::CaptureConfig &config) {
            if (workspace.recordingAudio) {
                workspace.status = "Stop recording before changing input device";
                return;
            }
            input->stop();
            if (workspace.monitoring) {
                std::string error;
                if (!input->start(recorder, config, error)) {
                    workspace.monitoring = false;
                    recorder.monitor(-1);
                    workspace.status = "Input unavailable: " + error;
                }
            }
        };
        workspace.requestRecord = [&] {
            if (workspace.recordingAudio) {
                input->stop();
                workspace.audioRecordingState(false);
                engine.play(false);
                try {
                    auto files = recorder.finish();
                    engine.play(false);
                    if (recorder.capturedFrames() && recorder.firstCaptureClock() != UINT64_MAX) {
                        const auto start = ally::recordingStartFrame(
                            takeStartFrame, takeStartClock, recorder.firstCaptureClock(),
                            recorder.firstPacketFrames(), engine.sampleRate(), takeInputLatency,
                            takeOutputLatency, takeGraphLatency, project.automaticRecordLatency,
                            project.recordOffsetFrames);
                        takeStartBeat = double(start - int64_t(recorder.firstPacketOffsetFrames())) /
                                        (engine.sampleRate() * 60.0 / project.bpm);
                    }
                    workspace.recorded(std::move(files), takeStartBeat, recorder.lostFrames());
                    if (recorder.lostFrames()) {
                        workspace.status = "Recording lost " + std::to_string(recorder.lostFrames()) +
                                           " frames; silence inserted";
                    }
                } catch (const std::exception &e) {
                    workspace.status = e.what();
                }
                if (workspace.monitoring) {
                    std::string error;
                    input->start(recorder, workspace.inputConfig, error);
                }
                return;
            }
            if (workspace.recordingMidi) {
                workspace.finishMidi();
                return;
            }
            if (!std::any_of(project.tracks.begin(), project.tracks.end(),
                             [](const auto &t) { return t.armed; })) {
                workspace.status = "Arm one or more tracks before recording";
                return;
            }
            workspace.action("record-audio");
        };
        workspace.startAudioTake = [&](const std::filesystem::path &base) {
            try {
                input->stop();
                std::vector<ally::RecordRoute> routes;
                for (size_t i = 0; i < project.tracks.size(); ++i) {
                    if (project.tracks[i].armed) {
                        auto path = base.parent_path() /
                                    (ally::pathText(base.stem()) + "-track" + std::to_string(i + 1) + ".wav");
                        if (std::filesystem::exists(path)) {
                            throw std::runtime_error("Recording destination exists: " + ally::pathText(path));
                        }
                        routes.push_back({i, project.tracks[i].inputChannel, path});
                    }
                }
                if (routes.empty()) {
                    throw std::runtime_error("No armed recording tracks");
                }
                recorder.start(std::move(routes), false);
                std::string error;
                if (!input->start(recorder, workspace.inputConfig, error)) {
                    const auto unused = recorder.finish();
                    for (const auto &file : unused) {
                        if (!file.frames) {
                            std::error_code cleanupError;
                            std::filesystem::remove(file.path, cleanupError);
                        }
                    }
                    throw std::runtime_error("Native input failed: " + error);
                }
                takeInputLatency = input->stats().inputLatencyMs;
                takeOutputLatency = backend->stats().outputLatencyMs;
                takeGraphLatency = ally::audio::planLatencyFrames(project.compile(engine.sampleRate()));
                takeStartFrame = engine.position();
                takeStartClock = engine.timestamp();
                workspace.audioRecordingState(true);
                engine.play(true);
                recorder.beginCapture();
                workspace.status = "Recording armed tracks";
            } catch (const std::exception &e) {
                workspace.status = e.what();
            }
        };
        if (!project.sourceWarnings().empty()) {
            workspace.status = project.sourceWarnings();
        }
        auto probeStarted = std::chrono::steady_clock::now();
        bool probeStopped = false;
        ally::midi::Input midi(engine);
        workspace.midiStatus = offline ? "MIDI device probing disabled in offline mode" : midi.connectFirst();
        if (!offline) {
            try {
                workspace.midiPorts = midi.ports();
            } catch (const std::exception &e) {
                workspace.midiStatus = e.what();
            }
        }
        workspace.requestMidi = [&](size_t port) { workspace.midiStatus = midi.connect(port); };
        std::cout << "Renderer: SDL3 / Skia Ganesh OpenGL\nAudio: " << workspace.backendStatus << '\n'
                  << workspace.midiStatus << '\n';
        if (!recordProbe.empty()) {
            if (offline || (!recordSeconds && frameLimit && frameLimit < 50)) {
                throw std::runtime_error("Record probe requires native audio and either --record-seconds, "
                                         "automatic completion, or --frames >= 50");
            }
            workspace.action("arm0");
            workspace.action("record");
            probeStarted = std::chrono::steady_clock::now();
            if (!workspace.recordingAudio) {
                throw std::runtime_error("Record probe start failed: " + workspace.status);
            }
        }
        if (autoplay) {
            engine.play(true);
        }
        float observedPeak = 0;
        bool running = true, textActive = false, closePending = false, closeAfterSave = false;
        int frames = 0;
        bool closeVerified = false, keyboardVerified = false;
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
                    workspace.press(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT,
                                    SDL_GetModState() & SDL_KMOD_SHIFT);
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
                        const bool previous = engine.playing();
                        workspace.action("play");
                        if (smoke && event.key.windowID == SDL_GetWindowID(window.get())) {
                            keyboardVerified = engine.playing() != previous;
                        }
                    } else if (key == SDLK_ESCAPE) {
                        workspace.action("stop");
                    } else if (key == SDLK_F5) {
                        workspace.action("piano");
                    } else if (key == SDLK_F8) {
                        workspace.action("browser");
                    } else if (key == SDLK_F9) {
                        workspace.action("mixer");
                    } else if (event.key.mod & SDL_KMOD_CTRL) {
                        if (key == SDLK_A) {
                            workspace.action("select-all");
                        }
                        if (key == SDLK_C) {
                            workspace.action("copy");
                        }
                        if (key == SDLK_V) {
                            workspace.action("paste");
                        }
                        if (key == SDLK_X) {
                            workspace.action("cut");
                        }
                        if (key == SDLK_D) {
                            workspace.action("duplicate");
                        }
                        if (key == SDLK_N) {
                            if (!workspace.modified() ||
                                ask(window.get(), "Ally", "Create a new project and discard current changes?",
                                    "New project", "Keep project") == 1) {
                                workspace.action("new-project");
                            }
                        }
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
                    } else if (key == SDLK_DELETE || key == SDLK_BACKSPACE) {
                        workspace.action("delete");
                    } else if (key == SDLK_R) {
                        workspace.action(event.key.mod & SDL_KMOD_SHIFT ? "record-midi" : "record");
                    } else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) {
                        int direction = key == SDLK_LEFT    ? -1
                                        : key == SDLK_RIGHT ? 1
                                        : key == SDLK_UP    ? 2
                                                            : -2;
                        workspace.action(std::string(event.key.mod & SDL_KMOD_ALT ? "resize:" : "nudge:") +
                                         std::to_string(direction));
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
            midi.route(uint8_t(workspace.selectedTrack()));
            workspace.midiDropped = midi.dropped();
            ally::midi::Message midiEvent;
            while (midi.poll(midiEvent)) {
                workspace.midiMessage(midiEvent);
            }
            if (workspace.monitoring) {
                recorder.monitor(workspace.selectedTrack(), workspace.selectedInputChannel());
            }
            workspace.inputStats = input->stats();
            workspace.recordingQueueHighWater = recorder.queueHighWaterFrames();
            workspace.recordingLostFrames = recorder.lostFrames();
            if (workspace.recordingAudio &&
                (recorder.failed() || recorder.capturedFrames() >= ally::Recorder::maxCaptureFrames)) {
                workspace.requestRecord();
            }
            if ((workspace.recordingAudio || workspace.monitoring) && !input->stats().active) {
                if (workspace.recordingAudio) {
                    workspace.requestRecord();
                }
                workspace.monitoring = false;
                recorder.monitor(-1);
                workspace.status = "Audio input stopped unexpectedly";
            }
            if ((workspace.recordingAudio || workspace.recordingMidi) && !engine.playing()) {
                workspace.action("stop");
            }
            if (closePending && (workspace.recordingAudio || workspace.recordingMidi)) {
                workspace.action("stop");
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
                key.key.windowID = SDL_GetWindowID(window.get());
                key.key.scancode = SDL_SCANCODE_SPACE;
                key.key.key = SDLK_SPACE;
                key.key.down = true;
                if (!SDL_PushEvent(&key)) {
                    throw std::runtime_error("Cannot queue SDL keyboard smoke event");
                }
            }
            if (smoke && frames == 9 && !keyboardVerified) {
                throw std::runtime_error("SDL keyboard transport failed");
            }
            if (smoke && frames == 10) {
                SDL_Event close{};
                close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
                close.window.windowID = SDL_GetWindowID(window.get());
                SDL_PushEvent(&close);
            }
            if (!recordProbe.empty() && !probeStopped &&
                (recordSeconds
                     ? std::chrono::steady_clock::now() - probeStarted >= std::chrono::seconds(recordSeconds)
                     : frames == 35)) {
                workspace.action("stop");
                probeStopped = true;
            }
            if (!recordProbe.empty() && probeStopped && !workspace.busy() && !frameLimit) {
                running = false;
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
        if (!recordProbe.empty()) {
            if (recordSeconds && recorder.capturedFrames() < uint64_t(recordSeconds) * 48000 * 9 / 10) {
                throw std::runtime_error("Native recording stopped before the requested duration");
            }
            if (workspace.busy() || project.tracks[0].clips.empty() ||
                !project.tracks[0].clips.back().audio) {
                throw std::runtime_error("Recording was not imported into the native workspace: " +
                                         workspace.status);
            }
            std::cout << "Native record/import probe: " << recorder.capturedFrames()
                      << " frames, lost=" << recorder.lostFrames()
                      << ", queue_peak=" << recorder.queueHighWaterFrames()
                      << ", compensated_start_beat=" << takeStartBeat << '\n';
        }
        input->stop();
        if (workspace.recordingAudio) {
            workspace.recordingAudio = false;
            recorder.finish();
        }
        workspace.finishMidi();
        const auto diagnostics = backend->stats();
        backend->stop();
        if (device) {
            std::cout << "Audio diagnostics: buffer=" << diagnostics.bufferFrames
                      << " frames, latency=" << diagnostics.outputLatencyMs
                      << " ms, processed=" << diagnostics.processedFrames
                      << ", xruns=" << diagnostics.underruns
                      << ", process_max=" << double(diagnostics.maxProcessNs) / 1e6
                      << " ms, stream_misses=" << engine.streamUnderruns()
                      << ", PDC=" << engine.latencyFrames() << " frames\n";
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
