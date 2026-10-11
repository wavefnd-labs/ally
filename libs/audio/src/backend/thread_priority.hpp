#pragma once
#include <cstdint>
namespace ally::audio {
// Native thread startup only. Never call IPC or change scheduling from process().
uint32_t requestAudioThreadPriority() noexcept;
} // namespace ally::audio
