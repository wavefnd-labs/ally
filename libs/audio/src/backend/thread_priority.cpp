#include "thread_priority.hpp"
#include <algorithm>
#include <memory>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifdef ALLY_RTKIT
#include <systemd/sd-bus.h>
#endif
namespace ally::audio {
uint32_t requestAudioThreadPriority() noexcept {
#ifdef ALLY_RTKIT
    sd_bus *raw = nullptr;
    if (sd_bus_open_system(&raw) >= 0) {
        std::unique_ptr<sd_bus, decltype(&sd_bus_unref)> bus(raw, sd_bus_unref);
        sd_bus_set_method_call_timeout(bus.get(), 500000);
        constexpr auto service = "org.freedesktop.RealtimeKit1";
        constexpr auto path = "/org/freedesktop/RealtimeKit1";
        constexpr auto interface = "org.freedesktop.RealtimeKit1";
        int32_t maximumPriority = 0;
        int64_t maximumTime = 0;
        const auto tid = uint64_t(syscall(SYS_gettid));
        bool realtime = false;
        if (sd_bus_get_property_trivial(bus.get(), service, path, interface, "MaxRealtimePriority", nullptr,
                                        'i', &maximumPriority) >= 0 &&
            sd_bus_get_property_trivial(bus.get(), service, path, interface, "RTTimeUSecMax", nullptr, 'x',
                                        &maximumTime) >= 0 &&
            maximumPriority > 0 && maximumTime >= 50000 && maximumTime <= 1000000) {
            rlimit limit{};
            sched_param normal{};
            if (getrlimit(RLIMIT_RTTIME, &limit) == 0 && limit.rlim_cur >= 50000 && limit.rlim_max >= 50000) {
                limit.rlim_cur = std::min(limit.rlim_cur, rlim_t(maximumTime));
                limit.rlim_max = std::min(limit.rlim_max, rlim_t(maximumTime));
                if (setrlimit(RLIMIT_RTTIME, &limit) == 0 &&
                    sched_setscheduler(0, SCHED_OTHER | SCHED_RESET_ON_FORK, &normal) == 0) {
                    realtime =
                        sd_bus_call_method(bus.get(), service, path, interface, "MakeThreadRealtime", nullptr,
                                           nullptr, "tu", tid, uint32_t(std::min(maximumPriority, 5))) >= 0;
                }
            }
        }
        if (!realtime) {
            int32_t minimumNice = 0;
            if (sd_bus_get_property_trivial(bus.get(), service, path, interface, "MinNiceLevel", nullptr, 'i',
                                            &minimumNice) >= 0 &&
                minimumNice >= -20 && minimumNice < 0) {
                sd_bus_call_method(bus.get(), service, path, interface, "MakeThreadHighPriority", nullptr,
                                   nullptr, "ti", tid, int32_t(std::max(minimumNice, -10)));
            }
        }
    }
#endif
    sched_param actual{};
    const int policy = sched_getscheduler(0) & ~SCHED_RESET_ON_FORK;
    return (policy == SCHED_RR || policy == SCHED_FIFO) && sched_getparam(0, &actual) == 0
               ? uint32_t(actual.sched_priority)
               : 0;
}
} // namespace ally::audio
