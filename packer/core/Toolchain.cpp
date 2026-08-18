#include "core/Toolchain.h"

#include "core/Json.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

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

std::filesystem::path EnvironmentPath(const wchar_t* name) {
#ifdef _WIN32
    wchar_t* environment = nullptr;
    std::size_t environmentSize = 0;
    if (_wdupenv_s(&environment, &environmentSize, name) != 0 || environment == nullptr) return {};
    const std::filesystem::path result(environment);
    std::free(environment);
    return result;
#else
    (void)name;
    return {};
#endif
}

}  // namespace

std::filesystem::path DefaultApplicationToolchainDirectory() {
#ifdef _WIN32
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return {};
    executable.resize(length);
    const auto executableDirectory = std::filesystem::path(executable).parent_path();
    if (executableDirectory.filename() == L"bin") return executableDirectory.parent_path() / "toolchain";
    return executableDirectory / "toolchain";
#else
    return {};
#endif
}

bool IsMinimalToolchainDirectory(const std::filesystem::path& directory) {
    if (directory.empty()) return false;
#ifdef _WIN32
    const auto executableSuffix = ".exe";
#else
    const auto executableSuffix = "";
#endif
    return std::filesystem::is_regular_file(directory / std::filesystem::u8path(std::string("aapt2") + executableSuffix)) &&
           std::filesystem::is_regular_file(directory / std::filesystem::u8path(std::string("zipalign") + executableSuffix)) &&
           std::filesystem::is_regular_file(directory / "android.jar") &&
           std::filesystem::is_regular_file(directory / "apksigner" / "apksigner.jar") &&
           std::filesystem::is_regular_file(directory / "jre" / "bin" /
                                             std::filesystem::u8path(std::string("java") + executableSuffix));
}

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
        const auto applicationToolchain = DefaultApplicationToolchainDirectory();
        if (IsMinimalToolchainDirectory(applicationToolchain)) result.sdkRoot = applicationToolchain;
#ifdef _WIN32
        if (result.sdkRoot.empty()) result.sdkRoot = EnvironmentPath(L"ANDROID_SDK_ROOT");
#else
        if (const char* environment = std::getenv("ANDROID_SDK_ROOT")) {
            result.sdkRoot = std::filesystem::u8path(environment);
        }
#endif
        if (result.sdkRoot.empty()) {
            throw std::runtime_error(
                "Minimal toolchain was not found; initialize it or pass --android-sdk / set ANDROID_SDK_ROOT");
        }
    }

#ifdef _WIN32
    constexpr const char* executableSuffix = ".exe";
#else
    constexpr const char* executableSuffix = "";
#endif
    const bool minimal = IsMinimalToolchainDirectory(result.sdkRoot);
    if (minimal) {
        result.aapt2 = result.sdkRoot / std::filesystem::u8path(std::string("aapt2") + executableSuffix);
        result.zipalign = result.sdkRoot / std::filesystem::u8path(std::string("zipalign") + executableSuffix);
        result.apksignerJar = result.sdkRoot / "apksigner" / "apksigner.jar";
        result.androidJar = result.sdkRoot / "android.jar";
    } else {
        const auto buildTools = result.sdkRoot / "build-tools" / std::filesystem::u8path(lock.buildToolsVersion);
        result.aapt2 = buildTools / std::filesystem::u8path(std::string("aapt2") + executableSuffix);
        result.zipalign = buildTools / std::filesystem::u8path(std::string("zipalign") + executableSuffix);
        result.apksignerJar = buildTools / "lib" / "apksigner.jar";
        result.androidJar = result.sdkRoot / "platforms" /
                            std::filesystem::u8path("android-" + std::to_string(lock.platformApi)) / "android.jar";
    }
#ifdef _WIN32
    std::filesystem::path javaHome = javaHomeOverride;
    if (javaHome.empty() && minimal) javaHome = result.sdkRoot / "jre";
    if (javaHome.empty()) javaHome = EnvironmentPath(L"JAVA_HOME");
    if (javaHome.empty()) {
        throw std::runtime_error("Java runtime was not found; initialize the minimal toolchain or pass --java-home");
    }
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
