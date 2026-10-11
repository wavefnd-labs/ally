#pragma once
#include <ally/audio/engine.hpp>
#include <filesystem>
namespace ally {
std::shared_ptr<audio::SampleStream> openStream(const std::filesystem::path &path);
}
