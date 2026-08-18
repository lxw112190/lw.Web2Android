#include "core/ReleaseMetadata.h"

#include "core/Json.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace lw::web2android {
namespace {

std::string MarkdownCell(std::string value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '|') escaped += '\\';
        escaped += character == '\r' || character == '\n' ? ' ' : character;
    }
    return escaped;
}

std::string NormalizeFilePart(const std::string& value, const std::string& fallback) {
    std::string result;
    result.reserve(value.size());
    bool separator = false;
    for (const unsigned char character : value) {
        const bool forbidden = character < 0x20U || character == '<' || character == '>' || character == ':' ||
                               character == '"' || character == '/' || character == '\\' || character == '|' ||
                               character == '?' || character == '*' || character == ' ';
        if (forbidden) {
            separator = !result.empty();
            continue;
        }
        if (separator && result.back() != '-') result += '-';
        result += static_cast<char>(character);
        separator = false;
    }
    while (!result.empty() && (result.back() == '.' || result.back() == '-' || result.back() == ' ')) {
        result.pop_back();
    }
    if (result.empty()) result = fallback;

    std::string upper = result;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](const unsigned char character) {
        return static_cast<char>(character >= 'a' && character <= 'z' ? character - ('a' - 'A') : character);
    });
    const auto dot = upper.find('.');
    const auto base = upper.substr(0, dot);
    static const std::unordered_set<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
        "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (reserved.find(base) != reserved.end()) result.insert(result.begin(), '_');
    return result;
}

}  // namespace

std::string ReleaseMetadata::ToJson() const {
    std::ostringstream output;
    output << "{\n"
           << "  \"schemaVersion\": " << schemaVersion << ",\n"
           << "  \"appName\": \"" << EscapeJson(appName) << "\",\n"
           << "  \"packageName\": \"" << EscapeJson(packageName) << "\",\n"
           << "  \"versionName\": \"" << EscapeJson(versionName) << "\",\n"
           << "  \"versionCode\": " << versionCode << ",\n"
           << "  \"apkFileName\": \"" << EscapeJson(apkFileName) << "\",\n"
           << "  \"apkSha256\": \"" << EscapeJson(apkSha256) << "\",\n"
           << "  \"certificateSha256\": \"" << EscapeJson(certificateSha256) << "\",\n"
           << "  \"runtimeVersion\": \"" << EscapeJson(runtimeVersion) << "\",\n"
           << "  \"toolchainVersion\": \"" << EscapeJson(toolchainVersion) << "\",\n"
           << "  \"buildTimeUtc\": \"" << EscapeJson(buildTimeUtc) << "\"\n"
           << "}\n";
    return output.str();
}

std::string ReleaseMetadata::ToMarkdown() const {
    std::ostringstream output;
    output << "# " << MarkdownCell(appName) << " " << MarkdownCell(versionName) << "\n\n"
           << "This file records the reproducible identity of the generated Android package.\n\n"
           << "| Field | Value |\n| --- | --- |\n"
           << "| App Name | " << MarkdownCell(appName) << " |\n"
           << "| Package Name | `" << MarkdownCell(packageName) << "` |\n"
           << "| Version Name | `" << MarkdownCell(versionName) << "` |\n"
           << "| Version Code | " << versionCode << " |\n"
           << "| APK | `" << MarkdownCell(apkFileName) << "` |\n"
           << "| APK SHA-256 | `" << MarkdownCell(apkSha256) << "` |\n"
           << "| Certificate SHA-256 | `" << MarkdownCell(certificateSha256) << "` |\n"
           << "| Runtime Version | `" << MarkdownCell(runtimeVersion) << "` |\n"
           << "| Toolchain Version | `" << MarkdownCell(toolchainVersion) << "` |\n"
           << "| Build Time (UTC) | `" << MarkdownCell(buildTimeUtc) << "` |\n";
    return output.str();
}

std::string MakeReleaseFileStem(const std::string& appName, const std::string& versionName) {
    return NormalizeFilePart(appName, "app") + "-" + NormalizeFilePart(versionName, "0");
}

std::string DefaultApkFileName(const ProjectConfig& config) {
    return MakeReleaseFileStem(config.name, config.versionName) + "-android.apk";
}

std::string CurrentUtcTimestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

}  // namespace lw::web2android
