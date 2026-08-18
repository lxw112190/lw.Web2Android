#pragma once

#include "core/ProjectConfig.h"

#include <filesystem>

namespace lw::web2android {

struct BuildOptions {
    std::filesystem::path androidSdk;
    std::filesystem::path runtimeDirectory;
    bool keepWorkingDirectory = false;
};

struct BuildResult {
    std::filesystem::path apk;
    std::filesystem::path workingDirectory;
};

class BuildPipeline {
public:
    static BuildResult Build(const ProjectConfig& config, const BuildOptions& options);
};

}  // namespace lw::web2android
