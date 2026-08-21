#include "core/Generators.h"

#include "core/IconGenerator.h"
#include "core/Json.h"

#include <fstream>
#include <stdexcept>

namespace lw::web2android {
namespace {

std::string MimeDataElements(const std::vector<std::string>& mimeTypes, bool acceptOctetStream) {
    std::string result;
    for (const auto& mimeType : mimeTypes) {
        result += "                <data android:mimeType=\"" + EscapeXml(mimeType) + "\"/>\n";
    }
    if (acceptOctetStream) {
        result += "                <data android:mimeType=\"application/octet-stream\"/>\n";
    }
    return result;
}

std::string ExternalIntentFilters(const ProjectConfig& config) {
    if (!config.externalContent.enabled) return {};
    std::string filters;
    if (config.externalContent.receiveSharedText) {
        filters +=
            "            <intent-filter>\n"
            "                <action android:name=\"android.intent.action.SEND\"/>\n"
            "                <category android:name=\"android.intent.category.DEFAULT\"/>\n"
            "                <data android:mimeType=\"text/plain\"/>\n"
            "            </intent-filter>\n";
    }
    if (config.externalContent.openFiles) {
        const auto resolved = ResolveExternalContentConfig(config.externalContent);
        const auto data = MimeDataElements(resolved.mimeTypes, config.externalContent.acceptOctetStream);
        filters +=
            "            <intent-filter>\n"
            "                <action android:name=\"android.intent.action.VIEW\"/>\n"
            "                <category android:name=\"android.intent.category.DEFAULT\"/>\n" + data +
            "            </intent-filter>\n"
            "            <intent-filter>\n"
            "                <action android:name=\"android.intent.action.SEND\"/>\n"
            "                <category android:name=\"android.intent.category.DEFAULT\"/>\n" + data +
            "            </intent-filter>\n";
    }
    return filters;
}

std::string JsonStringArray(const std::vector<std::string>& values, int indent) {
    if (values.empty()) return "[]";
    const std::string spaces(static_cast<std::size_t>(indent), ' ');
    const std::string itemSpaces(static_cast<std::size_t>(indent + 2), ' ');
    std::string result = "[\n";
    for (std::size_t index = 0; index < values.size(); ++index) {
        result += itemSpaces + "\"" + EscapeJson(values[index]) + "\"";
        result += index + 1U == values.size() ? "\n" : ",\n";
    }
    return result + spaces + "]";
}

}  // namespace

void WriteTextFile(const std::filesystem::path& file, const std::string& content) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Unable to write file: " + file.u8string());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) throw std::runtime_error("Failed while writing file: " + file.u8string());
}

std::string ManifestGenerator::Generate(const ProjectConfig& config) {
    std::string permissions =
        "    <uses-permission android:name=\"android.permission.ACCESS_NETWORK_STATE\"/>\n"
        "    <uses-permission android:name=\"android.permission.INTERNET\"/>\n";
    permissions += "\n";
    std::string orientation;
    if (config.orientation != "auto") {
        orientation = "\n            android:screenOrientation=\"" + config.orientation + "\"";
    }
    const auto networkSecurityConfig = config.allowHttp
        ? "\n        android:networkSecurityConfig=\"@xml/network_security_config\""
        : "";
    const auto launchMode = config.externalContent.enabled
        ? "\n            android:launchMode=\"singleTop\""
        : "";
    const auto externalIntentFilters = ExternalIntentFilters(config);
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
           "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
           "    package=\"" + EscapeXml(config.packageName) + "\">\n\n" +
           permissions +
           "    <application\n"
           "        android:allowBackup=\"false\"\n"
           "        android:hardwareAccelerated=\"true\"\n"
           "        android:icon=\"@mipmap/ic_launcher\"\n"
           "        android:label=\"@string/app_name\"\n"
           "        android:supportsRtl=\"true\"\n"
           "        android:theme=\"@android:style/Theme.Material.Light.NoActionBar\"\n"
           "        android:usesCleartextTraffic=\"" + std::string(config.allowHttp ? "true" : "false") + "\"" +
           networkSecurityConfig + ">\n"
           "        <activity\n"
           "            android:name=\"com.lw.web2android.runtime.MainActivity\"\n"
           "            android:configChanges=\"keyboardHidden|orientation|screenSize\"\n"
           "            android:exported=\"true\"" + launchMode + orientation + ">\n"
           "            <intent-filter>\n"
           "                <action android:name=\"android.intent.action.MAIN\"/>\n"
           "                <category android:name=\"android.intent.category.LAUNCHER\"/>\n"
           "            </intent-filter>\n"
           + externalIntentFilters +
           "        </activity>\n"
           "    </application>\n"
           "</manifest>\n";
}

std::string RuntimeConfigGenerator::Generate(const ProjectConfig& config, const std::string& runtimeVersion) {
    // ProjectConfig::entry is relative to the selected web source directory.
    // Local web files are packaged below assets/www, so the Runtime config must
    // contain the corresponding APK asset path rather than the source path.
    const auto runtimeEntry = config.mode == "local" ? "www/" + config.entry : config.entry;
    const auto resolved = ResolveExternalContentConfig(config.externalContent);
    return "{\n"
           "  \"schemaVersion\": 2,\n"
           "  \"runtimeVersion\": \"" + EscapeJson(runtimeVersion) + "\",\n"
           "  \"mode\": \"" + EscapeJson(config.mode) + "\",\n"
           "  \"entry\": \"" + EscapeJson(runtimeEntry) + "\",\n"
           "  \"url\": \"" + EscapeJson(config.url) + "\",\n"
           "  \"fullscreen\": " + std::string(config.fullscreen ? "true" : "false") + ",\n"
           "  \"orientation\": \"" + EscapeJson(config.orientation) + "\",\n"
           "  \"allowHttp\": " + std::string(config.allowHttp ? "true" : "false") + ",\n"
           "  \"externalContent\": {\n"
           "    \"enabled\": " + std::string(config.externalContent.enabled ? "true" : "false") + ",\n"
           "    \"receiveSharedText\": " +
               std::string(config.externalContent.receiveSharedText ? "true" : "false") + ",\n"
           "    \"openFiles\": " + std::string(config.externalContent.openFiles ? "true" : "false") + ",\n"
           "    \"acceptOctetStream\": " +
               std::string(config.externalContent.acceptOctetStream ? "true" : "false") + ",\n"
           "    \"maxTextBytes\": " + std::to_string(config.externalContent.maxTextBytes) + ",\n"
           "    \"extensions\": " + JsonStringArray(resolved.extensions, 4) + ",\n"
           "    \"fileNames\": " + JsonStringArray(resolved.fileNames, 4) + ",\n"
           "    \"mimeTypes\": " + JsonStringArray(resolved.mimeTypes, 4) + "\n"
           "  }\n"
           "}\n";
}

void ResourceGenerator::Generate(const ProjectConfig& config,
                                 const std::filesystem::path& resourceDirectory,
                                 const std::filesystem::path& defaultIcon) {
    WriteTextFile(resourceDirectory / "values" / "strings.xml",
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<resources>\n"
                  "    <string name=\"app_name\">" + EscapeXml(config.name) + "</string>\n"
                  "</resources>\n");
    const auto icon = config.icon.empty() ? defaultIcon : config.icon;
    if (!icon.empty() && std::filesystem::is_regular_file(icon)) {
        IconGenerator::Generate(icon, resourceDirectory);
    } else {
        WriteTextFile(resourceDirectory / "mipmap-anydpi" / "ic_launcher.xml",
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                      "<vector xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
                      "    android:width=\"108dp\"\n"
                      "    android:height=\"108dp\"\n"
                      "    android:viewportWidth=\"108\"\n"
                      "    android:viewportHeight=\"108\">\n"
                      "    <path android:fillColor=\"#1565C0\" android:pathData=\"M0,0h108v108h-108z\"/>\n"
                      "    <path android:fillColor=\"#FFFFFF\" android:pathData=\"M24,31h60v10h-60zM24,49h60v10h-60zM24,67h42v10h-42z\"/>\n"
                      "</vector>\n");
    }
    if (config.allowHttp) {
        WriteTextFile(resourceDirectory / "xml" / "network_security_config.xml",
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                      "<network-security-config>\n"
                      "    <base-config cleartextTrafficPermitted=\"true\"/>\n"
                      "</network-security-config>\n");
    }
}

void WebAssetManager::Prepare(const ProjectConfig& config,
                              const std::filesystem::path& assetsDirectory,
                              const std::string& runtimeVersion) {
    std::filesystem::create_directories(assetsDirectory);
    WriteTextFile(assetsDirectory / "lw-config.json", RuntimeConfigGenerator::Generate(config, runtimeVersion));
    if (config.mode != "local") return;

    const auto destination = assetsDirectory / "www";
    std::filesystem::create_directories(destination);
    for (const auto& item : std::filesystem::recursive_directory_iterator(config.source)) {
        
        // if (std::filesystem::equivalent(item.path(), config.configFile)) {
        //     continue;
        // }
        if (!config.configFile.empty()) {
            std::error_code ec;
            const bool same =
                std::filesystem::equivalent(item.path(), config.configFile, ec);

            if (!ec && same) {
                continue;
            }
        }

        const auto relative = std::filesystem::relative(item.path(), config.source);
        const auto target = destination / relative;
        if (item.is_symlink()) {
            throw std::runtime_error("Symbolic links are not supported in web assets: " + item.path().u8string());
        }
        if (item.is_directory()) {
            std::filesystem::create_directories(target);
        } else if (item.is_regular_file()) {
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(item.path(), target, std::filesystem::copy_options::overwrite_existing);
        } else {
            throw std::runtime_error("Unsupported web asset type: " + item.path().u8string());
        }
    }
}

}  // namespace lw::web2android
