#pragma once

#include "core/ProjectConfig.h"

#include <filesystem>
#include <functional>
#include <string>

namespace lw::web2android {

struct BuildOptions {
    std::filesystem::path androidSdk;
    std::filesystem::path javaHome;
    std::filesystem::path runtimeDirectory;
    std::filesystem::path keysDirectory;
    bool keepWorkingDirectory = false;
    std::function<void(int step, int total, const std::string& name)> progress;
};

struct BuildResult {
    std::filesystem::path apk;
    std::filesystem::path releaseJson;
    std::filesystem::path releaseMarkdown;
    std::filesystem::path workingDirectory;
    std::string certificateSha256;
    std::string apkSha256;
};

class BuildPipeline {
public:
    static BuildResult Build(const ProjectConfig& config, const BuildOptions& options);
};

}  // namespace lw::web2android
