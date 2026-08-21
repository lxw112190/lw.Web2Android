#include "core/ProjectValidator.h"

#include "core/IconGenerator.h"

#include <algorithm>
#include <cctype>
#include <regex>
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

std::string Canonical(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    value = first < last ? std::string(first, last) : std::string{};
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

void ValidateExternalContent(const ProjectConfig& config) {
    const auto& external = config.externalContent;
    if (external.enabled && config.mode == "remote") {
        throw std::runtime_error("External content integration is supported only for local Web projects.");
    }
    if (!external.enabled) return;

    Require(external.receiveSharedText || external.openFiles,
            "externalContent must enable receiveSharedText or openFiles");
    const auto preset = Canonical(external.preset);
    Require(preset == "text-basic" || preset == "text-config" || preset == "code-config",
            "externalContent.preset must be text-basic, text-config, or code-config");
    Require(external.maxTextBytes >= 64U * 1024U && external.maxTextBytes <= 32U * 1024U * 1024U,
            "externalContent.maxTextBytes must be between 65536 and 33554432");

    static const std::regex extensionPattern("^[a-z0-9][a-z0-9._+\\-]*$");
    for (const auto& raw : external.extensions) {
        const auto extension = Canonical(raw);
        Require(!extension.empty() && extension.find("..") == std::string::npos && extension.front() != '.' &&
                    extension.find('/') == std::string::npos && extension.find('\\') == std::string::npos &&
                    extension.find('\0') == std::string::npos && std::regex_match(extension, extensionPattern),
                "externalContent.extensions contains an invalid extension");
    }
    for (const auto& raw : external.fileNames) {
        const auto fileName = Canonical(raw);
        Require(!fileName.empty() && fileName != "." && fileName != ".." &&
                    fileName.find('/') == std::string::npos && fileName.find('\\') == std::string::npos &&
                    fileName.find('\0') == std::string::npos,
                "externalContent.fileNames must contain base names only");
    }

    static const std::regex mimePattern("^[a-z0-9!#$&^_.+\\-]+/(?:[a-z0-9!#$&^_.+\\-]+|\\*)$");
    for (const auto& raw : external.mimeTypes) {
        const auto mimeType = Canonical(raw);
        Require(mimeType != "*/*" && std::regex_match(mimeType, mimePattern),
                "externalContent.mimeTypes contains an invalid or overly broad MIME type");
    }
}

}  // namespace

void ProjectValidator::ValidatePackageName(const std::string& packageName) {
    Require(IsValidPackage(packageName), "packageName must contain at least two valid Java identifiers");
    Require(packageName != "com.lw.web2android.runtime" &&
                !StartsWith(packageName, "com.lw.web2android.runtime."),
            "packageName must not overlap the fixed Runtime namespace");
}

void ProjectValidator::Validate(const ProjectConfig& config) {
    Require(config.schemaVersion == 1, "schemaVersion must be 1");
    Require(config.mode == "local" || config.mode == "remote", "mode must be local or remote");
    Require(!config.name.empty(), "name is required");
    Require(config.name.size() <= 128U, "name must not exceed 128 UTF-8 bytes");
    ValidatePackageName(config.packageName);
    Require(!config.versionName.empty() && config.versionName.size() <= 64U,
            "versionName must contain 1 to 64 UTF-8 bytes");
    Require(config.versionCode >= 1 && config.versionCode <= 2'100'000'000,
            "versionCode must be between 1 and 2100000000");
    Require(config.orientation == "auto" || config.orientation == "portrait" || config.orientation == "landscape",
            "orientation must be auto, portrait, or landscape");
    Require(std::filesystem::exists(config.toolchainLock) && std::filesystem::is_regular_file(config.toolchainLock),
            "toolchainLock does not exist: " + config.toolchainLock.u8string());
    if (!config.icon.empty()) {
        auto extension = config.icon.extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        Require(extension == ".png", "icon file must use the .png extension");
        IconGenerator::Inspect(config.icon);
    }

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
    ValidateExternalContent(config);
}

}  // namespace lw::web2android
