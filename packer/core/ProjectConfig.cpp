#include "core/ProjectConfig.h"

#include "core/Json.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace lw::web2android {
namespace {

std::string ReadText(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open project config: " + file.u8string());
    std::ostringstream content;
    content << input.rdbuf();
    auto text = content.str();
    if (text.size() >= 3U && static_cast<unsigned char>(text[0]) == 0xefU &&
        static_cast<unsigned char>(text[1]) == 0xbbU && static_cast<unsigned char>(text[2]) == 0xbfU) {
        text.erase(0, 3);
    }
    return text;
}

std::filesystem::path Resolve(const std::filesystem::path& base, const std::string& value) {
    auto path = std::filesystem::u8path(value);
    if (path.is_relative()) path = base / path;
    return std::filesystem::absolute(path).lexically_normal();
}

int ReadInt(const JsonObject& json, const std::string& key, std::int64_t fallback) {
    const auto value = json.OptionalInteger(key, fallback);
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        throw std::runtime_error("JSON integer is outside the supported range: " + key);
    }
    return static_cast<int>(value);
}

}  // namespace

ProjectConfig ProjectConfig::Load(const std::filesystem::path& file) {
    const auto absoluteFile = std::filesystem::absolute(file).lexically_normal();
    const auto json = JsonObject::Parse(ReadText(absoluteFile));
    ProjectConfig config;
    config.configFile = absoluteFile;
    config.schemaVersion = ReadInt(json, "schemaVersion", 1);
    config.mode = json.OptionalString("mode", "local");
    config.name = json.RequiredString("name");
    config.packageName = json.RequiredString("packageName");
    config.versionName = json.OptionalString("versionName", "1.0.0");
    config.versionCode = ReadInt(json, "versionCode", 1);
    config.entry = json.OptionalString("entry", "index.html");
    config.url = json.OptionalString("url", "");
    config.fullscreen = json.OptionalBoolean("fullscreen", false);
    config.orientation = json.OptionalString("orientation", "auto");
    config.allowHttp = json.OptionalBoolean("allowHttp", false);
    config.outputFile = json.OptionalString("outputFile", "");

    const auto base = absoluteFile.parent_path();
    if (json.Contains("icon")) config.icon = Resolve(base, json.RequiredString("icon"));
    if (json.Contains("source")) config.source = Resolve(base, json.RequiredString("source"));
    config.outputDirectory = Resolve(base, json.OptionalString("output", "output"));
    config.toolchainLock = Resolve(base, json.OptionalString("toolchainLock", "toolchain.lock.json"));
    config.runtimeDirectory = Resolve(base, json.OptionalString("runtime", "toolchain/runtime"));
    return config;
}

}  // namespace lw::web2android
