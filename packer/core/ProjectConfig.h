#pragma once

#include <filesystem>
#include <string>

namespace lw::web2android {

struct ProjectConfig {
    int schemaVersion = 1;
    std::string mode = "local";
    std::string name;
    std::string packageName;
    std::string versionName = "1.0.0";
    int versionCode = 1;
    std::filesystem::path source;
    std::string entry = "index.html";
    std::string url;
    bool fullscreen = false;
    std::string orientation = "auto";
    bool allowHttp = false;
    std::filesystem::path icon;
    std::filesystem::path outputDirectory;
    std::string outputFile;
    std::filesystem::path toolchainLock;
    std::filesystem::path runtimeDirectory;
    std::filesystem::path configFile;

    static ProjectConfig Load(const std::filesystem::path& file);
};

}  // namespace lw::web2android
