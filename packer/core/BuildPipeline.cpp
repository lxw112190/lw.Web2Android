#include "core/BuildPipeline.h"

#include "core/ApkAssembler.h"
#include "core/Generators.h"
#include "core/Hash.h"
#include "core/Logging.h"
#include "core/ProcessRunner.h"
#include "core/ProjectValidator.h"
#include "core/ReleaseMetadata.h"
#include "core/SigningKeyManager.h"
#include "core/Toolchain.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

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

class SensitiveFileGuard {
public:
    explicit SensitiveFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
    ~SensitiveFileGuard() { DeleteNow(); }
    void DeleteNow() {
        if (!deleted_) {
            SecureDeleteFile(path_);
            deleted_ = true;
        }
    }

private:
    std::filesystem::path path_;
    bool deleted_ = false;
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

std::vector<ArchiveEntrySource> CollectApplicationEntries(
    const std::filesystem::path& assetsDirectory,
    const std::vector<std::filesystem::path>& dexFiles) {
    std::vector<ArchiveEntrySource> entries;
    for (const auto& item : std::filesystem::recursive_directory_iterator(assetsDirectory)) {
        if (!item.is_regular_file()) continue;
        const auto relative = std::filesystem::relative(item.path(), assetsDirectory).generic_u8string();
        entries.push_back({item.path(), "assets/" + relative});
    }
    for (const auto& dex : dexFiles) {
        entries.push_back({dex, dex.filename().u8string()});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.archiveName < right.archiveName;
    });
    return entries;
}

std::filesystem::path CreateWorkspace() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      std::filesystem::u8path("lw-web2android-build-" + std::to_string(nonce));
    if (!std::filesystem::create_directories(path)) {
        throw std::runtime_error("Unable to create temporary workspace: " + path.u8string());
    }
    return path;
}

void LogStep(const BuildOptions& options, int number, const char* name) {
    std::cout << '[' << number << "/15] " << name << std::endl;
    PackerLogger().Info("[" + std::to_string(number) + "/15] " + name);
    if (options.progress) options.progress(number, 15, name);
}

void PublishOutput(const std::filesystem::path& verifiedApk, const std::filesystem::path& outputApk) {
    std::filesystem::create_directories(outputApk.parent_path());
    const auto partial = outputApk.parent_path() /
                         std::filesystem::u8path(outputApk.filename().u8string() + ".partial");
    std::error_code ignored;
    std::filesystem::remove(partial, ignored);
    std::filesystem::copy_file(verifiedApk, partial, std::filesystem::copy_options::overwrite_existing);
#ifdef _WIN32
    if (!MoveFileExW(partial.c_str(), outputApk.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        std::filesystem::remove(partial, ignored);
        throw std::runtime_error("Unable to publish signed APK (Windows error " + std::to_string(error) + ")");
    }
#else
    std::filesystem::remove(outputApk, ignored);
    std::filesystem::rename(partial, outputApk);
#endif
}

}  // namespace

BuildResult BuildPipeline::Build(const ProjectConfig& config, const BuildOptions& options) {
    auto& log = PackerLogger();
    const auto started = std::chrono::steady_clock::now();
    log.Info("Packaging started");
    log.Info("Application: " + config.name + " (" + config.packageName + ")");
    log.Info("Mode: " + config.mode + ", version: " + config.versionName +
             " (" + std::to_string(config.versionCode) + ")");
    if (config.mode == "local") log.Info("Source: " + config.source.u8string());
    try {
    LogStep(options, 1, "Validate project");
    ProjectValidator::Validate(config);

    LogStep(options, 2, "Resolve locked Android toolchain");
    const auto lock = ToolchainLock::Load(config.toolchainLock);
    const auto toolchain = AndroidToolchain::Resolve(lock, options.androidSdk, options.javaHome);
    log.Debug("Toolchain root: " + toolchain.sdkRoot.u8string());
    auto runtimeDirectory = options.runtimeDirectory.empty() ? config.runtimeDirectory :
                            std::filesystem::absolute(options.runtimeDirectory).lexically_normal();
    const auto minimalRuntime = toolchain.sdkRoot / "runtime";
    if (!std::filesystem::is_directory(runtimeDirectory) && std::filesystem::is_directory(minimalRuntime)) {
        runtimeDirectory = minimalRuntime;
    }
    ValidateRuntimeMetadata(runtimeDirectory, lock);
    const auto dexFiles = FindRuntimeDexFiles(runtimeDirectory);

    LogStep(options, 3, "Prepare isolated workspace");
    const auto workspace = CreateWorkspace();
    log.Debug("Workspace: " + workspace.u8string());
    WorkspaceGuard workspaceGuard(workspace, options.keepWorkingDirectory);
    const auto assets = workspace / "assets";
    const auto resources = workspace / "res";
    const auto manifest = workspace / "AndroidManifest.xml";

    LogStep(options, 4, "Copy web assets and generate Runtime config");
    WebAssetManager::Prepare(config, assets, lock.runtimeVersion);

    LogStep(options, 5, "Generate Android manifest");
    WriteTextFile(manifest, ManifestGenerator::Generate(config));

    LogStep(options, 6, "Generate Android resources");
    ResourceGenerator::Generate(config, resources);

    LogStep(options, 7, "Compile resources with AAPT2");
    const auto compiledResources = workspace / "compiled-resources.zip";
    Aapt2Runner::Compile(toolchain.aapt2, resources, compiledResources);

    LogStep(options, 8, "Link resource APK with AAPT2");
    const auto resourceApk = workspace / "resources.apk";
    Aapt2Runner::Link(toolchain.aapt2, toolchain.androidJar, manifest, compiledResources, resourceApk,
                      23, lock.platformApi, config.versionCode, config.versionName);

    LogStep(options, 9, "Inject Web assets and precompiled Runtime DEX");
    const auto unalignedApk = workspace / "app-unaligned.apk";
    const auto applicationEntries = CollectApplicationEntries(assets, dexFiles);
    ApkAssembler::InjectEntries(resourceApk, applicationEntries, unalignedApk);
    log.Info("Injected " + std::to_string(applicationEntries.size()) +
             " UTF-8 canonical Web asset and Runtime entries");

    LogStep(options, 10, "Align unsigned APK");
    const auto alignedApk = workspace / "app-aligned.apk";
    ZipAlignRunner::AlignAndVerify(toolchain.zipalign, unalignedApk, alignedApk);

    LogStep(options, 11, "Resolve package signing identity");
    const SigningKeyManager keyManager(options.keysDirectory);
    const auto identity = keyManager.Resolve(config.packageName);
    std::cout << (identity.newlyCreated ? "Created" : "Reused") << " signing identity: "
              << identity.certificateSha256 << std::endl;
    log.Info(std::string(identity.newlyCreated ? "Created" : "Reused") +
             " signing identity: " + identity.certificateSha256);

    LogStep(options, 12, "Sign APK");
    const auto temporaryKey = workspace / "signing-key.pk8";
    SensitiveFileGuard sensitiveKey(temporaryKey);
    keyManager.WriteTemporaryPrivateKey(identity, temporaryKey);
    const auto signedApk = workspace / "app-signed.apk";
    ApkSignerRunner::Sign(toolchain.java, toolchain.apksignerJar, temporaryKey, identity.certificateFile,
                          alignedApk, signedApk);
    sensitiveKey.DeleteNow();

    LogStep(options, 13, "Verify APK signature");
    ApkSignerRunner::Verify(toolchain.java, toolchain.apksignerJar, signedApk);

    LogStep(options, 14, "Publish APK and release metadata");
    const auto outputName = config.outputFile.empty() ? lw::web2android::DefaultApkFileName(config) : config.outputFile;
    const auto outputApk = config.outputDirectory / std::filesystem::u8path(outputName);
    PublishOutput(signedApk, outputApk);
    const auto apkSha256 = Sha256File(outputApk);
    WriteTextFile(std::filesystem::path(outputApk.wstring() + L".sha256"),
                  apkSha256 + "  " + outputApk.filename().u8string() + "\n");
    const auto releaseStem = outputApk.parent_path() / outputApk.stem();
    const auto releaseJson = std::filesystem::path(releaseStem.wstring() + L".release.json");
    const auto releaseMarkdown = std::filesystem::path(releaseStem.wstring() + L"-RELEASE.md");
    const ReleaseMetadata metadata{1,
                                   config.name,
                                   config.packageName,
                                   config.versionName,
                                   config.versionCode,
                                   outputApk.filename().u8string(),
                                   apkSha256,
                                   identity.certificateSha256,
                                   lock.runtimeVersion,
                                   lock.toolchainVersion,
                                   CurrentUtcTimestamp()};
    WriteTextFile(releaseJson, metadata.ToJson());
    WriteTextFile(releaseMarkdown, metadata.ToMarkdown());

    LogStep(options, 15, "Complete");
    std::cout << "Signed APK: " << outputApk.u8string() << std::endl;
    std::cout << "APK SHA-256: " << apkSha256 << std::endl;
    std::cout << "Certificate SHA-256: " << identity.certificateSha256 << std::endl;
    if (options.keepWorkingDirectory) std::cout << "Workspace: " << workspace.u8string() << std::endl;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    log.Info("Signed APK: " + outputApk.u8string());
    log.Info("APK SHA-256: " + apkSha256);
    log.Info("Packaging completed in " + std::to_string(elapsed.count()) + " ms");
    log.Flush();
    return BuildResult{outputApk,
                       releaseJson,
                       releaseMarkdown,
                       options.keepWorkingDirectory ? workspace : std::filesystem::path{},
                       identity.certificateSha256,
                       apkSha256};
    } catch (const std::exception& error) {
        log.Error(std::string("Packaging failed: ") + error.what());
        log.Flush();
        throw;
    } catch (...) {
        log.Error("Packaging failed with an unknown exception");
        log.Flush();
        throw;
    }
}

}  // namespace lw::web2android
