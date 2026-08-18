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

    static AndroidToolchain Resolve(const ToolchainLock& lock, const std::filesystem::path& sdkOverride);
};

}  // namespace lw::web2android
