#include "core/ProjectValidator.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <vector>

namespace lw::web2android {
namespace {

bool IsPackageSegment(const std::string& segment) {
    if (segment.empty() || !std::isalpha(static_cast<unsigned char>(segment.front()))) return false;
    return std::all_of(segment.begin() + 1, segment.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '_';
    });
}

bool IsValidPackage(const std::string& value) {
    std::size_t start = 0;
    int segments = 0;
    while (start <= value.size()) {
        const auto end = value.find('.', start);
        const auto segment = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!IsPackageSegment(segment)) return false;
        ++segments;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return segments >= 2;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("Invalid project: " + message);
}

}  // namespace

void ProjectValidator::Validate(const ProjectConfig& config) {
    Require(config.schemaVersion == 1, "schemaVersion must be 1");
    Require(config.mode == "local" || config.mode == "remote", "mode must be local or remote");
    Require(!config.name.empty(), "name is required");
    Require(config.name.size() <= 128U, "name must not exceed 128 UTF-8 bytes");
    Require(IsValidPackage(config.packageName), "packageName must contain at least two valid Java identifiers");
    Require(config.packageName != "com.lw.web2android.runtime" &&
                !StartsWith(config.packageName, "com.lw.web2android.runtime."),
            "packageName must not overlap the fixed Runtime namespace");
    Require(!config.versionName.empty() && config.versionName.size() <= 64U,
            "versionName must contain 1 to 64 UTF-8 bytes");
    Require(config.versionCode >= 1 && config.versionCode <= 2'100'000'000,
            "versionCode must be between 1 and 2100000000");
    Require(config.orientation == "auto" || config.orientation == "portrait" || config.orientation == "landscape",
            "orientation must be auto, portrait, or landscape");
    Require(std::filesystem::exists(config.toolchainLock) && std::filesystem::is_regular_file(config.toolchainLock),
            "toolchainLock does not exist: " + config.toolchainLock.u8string());

    if (!config.outputFile.empty()) {
        const auto outputName = std::filesystem::u8path(config.outputFile);
        Require(outputName.filename() == outputName && outputName.extension() == ".apk",
                "outputFile must be an APK filename without directory components");
    }

    if (config.mode == "local") {
        Require(!config.source.empty(), "source is required in local mode");
        Require(std::filesystem::exists(config.source) && std::filesystem::is_directory(config.source),
                "source directory does not exist: " + config.source.u8string());
        const auto entryPath = std::filesystem::u8path(config.entry);
        Require(!config.entry.empty() && entryPath.is_relative(), "entry must be a relative path");
        Require(std::none_of(entryPath.begin(), entryPath.end(), [](const auto& component) { return component == ".."; }),
                "entry must not leave the source directory");
        Require(std::filesystem::is_regular_file(config.source / entryPath),
                "entry file does not exist under source: " + config.entry);
        Require(config.url.empty(), "url must be empty in local mode");
    } else {
        Require(config.source.empty(), "source must be omitted in remote mode");
        Require(StartsWith(config.url, "https://") || (config.allowHttp && StartsWith(config.url, "http://")),
                "remote url must use HTTPS unless allowHttp is true");
    }
}

}  // namespace lw::web2android
