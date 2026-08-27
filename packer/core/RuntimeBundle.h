#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lw::web2android {

inline constexpr int kRuntimeConfigSchemaVersion = 2;

std::vector<std::filesystem::path> ValidateRuntimeBundle(
    const std::filesystem::path& runtimeDirectory,
    const std::string& expectedRuntimeVersion);

}  // namespace lw::web2android
