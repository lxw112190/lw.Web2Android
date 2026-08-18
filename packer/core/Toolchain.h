#pragma once

#include <filesystem>
#include <string>

namespace lw::web2android {

struct ToolchainLock {
    int schemaVersion = 1;
    std::string toolchainVersion;
    int platformApi = 0;
    std::string buildToolsVersion;
    std::string runtimeVersion;

    static ToolchainLock Load(const std::filesystem::path& file);
};

struct AndroidToolchain {
    ToolchainLock lock;
    std::filesystem::path sdkRoot;
    std::filesystem::path androidJar;
    std::filesystem::path aapt2;
    std::filesystem::path zipalign;
    std::filesystem::path java;
    std::filesystem::path apksignerJar;

    static AndroidToolchain Resolve(const ToolchainLock& lock,
                                    const std::filesystem::path& sdkOverride,
                                    const std::filesystem::path& javaHomeOverride = {});
};

std::filesystem::path DefaultApplicationToolchainDirectory();
bool IsMinimalToolchainDirectory(const std::filesystem::path& directory);

}  // namespace lw::web2android
