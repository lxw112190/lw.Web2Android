#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lw::web2android {

struct ExternalContentConfig {
    bool enabled = false;
    bool receiveSharedText = false;
    bool openFiles = false;
    std::string preset = "text-config";
    std::vector<std::string> extensions;
    std::vector<std::string> fileNames;
    std::vector<std::string> mimeTypes;
    bool acceptOctetStream = false;
    std::uint64_t maxTextBytes = 8U * 1024U * 1024U;
};

struct ResolvedExternalContentConfig {
    std::vector<std::string> extensions;
    std::vector<std::string> fileNames;
    std::vector<std::string> mimeTypes;
};

ResolvedExternalContentConfig ResolveExternalContentConfig(const ExternalContentConfig& config);

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
    ExternalContentConfig externalContent;
    std::filesystem::path icon;
    std::filesystem::path outputDirectory;
    std::string outputFile;
    std::filesystem::path toolchainLock;
    std::filesystem::path runtimeDirectory;
    std::filesystem::path configFile;

    static ProjectConfig Load(const std::filesystem::path& file);
};

}  // namespace lw::web2android
