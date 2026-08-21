#include "core/ProjectConfig.h"

#include "core/Json.h"

#include <fstream>
#include <algorithm>
#include <cctype>
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

std::uint64_t ReadUnsigned(const JsonObject& json, const std::string& key, std::uint64_t fallback) {
    const auto value = json.OptionalInteger(key, static_cast<std::int64_t>(fallback));
    if (value < 0) throw std::runtime_error("JSON integer must not be negative: " + key);
    return static_cast<std::uint64_t>(value);
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

void AppendCanonical(std::vector<std::string>& destination, const std::vector<std::string>& values) {
    for (const auto& value : values) destination.push_back(Canonical(value));
}

void NormalizeSet(std::vector<std::string>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
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
    if (json.Contains("externalContent")) {
        const auto external = json.RequiredObject("externalContent");
        config.externalContent.enabled = external.OptionalBoolean("enabled", false);
        config.externalContent.receiveSharedText = external.OptionalBoolean("receiveSharedText", false);
        config.externalContent.openFiles = external.OptionalBoolean("openFiles", false);
        config.externalContent.preset = external.OptionalString("preset", "text-config");
        config.externalContent.extensions = external.OptionalStringArray("extensions");
        config.externalContent.fileNames = external.OptionalStringArray("fileNames");
        config.externalContent.mimeTypes = external.OptionalStringArray("mimeTypes");
        config.externalContent.acceptOctetStream =
            external.OptionalBoolean("acceptOctetStream", false);
        config.externalContent.maxTextBytes =
            ReadUnsigned(external, "maxTextBytes", 8U * 1024U * 1024U);
    }

    const auto base = absoluteFile.parent_path();
    if (json.Contains("icon")) config.icon = Resolve(base, json.RequiredString("icon"));
    if (json.Contains("source")) config.source = Resolve(base, json.RequiredString("source"));
    config.outputDirectory = Resolve(base, json.OptionalString("output", "output"));
    config.toolchainLock = Resolve(base, json.OptionalString("toolchainLock", "toolchain.lock.json"));
    config.runtimeDirectory = Resolve(base, json.OptionalString("runtime", "toolchain/runtime"));
    return config;
}

ResolvedExternalContentConfig ResolveExternalContentConfig(const ExternalContentConfig& config) {
    ResolvedExternalContentConfig resolved;
    const auto preset = Canonical(config.preset);
    if (preset == "text-basic" || preset == "text-config" || preset == "code-config") {
        AppendCanonical(resolved.extensions, {"txt", "log", "md", "markdown", "csv"});
        AppendCanonical(resolved.fileNames, {"README", "LICENSE"});
        AppendCanonical(resolved.mimeTypes, {"text/*"});
    }
    if (preset == "text-config" || preset == "code-config") {
        AppendCanonical(resolved.extensions,
                        {"json", "jsonc", "yaml", "yml", "toml", "xml", "ini", "conf", "cfg",
                         "properties", "env"});
        AppendCanonical(resolved.fileNames,
                        {".env", ".env.local", ".env.development", ".env.production", "Dockerfile",
                         "Makefile", "CMakeLists.txt", ".gitignore", ".gitattributes", ".editorconfig"});
        AppendCanonical(resolved.mimeTypes,
                        {"application/json", "application/yaml", "application/toml", "application/xml",
                         "text/xml"});
    }
    if (preset == "code-config") {
        AppendCanonical(resolved.extensions,
                        {"js", "mjs", "cjs", "ts", "tsx", "jsx", "vue", "css", "scss", "less", "html", "htm",
                         "sql", "py", "sh", "bash", "zsh", "fish", "c", "h", "cc", "cpp", "cxx", "hpp",
                         "java", "kt", "kts", "go", "rs", "lua", "gradle"});
    }
    AppendCanonical(resolved.extensions, config.extensions);
    AppendCanonical(resolved.fileNames, config.fileNames);
    AppendCanonical(resolved.mimeTypes, config.mimeTypes);
    NormalizeSet(resolved.extensions);
    NormalizeSet(resolved.fileNames);
    NormalizeSet(resolved.mimeTypes);
    return resolved;
}

}  // namespace lw::web2android
