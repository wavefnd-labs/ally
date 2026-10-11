#include <ally/audio/backend.hpp>
#include <chrono>
#include <cstring>
#include <mutex>
#include <pipewire/pipewire.h>
namespace ally::audio {
// Non-RT registry discovery. Capture itself remains the native ALSA implementation.
std::vector<Device> pipewireInputs() {
    static std::once_flag initialized;
    std::call_once(initialized, [] { pw_init(nullptr, nullptr); });
    struct Discovery {
        std::vector<Device> devices;
        int sequence = 0;
        bool done = false;
    } state;
    pw_main_loop *main = pw_main_loop_new(nullptr);
    if (!main) {
        return {};
    }
    pw_context *context = pw_context_new(pw_main_loop_get_loop(main), nullptr, 0);
    pw_core *core = context ? pw_context_connect(context, nullptr, 0) : nullptr;
    if (!core) {
        if (context) {
            pw_context_destroy(context);
        }
        pw_main_loop_destroy(main);
        return {};
    }
    pw_registry *registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
    spa_hook registryHook{}, coreHook{};
    pw_registry_events registryEvents{};
    registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
    registryEvents.global = [](void *user, uint32_t, uint32_t, const char *type, uint32_t,
                               const spa_dict *props) {
        if (!props || std::strcmp(type, PW_TYPE_INTERFACE_Node)) {
            return;
        }
        const char *media = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS),
                   *name = spa_dict_lookup(props, PW_KEY_NODE_NAME),
                   *description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
        if (media && name && std::string_view(media).starts_with("Audio/Source") && !std::strchr(name, ',') &&
            !std::strchr(name, ':')) {
            static_cast<Discovery *>(user)->devices.push_back(
                {"pipewire:NODE=" + std::string(name), description ? description : name});
        }
    };
    pw_core_events coreEvents{};
    coreEvents.version = PW_VERSION_CORE_EVENTS;
    coreEvents.done = [](void *user, uint32_t id, int sequence) {
        auto &s = *static_cast<Discovery *>(user);
        if (id == PW_ID_CORE && sequence == s.sequence) {
            s.done = true;
        }
    };
    coreEvents.error = [](void *user, uint32_t, int, int, const char *) {
        static_cast<Discovery *>(user)->done = true;
    };
    if (registry) {
        pw_registry_add_listener(registry, &registryHook, &registryEvents, &state);
        pw_core_add_listener(core, &coreHook, &coreEvents, &state);
        state.sequence = pw_core_sync(core, PW_ID_CORE, 0);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!state.done && std::chrono::steady_clock::now() < deadline) {
            pw_loop_iterate(pw_main_loop_get_loop(main), 20);
        }
        spa_hook_remove(&coreHook);
        spa_hook_remove(&registryHook);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry));
    }
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(main);
    return state.devices;
}
} // namespace ally::audio
