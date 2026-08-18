#include "core/Toolchain.h"

#include "core/Json.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace lw::web2android {
namespace {

std::string ReadText(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open toolchain lock: " + file.u8string());
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void RequireFile(const std::filesystem::path& file, const std::string& label) {
    if (!std::filesystem::is_regular_file(file)) {
        throw std::runtime_error(label + " was not found: " + file.u8string());
    }
}

}  // namespace

ToolchainLock ToolchainLock::Load(const std::filesystem::path& file) {
    const auto json = JsonObject::Parse(ReadText(file));
    ToolchainLock lock;
    lock.schemaVersion = static_cast<int>(json.RequiredInteger("schemaVersion"));
    lock.toolchainVersion = json.RequiredString("toolchainVersion");
    lock.platformApi = static_cast<int>(json.RequiredInteger("platformApi"));
    lock.buildToolsVersion = json.RequiredString("buildToolsVersion");
    lock.runtimeVersion = json.RequiredString("runtimeVersion");
    if (lock.schemaVersion != 1 || lock.platformApi < 23 || lock.buildToolsVersion.empty() ||
        lock.runtimeVersion.empty() || lock.toolchainVersion.empty()) {
        throw std::runtime_error("toolchain.lock.json contains unsupported or incomplete values");
    }
    return lock;
}

AndroidToolchain AndroidToolchain::Resolve(const ToolchainLock& lock,
                                           const std::filesystem::path& sdkOverride,
                                           const std::filesystem::path& javaHomeOverride) {
    AndroidToolchain result;
    result.lock = lock;
    if (!sdkOverride.empty()) {
        result.sdkRoot = std::filesystem::absolute(sdkOverride).lexically_normal();
    } else {
#ifdef _WIN32
        char* environment = nullptr;
        std::size_t environmentSize = 0;
        if (_dupenv_s(&environment, &environmentSize, "ANDROID_SDK_ROOT") == 0 && environment != nullptr) {
            result.sdkRoot = std::filesystem::u8path(environment);
            std::free(environment);
        }
#else
        if (const char* environment = std::getenv("ANDROID_SDK_ROOT")) {
            result.sdkRoot = std::filesystem::u8path(environment);
        }
#endif
        if (result.sdkRoot.empty()) {
            throw std::runtime_error("Android SDK path is required; pass --android-sdk or set ANDROID_SDK_ROOT");
        }
    }

#ifdef _WIN32
    constexpr const char* executableSuffix = ".exe";
#else
    constexpr const char* executableSuffix = "";
#endif
    const auto buildTools = result.sdkRoot / "build-tools" / std::filesystem::u8path(lock.buildToolsVersion);
    result.aapt2 = buildTools / std::filesystem::u8path(std::string("aapt2") + executableSuffix);
    result.zipalign = buildTools / std::filesystem::u8path(std::string("zipalign") + executableSuffix);
    result.apksignerJar = buildTools / "lib" / "apksigner.jar";
    result.androidJar = result.sdkRoot / "platforms" /
                        std::filesystem::u8path("android-" + std::to_string(lock.platformApi)) / "android.jar";
#ifdef _WIN32
    std::filesystem::path javaHome = javaHomeOverride;
    if (javaHome.empty()) {
        wchar_t* environment = nullptr;
        std::size_t environmentSize = 0;
        if (_wdupenv_s(&environment, &environmentSize, L"JAVA_HOME") == 0 && environment != nullptr) {
            javaHome = std::filesystem::path(environment);
            std::free(environment);
        }
    }
    if (javaHome.empty()) throw std::runtime_error("Java runtime is required; pass --java-home or set JAVA_HOME");
    result.java = javaHome / "bin" / "java.exe";
#else
    result.java = javaHomeOverride / "bin" / "java";
#endif
    RequireFile(result.aapt2, "AAPT2");
    RequireFile(result.zipalign, "zipalign");
    RequireFile(result.androidJar, "Android platform JAR");
    RequireFile(result.apksignerJar, "apksigner JAR");
    RequireFile(result.java, "Java runtime");
    return result;
}

}  // namespace lw::web2android
