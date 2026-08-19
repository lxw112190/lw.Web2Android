#include "core/Generators.h"

#include "core/Json.h"

#include <fstream>
#include <stdexcept>

namespace lw::web2android {

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
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
           "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
           "    package=\"" + EscapeXml(config.packageName) + "\">\n\n" +
           permissions +
           "    <application\n"
           "        android:allowBackup=\"false\"\n"
           "        android:hardwareAccelerated=\"true\"\n"
           "        android:icon=\"@drawable/ic_launcher\"\n"
           "        android:label=\"@string/app_name\"\n"
           "        android:supportsRtl=\"true\"\n"
           "        android:theme=\"@android:style/Theme.Material.Light.NoActionBar\"\n"
           "        android:usesCleartextTraffic=\"" + std::string(config.allowHttp ? "true" : "false") + "\"" +
           networkSecurityConfig + ">\n"
           "        <activity\n"
           "            android:name=\"com.lw.web2android.runtime.MainActivity\"\n"
           "            android:configChanges=\"keyboardHidden|orientation|screenSize\"\n"
           "            android:exported=\"true\"" + orientation + ">\n"
           "            <intent-filter>\n"
           "                <action android:name=\"android.intent.action.MAIN\"/>\n"
           "                <category android:name=\"android.intent.category.LAUNCHER\"/>\n"
           "            </intent-filter>\n"
           "        </activity>\n"
           "    </application>\n"
           "</manifest>\n";
}

std::string RuntimeConfigGenerator::Generate(const ProjectConfig& config, const std::string& runtimeVersion) {
    // ProjectConfig::entry is relative to the selected web source directory.
    // Local web files are packaged below assets/www, so the Runtime config must
    // contain the corresponding APK asset path rather than the source path.
    const auto runtimeEntry = config.mode == "local" ? "www/" + config.entry : config.entry;
    return "{\n"
           "  \"schemaVersion\": 1,\n"
           "  \"runtimeVersion\": \"" + EscapeJson(runtimeVersion) + "\",\n"
           "  \"mode\": \"" + EscapeJson(config.mode) + "\",\n"
           "  \"entry\": \"" + EscapeJson(runtimeEntry) + "\",\n"
           "  \"url\": \"" + EscapeJson(config.url) + "\",\n"
           "  \"fullscreen\": " + std::string(config.fullscreen ? "true" : "false") + ",\n"
           "  \"orientation\": \"" + EscapeJson(config.orientation) + "\",\n"
           "  \"allowHttp\": " + std::string(config.allowHttp ? "true" : "false") + "\n"
           "}\n";
}

void ResourceGenerator::Generate(const ProjectConfig& config, const std::filesystem::path& resourceDirectory) {
    WriteTextFile(resourceDirectory / "values" / "strings.xml",
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<resources>\n"
                  "    <string name=\"app_name\">" + EscapeXml(config.name) + "</string>\n"
                  "</resources>\n");
    WriteTextFile(resourceDirectory / "drawable" / "ic_launcher.xml",
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<vector xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
                  "    android:width=\"108dp\"\n"
                  "    android:height=\"108dp\"\n"
                  "    android:viewportWidth=\"108\"\n"
                  "    android:viewportHeight=\"108\">\n"
                  "    <path android:fillColor=\"#1565C0\" android:pathData=\"M0,0h108v108h-108z\"/>\n"
                  "    <path android:fillColor=\"#FFFFFF\" android:pathData=\"M24,31h60v10h-60zM24,49h60v10h-60zM24,67h42v10h-42z\"/>\n"
                  "</vector>\n");
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
