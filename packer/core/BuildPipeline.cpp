#include "core/BuildPipeline.h"

#include "core/ApkAssembler.h"
#include "core/Generators.h"
#include "core/ProcessRunner.h"
#include "core/ProjectValidator.h"
#include "core/Toolchain.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace lw::web2android {
namespace {

class WorkspaceGuard {
public:
    WorkspaceGuard(std::filesystem::path path, bool keep) : path_(std::move(path)), keep_(keep) {}
    ~WorkspaceGuard() {
        if (!keep_) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
    }

private:
    std::filesystem::path path_;
    bool keep_;
};

std::string ReadText(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to read file: " + file.u8string());
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void ValidateRuntimeMetadata(const std::filesystem::path& runtimeDirectory, const ToolchainLock& lock) {
    const auto metadataFile = runtimeDirectory / "metadata.json";
    if (!std::filesystem::is_regular_file(metadataFile)) {
        throw std::runtime_error("Runtime metadata was not found: " + metadataFile.u8string());
    }
    const auto metadata = ReadText(metadataFile);
    std::smatch match;
    const std::regex versionPattern("\\\"runtimeVersion\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    if (!std::regex_search(metadata, match, versionPattern) || match[1].str() != lock.runtimeVersion) {
        throw std::runtime_error("Runtime metadata version does not match toolchain.lock.json");
    }
    const std::regex namespacePattern("\\\"namespace\\\"\\s*:\\s*\\\"com\\.lw\\.web2android\\.runtime\\\"");
    if (!std::regex_search(metadata, namespacePattern)) {
        throw std::runtime_error("Runtime metadata contains an unexpected namespace");
    }
}

std::vector<std::filesystem::path> FindRuntimeDexFiles(const std::filesystem::path& directory) {
    if (!std::filesystem::is_directory(directory)) {
        throw std::runtime_error("Runtime directory does not exist: " + directory.u8string());
    }
    const std::regex namePattern("classes([2-9][0-9]*)?\\.dex");
    std::vector<std::pair<int, std::filesystem::path>> indexed;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (!item.is_regular_file()) continue;
        const auto name = item.path().filename().u8string();
        std::smatch match;
        if (!std::regex_match(name, match, namePattern)) continue;
        const int index = match[1].matched ? std::stoi(match[1].str()) : 1;
        indexed.emplace_back(index, item.path());
    }
    std::sort(indexed.begin(), indexed.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    if (indexed.empty() || indexed.front().first != 1) throw std::runtime_error("Runtime classes.dex was not found");

    std::vector<std::filesystem::path> result;
    for (std::size_t position = 0; position < indexed.size(); ++position) {
        const int expected = position == 0 ? 1 : static_cast<int>(position + 1U);
        if (indexed[position].first != expected) throw std::runtime_error("Runtime DEX numbering must be contiguous");
        const auto bytes = ReadText(indexed[position].second);
        if (bytes.size() < 8U || bytes.compare(0, 4, "dex\n") != 0) {
            throw std::runtime_error("Runtime file is not a DEX: " + indexed[position].second.u8string());
        }
        const std::vector<std::string> forbidden = {
            "Lcom/lw/web2android/runtime/R;", "Lcom/lw/web2android/runtime/R$",
            "Lcom/lw/web2android/runtime/BuildConfig;"};
        for (const auto& descriptor : forbidden) {
            if (bytes.find(descriptor) != std::string::npos) {
                throw std::runtime_error("Runtime DEX references a generated application class: " + descriptor);
            }
        }
        result.push_back(indexed[position].second);
    }
    return result;
}

std::filesystem::path CreateWorkspace() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      std::filesystem::u8path("lw-web2android-m2-" + std::to_string(nonce));
    if (!std::filesystem::create_directories(path)) {
        throw std::runtime_error("Unable to create temporary workspace: " + path.u8string());
    }
    return path;
}

std::string DefaultOutputFile(const ProjectConfig& config) {
    return config.packageName + "-" + config.versionName + "-unsigned.apk";
}

void LogStep(int number, const char* name) {
    std::cout << '[' << number << "/11] " << name << std::endl;
}

}  // namespace

BuildResult BuildPipeline::Build(const ProjectConfig& config, const BuildOptions& options) {
    LogStep(1, "Validate project");
    ProjectValidator::Validate(config);

    LogStep(2, "Resolve locked Android toolchain");
    const auto lock = ToolchainLock::Load(config.toolchainLock);
    const auto toolchain = AndroidToolchain::Resolve(lock, options.androidSdk);
    const auto runtimeDirectory = options.runtimeDirectory.empty() ? config.runtimeDirectory :
                                  std::filesystem::absolute(options.runtimeDirectory).lexically_normal();
    ValidateRuntimeMetadata(runtimeDirectory, lock);
    const auto dexFiles = FindRuntimeDexFiles(runtimeDirectory);

    LogStep(3, "Prepare isolated workspace");
    const auto workspace = CreateWorkspace();
    WorkspaceGuard workspaceGuard(workspace, options.keepWorkingDirectory);
    const auto assets = workspace / "assets";
    const auto resources = workspace / "res";
    const auto manifest = workspace / "AndroidManifest.xml";

    LogStep(4, "Copy web assets and generate Runtime config");
    WebAssetManager::Prepare(config, assets);

    LogStep(5, "Generate Android manifest");
    WriteTextFile(manifest, ManifestGenerator::Generate(config));

    LogStep(6, "Generate Android resources");
    ResourceGenerator::Generate(config, resources);

    LogStep(7, "Compile resources with AAPT2");
    const auto compiledResources = workspace / "compiled-resources.zip";
    Aapt2Runner::Compile(toolchain.aapt2, resources, compiledResources);

    LogStep(8, "Link resource APK with AAPT2");
    const auto resourceApk = workspace / "resources.apk";
    Aapt2Runner::Link(toolchain.aapt2, toolchain.androidJar, manifest, assets, compiledResources, resourceApk,
                      23, lock.platformApi, config.versionCode, config.versionName);

    LogStep(9, "Inject precompiled Runtime DEX");
    const auto unalignedApk = workspace / "app-unaligned.apk";
    ApkAssembler::InjectFiles(resourceApk, dexFiles, unalignedApk);

    LogStep(10, "Align and verify unsigned APK");
    std::filesystem::create_directories(config.outputDirectory);
    const auto outputName = config.outputFile.empty() ? DefaultOutputFile(config) : config.outputFile;
    const auto outputApk = config.outputDirectory / std::filesystem::u8path(outputName);
    ZipAlignRunner::AlignAndVerify(toolchain.zipalign, unalignedApk, outputApk);

    LogStep(11, "Publish unsigned APK");
    std::cout << "Unsigned APK: " << outputApk.u8string() << std::endl;
    if (options.keepWorkingDirectory) std::cout << "Workspace: " << workspace.u8string() << std::endl;
    return BuildResult{outputApk, options.keepWorkingDirectory ? workspace : std::filesystem::path{}};
}

}  // namespace lw::web2android
