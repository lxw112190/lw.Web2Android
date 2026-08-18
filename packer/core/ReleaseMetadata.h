#pragma once

#include "core/ProjectConfig.h"

#include <string>

namespace lw::web2android {

struct ReleaseMetadata {
    int schemaVersion = 1;
    std::string appName;
    std::string packageName;
    std::string versionName;
    int versionCode = 1;
    std::string apkFileName;
    std::string apkSha256;
    std::string certificateSha256;
    std::string runtimeVersion;
    std::string toolchainVersion;
    std::string buildTimeUtc;

    std::string ToJson() const;
    std::string ToMarkdown() const;
};

std::string MakeReleaseFileStem(const std::string& appName, const std::string& versionName);
std::string DefaultApkFileName(const ProjectConfig& config);
std::string CurrentUtcTimestamp();

}  // namespace lw::web2android
