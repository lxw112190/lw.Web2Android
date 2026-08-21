#include "core/ApkAssembler.h"
#include "core/Generators.h"
#include "core/Hash.h"
#include "core/IconGenerator.h"
#include "core/Logging.h"
#include "core/ProjectConfig.h"
#include "core/ProjectValidator.h"
#include "core/ProcessRunner.h"
#include "core/ReleaseMetadata.h"
#include "core/SigningKeyManager.h"
#include "core/Toolchain.h"
#include "gui/GuiProjectModel.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <ncrypt.h>
#include <wincrypt.h>
#endif

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("Test failed: " + message);
}

void WriteBinary(const std::filesystem::path& file, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Unable to create test file");
}

std::vector<std::uint8_t> ReadBinary(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Unable to open test file");
    const auto size = input.tellg();
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("Unable to read test file");
    return bytes;
}

std::vector<std::uint8_t> EmptyZip() {
    return {0x50, 0x4b, 0x05, 0x06, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
}

class TempDirectory {
public:
    TempDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               std::filesystem::u8path("lw-web2android-tests-" + std::to_string(nonce));
        std::filesystem::create_directories(path);
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

void TestProjectAndGenerators(const std::filesystem::path& root) {
    const auto source = root / "web";
    const auto lock = root / "toolchain.lock.json";
    lw::web2android::WriteTextFile(source / "index.html", "<!doctype html><title>ok</title>");
    lw::web2android::WriteTextFile(lock,
                                  "{\"schemaVersion\":1,\"toolchainVersion\":\"test\","
                                  "\"platformApi\":35,\"buildToolsVersion\":\"35.0.0\","
                                  "\"runtimeVersion\":\"1\"}");
    const auto configFile = root / "project.json";
    lw::web2android::WriteTextFile(
        configFile,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"mode\": \"local\",\n"
        "  \"name\": \"Demo \\u4e2d\\u6587 & Web\",\n"
        "  \"packageName\": \"com.example.demo\",\n"
        "  \"versionName\": \"1.0.0\",\n"
        "  \"versionCode\": 1,\n"
        "  \"source\": \"web\",\n"
        "  \"entry\": \"index.html\",\n"
        "  \"fullscreen\": true,\n"
        "  \"orientation\": \"portrait\",\n"
        "  \"allowHttp\": false,\n"
        "  \"toolchainLock\": \"toolchain.lock.json\"\n"
        "}\n");

    const auto config = lw::web2android::ProjectConfig::Load(configFile);
    lw::web2android::ProjectValidator::Validate(config);
    Require(config.name.find("中文") != std::string::npos, "unicode escapes must decode as UTF-8");
    const auto manifest = lw::web2android::ManifestGenerator::Generate(config);
    Require(manifest.find("com.example.demo") != std::string::npos, "manifest package");
    Require(manifest.find("screenOrientation=\"portrait\"") != std::string::npos, "manifest orientation");
    Require(manifest.find("INTERNET") != std::string::npos,
            "local manifest must allow HTTPS downloads and Web requests");
    Require(manifest.find("ACCESS_NETWORK_STATE") != std::string::npos,
            "manifest must allow non-sensitive network diagnostics");
    Require(manifest.find("hardwareAccelerated=\"true\"") != std::string::npos,
            "manifest must enable hardware acceleration for HTML5 video");
    Require(manifest.find("configChanges=\"keyboardHidden|orientation|screenSize\"") !=
                    std::string::npos,
            "manifest must keep the WebView alive across fullscreen video rotation");
    Require(manifest.find("android.intent.action.VIEW") == std::string::npos &&
                manifest.find("android:launchMode=\"singleTop\"") == std::string::npos,
            "default manifest must not register external content integration");
    const auto runtime = lw::web2android::RuntimeConfigGenerator::Generate(config, "1");
    Require(runtime.find("\"schemaVersion\": 2") != std::string::npos,
            "Runtime config schema must be 2");
    Require(runtime.find("\"enabled\": false") != std::string::npos,
            "Runtime external content must be disabled by default");
    Require(runtime.find("\"fullscreen\": true") != std::string::npos, "runtime fullscreen config");
    Require(runtime.find("\"runtimeVersion\": \"1\"") != std::string::npos,
            "runtime version config");
    Require(runtime.find("\"entry\": \"www/index.html\"") != std::string::npos,
            "runtime entry must point to the packaged local web asset");

    const auto resources = root / "generated-res";
    const auto assets = root / "generated-assets";
    lw::web2android::ResourceGenerator::Generate(config, resources);
    lw::web2android::WebAssetManager::Prepare(config, assets, "1");
    Require(manifest.find("android:icon=\"@mipmap/ic_launcher\"") != std::string::npos,
            "manifest must use mipmap launcher icon");
    Require(std::filesystem::is_regular_file(resources / "mipmap-anydpi" / "ic_launcher.xml"),
            "generated default icon");
    const auto defaultIcon = std::filesystem::current_path() / "assets" / "default-app-icon.png";
    if (std::filesystem::is_regular_file(defaultIcon)) {
        const auto iconInfo = lw::web2android::IconGenerator::Inspect(defaultIcon);
        Require(iconInfo.width == iconInfo.height && iconInfo.width >= 192U,
                "bundled default icon must be a supported square PNG");
        const auto customResources = root / "generated-custom-icon-res";
        lw::web2android::IconGenerator::Generate(defaultIcon, customResources);
        for (const auto& density : {"mipmap-mdpi", "mipmap-hdpi", "mipmap-xhdpi",
                                    "mipmap-xxhdpi", "mipmap-xxxhdpi"}) {
            Require(std::filesystem::is_regular_file(customResources / density / "ic_launcher.png"),
                    "custom icon density resource missing");
        }
    }
    Require(std::filesystem::is_regular_file(assets / "www" / "index.html"), "copied web entry");
    const auto generatedRuntimeConfig = ReadBinary(assets / "lw-config.json");
    const std::string generatedRuntimeConfigText(generatedRuntimeConfig.begin(), generatedRuntimeConfig.end());
    Require(generatedRuntimeConfigText.find("\"entry\": \"www/index.html\"") != std::string::npos,
            "prepared Runtime config must match the copied web entry");
    Require(!std::filesystem::exists(assets / "www" / "project.json"), "project config must not become a web asset");

    auto guiConfig = config;
    guiConfig.configFile = source / "gui-project.json";
    const auto guiAssets = root / "generated-gui-assets";
    lw::web2android::WebAssetManager::Prepare(guiConfig, guiAssets, "1");
    Require(std::filesystem::is_regular_file(guiAssets / "www" / "index.html"),
            "a nonexistent GUI project file must not break Web asset copying");

    auto httpConfig = config;
    httpConfig.allowHttp = true;
    const auto httpManifest = lw::web2android::ManifestGenerator::Generate(httpConfig);
    Require(httpManifest.find("INTERNET") != std::string::npos,
            "local HTTP API mode must request INTERNET");
    Require(httpManifest.find("@xml/network_security_config") != std::string::npos,
            "HTTP mode must reference Network Security Config");
    const auto httpResources = root / "generated-http-res";
    lw::web2android::ResourceGenerator::Generate(httpConfig, httpResources);
    Require(std::filesystem::is_regular_file(httpResources / "xml" / "network_security_config.xml"),
            "HTTP mode must generate Network Security Config");
}

void TestExternalContentConfiguration(const std::filesystem::path& root) {
    const auto configFile = root / "external-content-project.json";
    lw::web2android::WriteTextFile(
        configFile,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"mode\": \"local\",\n"
        "  \"name\": \"External Content\",\n"
        "  \"packageName\": \"com.example.externalcontent\",\n"
        "  \"source\": \"web\",\n"
        "  \"toolchainLock\": \"toolchain.lock.json\",\n"
        "  \"externalContent\": {\n"
        "    \"enabled\": true,\n"
        "    \"receiveSharedText\": true,\n"
        "    \"openFiles\": true,\n"
        "    \"preset\": \"text-config\",\n"
        "    \"extensions\": [\" custom \"],\n"
        "    \"fileNames\": [\"Special.Config\"],\n"
        "    \"mimeTypes\": [\"application/x-example-config\"],\n"
        "    \"maxTextBytes\": 1048576\n"
        "  }\n"
        "}\n");
    const auto config = lw::web2android::ProjectConfig::Load(configFile);
    lw::web2android::ProjectValidator::Validate(config);
    Require(config.externalContent.enabled && config.externalContent.receiveSharedText &&
                config.externalContent.openFiles,
            "external content flags must load");
    Require(config.externalContent.maxTextBytes == 1048576U,
            "external content maximum size must load");

    const auto resolved = lw::web2android::ResolveExternalContentConfig(config.externalContent);
    Require(std::find(resolved.extensions.begin(), resolved.extensions.end(), "yaml") != resolved.extensions.end(),
            "text-config preset must include yaml");
    Require(std::find(resolved.extensions.begin(), resolved.extensions.end(), "custom") !=
                resolved.extensions.end(),
            "custom extension must be normalized and merged");
    Require(std::find(resolved.fileNames.begin(), resolved.fileNames.end(), "dockerfile") !=
                resolved.fileNames.end(),
            "text-config preset must include Dockerfile");
    auto codeConfig = config.externalContent;
    codeConfig.preset = "code-config";
    const auto resolvedCode = lw::web2android::ResolveExternalContentConfig(codeConfig);
    Require(std::find(resolvedCode.extensions.begin(), resolvedCode.extensions.end(), "vue") !=
                resolvedCode.extensions.end(),
            "code-config preset must include Vue single-file components");

    const auto manifest = lw::web2android::ManifestGenerator::Generate(config);
    for (const auto* marker : {"android:launchMode=\"singleTop\"", "android.intent.action.VIEW",
                               "android.intent.action.SEND", "android.intent.category.DEFAULT", "text/*",
                               "application/json", "application/yaml", "application/toml", "application/xml"}) {
        Require(manifest.find(marker) != std::string::npos,
                std::string("external content manifest marker missing: ") + marker);
    }
    const auto runtime = lw::web2android::RuntimeConfigGenerator::Generate(config, "6");
    Require(runtime.find("\"schemaVersion\": 2") != std::string::npos,
            "Runtime config schema must be 2");
    Require(runtime.find("\"externalContent\"") != std::string::npos &&
                runtime.find("\"maxTextBytes\": 1048576") != std::string::npos &&
                runtime.find("\"yaml\"") != std::string::npos,
            "Runtime external content policy must be generated");

    auto remote = config;
    remote.mode = "remote";
    remote.source.clear();
    remote.url = "https://example.com";
    bool rejected = false;
    try {
        lw::web2android::ProjectValidator::Validate(remote);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()) ==
                   "External content integration is supported only for local Web projects.";
    }
    Require(rejected, "Remote projects must reject external content integration explicitly");
}

void TestZipAssembler(const std::filesystem::path& root) {
    const auto resourceApk = root / "resources.apk";
    const auto dex = root / "classes.dex";
    const auto webEntry = root / "web" / "index.html";
    const auto chineseEntry = root / std::filesystem::u8path(u8"web/测试页/index.html");
    const auto documentEntry = root / std::filesystem::u8path(u8"web/文件/用户说明.txt");
    const auto japaneseEntry = root / std::filesystem::u8path(u8"web/日本語 空格/案内-✓.txt");
    const auto output = root / "assembled.apk";
    WriteBinary(resourceApk, EmptyZip());
    WriteBinary(dex, {'d', 'e', 'x', '\n', '0', '3', '9', 0, 1, 2, 3, 4});
    lw::web2android::WriteTextFile(webEntry, "<!doctype html>");
    lw::web2android::WriteTextFile(chineseEntry, u8"<title>测试页</title>");
    lw::web2android::WriteTextFile(documentEntry, u8"车辆检测系统");
    lw::web2android::WriteTextFile(japaneseEntry, u8"日本語 Unicode fixture");
    lw::web2android::ApkAssembler::InjectEntries(
        resourceApk,
        {{webEntry, "assets/www/index.html"},
         {chineseEntry, u8"assets/www/测试页/index.html"},
         {documentEntry, u8"assets/www/文件/用户说明.txt"},
         {japaneseEntry, u8"assets/www/日本語 空格/案内-✓.txt"},
         {dex, "classes.dex"}},
        output);
    const auto names = lw::web2android::ApkAssembler::ListEntries(output);
    Require(names.size() == 5U, "Unicode assets and DEX entry count");
    Require(std::find(names.begin(), names.end(), "assets/www/index.html") != names.end(),
            "canonical web asset must be present in assembled ZIP");
    Require(std::find(names.begin(), names.end(), "classes.dex") != names.end(),
            "DEX must be present in assembled ZIP");
    Require(std::find(names.begin(), names.end(), u8"assets/www/测试页/index.html") != names.end(),
            "Chinese directory must be preserved in assembled ZIP");
    Require(std::find(names.begin(), names.end(), u8"assets/www/文件/用户说明.txt") != names.end(),
            "Chinese filename must be preserved in assembled ZIP");
    Require(std::find(names.begin(), names.end(), u8"assets/www/日本語 空格/案内-✓.txt") != names.end(),
            "Japanese, spaces, and special Unicode must be preserved in assembled ZIP");
    Require(std::none_of(names.begin(), names.end(), [](const auto& name) {
                return name.find('\\') != std::string::npos;
            }),
            "all APK entry names must use canonical forward slashes");

    bool duplicateRejected = false;
    try {
        lw::web2android::ApkAssembler::InjectFiles(output, {dex}, root / "duplicate.apk");
    } catch (const std::exception&) {
        duplicateRejected = true;
    }
    Require(duplicateRejected, "duplicate DEX must be rejected");
}

void TestGuiProjectModel(const std::filesystem::path& root) {
    const auto runtime = root / "toolchain" / "runtime";
    std::filesystem::create_directories(runtime);
    const auto environment = lw::web2android::gui::GuiEnvironment::Discover(
        root / "bin" / "lw.Web2Android.GUI.exe", root / "unrelated");
    Require(environment.applicationRoot == root, "GUI must discover its packaged application root");
    Require(environment.runtimeDirectory == runtime, "GUI must prefer the packaged Runtime Bundle");

    lw::web2android::gui::GuiProjectInput local;
    local.sourceDirectory = root / "web";
    local.name = "GUI Demo";
    local.packageName = "com.example.guidemo";
    local.versionName = "2.1.0";
    local.versionCode = 21;
    local.orientation = "portrait";
    local.fullscreen = true;
    local.receiveSharedText = true;
    local.openExternalFiles = true;
    local.externalContentPreset = "text-config";
    local.outputDirectory = root / "gui-output";
    const auto localConfig = lw::web2android::gui::CreateProjectConfig(local, environment);
    Require(localConfig.mode == "local" && localConfig.entry == "index.html", "GUI local project mapping");
    Require(localConfig.source == std::filesystem::absolute(local.sourceDirectory).lexically_normal(),
            "GUI local source normalization");
    Require(localConfig.toolchainLock == root / "toolchain.lock.json", "GUI toolchain lock mapping");
    Require(localConfig.runtimeDirectory == runtime, "GUI Runtime Bundle mapping");
    Require(!localConfig.allowHttp, "GUI must keep HTTP disabled by default");
    Require(localConfig.externalContent.enabled && localConfig.externalContent.receiveSharedText &&
                localConfig.externalContent.openFiles && localConfig.externalContent.preset == "text-config",
            "GUI external content mapping");

    lw::web2android::gui::GuiProjectInput remote = local;
    remote.remote = true;
    remote.sourceDirectory.clear();
    remote.remoteUrl = "https://example.com/app";
    remote.packageName = "com.example.guiremote";
    remote.receiveSharedText = false;
    remote.openExternalFiles = false;
    const auto remoteConfig = lw::web2android::gui::CreateProjectConfig(remote, environment);
    Require(remoteConfig.mode == "remote" && remoteConfig.source.empty(), "GUI remote project mapping");
    Require(remoteConfig.url == remote.remoteUrl, "GUI remote URL mapping");

    remote.remoteUrl = "http://intranet.example.test:9000/app";
    remote.allowHttp = true;
    const auto remoteHttpConfig = lw::web2android::gui::CreateProjectConfig(remote, environment);
    Require(remoteHttpConfig.allowHttp && remoteHttpConfig.url == remote.remoteUrl,
            "GUI trusted intranet HTTP mapping");
}

void TestSigningIdentity(const std::filesystem::path& root) {
    const auto keys = root / "keys";
    const lw::web2android::SigningKeyManager manager(keys);
    const auto first = manager.Resolve("com.example.signing");
    Require(first.newlyCreated, "first signing identity must be newly created");
    Require(first.certificateSha256.size() == 64U, "certificate SHA-256 length");
    Require(std::filesystem::is_regular_file(first.encryptedKeyFile), "encrypted key file");
    Require(std::filesystem::is_regular_file(first.certificateFile), "certificate file");
    Require(std::filesystem::is_regular_file(first.metadataFile), "signing metadata file");

    const auto privateKey = root / "temporary.pk8";
    manager.WriteTemporaryPrivateKey(first, privateKey);
    std::ifstream input(privateKey, std::ios::binary);
    const int firstByte = input.get();
    input.close();
    Require(firstByte == 0x30, "PKCS#8 must start with an ASN.1 sequence");
    lw::web2android::SecureDeleteFile(privateKey);
    Require(!std::filesystem::exists(privateKey), "temporary private key must be deleted");

    const auto second = manager.Resolve("com.example.signing");
    Require(!second.newlyCreated, "existing signing identity must be reused");
    Require(second.certificateSha256 == first.certificateSha256, "same package must keep the same certificate");
    const auto loaded = manager.Load("com.example.signing");
    Require(!loaded.newlyCreated && loaded.certificateSha256 == first.certificateSha256,
            "signing info load must preserve the identity without creating it");

    const auto backup = root / "com.example.signing.pfx";
    const std::wstring password = L"test-backup-password";
    manager.ExportPkcs12(first, backup, password);
    Require(std::filesystem::is_regular_file(backup) && std::filesystem::file_size(backup) > 0U,
            "PKCS#12 backup must be written");
#ifdef _WIN32
    auto pfxBytes = ReadBinary(backup);
    CRYPT_DATA_BLOB pfx{static_cast<DWORD>(pfxBytes.size()), pfxBytes.data()};
    Require(PFXIsPFXBlob(&pfx) == TRUE, "exported backup must be a PKCS#12 blob");
    Require(PFXVerifyPassword(&pfx, password.c_str(), 0) == TRUE, "PKCS#12 password must unlock the backup");
    Require(PFXVerifyPassword(&pfx, L"wrong-password", 0) == FALSE,
            "PKCS#12 backup must reject an incorrect password");
    const HCERTSTORE imported =
        PFXImportCertStore(&pfx, password.c_str(), PKCS12_NO_PERSIST_KEY | PKCS12_ALWAYS_CNG_KSP);
    Require(imported != nullptr, "PKCS#12 backup must import without persisting its key");
    PCCERT_CONTEXT importedCertificate = CertEnumCertificatesInStore(imported, nullptr);
    Require(importedCertificate != nullptr, "PKCS#12 backup must contain its certificate");
    CERT_KEY_CONTEXT keyContext{};
    DWORD keyContextSize = sizeof(keyContext);
    Require(CertGetCertificateContextProperty(importedCertificate, CERT_KEY_CONTEXT_PROP_ID,
                                              &keyContext, &keyContextSize) == TRUE,
            "PKCS#12 backup must contain its non-persisted private key context");
    Require(keyContext.dwKeySpec == CERT_NCRYPT_KEY_SPEC, "PKCS#12 private key must use CNG");
    DWORD algorithmSize = 0;
    Require(NCryptGetProperty(static_cast<NCRYPT_KEY_HANDLE>(keyContext.hCryptProv), NCRYPT_ALGORITHM_PROPERTY,
                              nullptr, 0, &algorithmSize, 0) == ERROR_SUCCESS && algorithmSize > 0U,
            "PKCS#12 private key handle must be usable");
    CertFreeCertificateContext(importedCertificate);
    CertCloseStore(imported, 0);
#endif
    bool overwriteRejected = false;
    try {
        manager.ExportPkcs12(first, backup, password);
    } catch (const std::exception&) {
        overwriteRejected = true;
    }
    Require(overwriteRejected, "PKCS#12 export must not overwrite an existing backup");

    const auto other = manager.Resolve("com.example.other");
    Require(other.certificateSha256 != first.certificateSha256,
            "different packages must receive different signing identities");
}

void TestSha256(const std::filesystem::path& root) {
    const auto file = root / "sha256.txt";
    lw::web2android::WriteTextFile(file, "abc");
    Require(lw::web2android::Sha256File(file) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 must match the standard abc test vector");
}

void TestRotatingLog(const std::filesystem::path& root) {
    const auto distribution = root / "distribution";
    lw::web2android::WriteTextFile(distribution / "toolchain.lock.json", "{}");
    Require(lw::web2android::PackerLogFileForExecutable(distribution / "bin" / "lw.Web2Android.exe") ==
                distribution / "logs" / "packer.log",
            "release log must be stored under the distribution root");
    Require(lw::web2android::PackerLogFileForExecutable(root / "standalone" / "lw.Web2Android.exe") ==
                root / "standalone" / "logs" / "packer.log",
            "standalone log must be stored beside the executable");

    const auto logFile = root / "logs" / "rotation.log";
    lw::web2android::WriteTextFile(logFile, u8"legacy log: 测试应用\n");
    {
        auto logger = lw::web2android::Logger::Rotating(
            "lw.Web2Android.Test", logFile, lw::web2android::LogRotation{512U, 2U});
        logger.Info(u8"Application: 我的网页应用-3 / 车辆检测系统 / 测试应用");
        logger.Flush();
        const auto initialLog = ReadBinary(logFile);
        Require(initialLog.size() >= 3U && initialLog[0] == 0xefU && initialLog[1] == 0xbbU &&
                    initialLog[2] == 0xbfU,
                "new log files must carry a UTF-8 BOM for Windows viewers");
        const std::string initialText(initialLog.begin() + 3, initialLog.end());
        Require(initialText.find(u8"我的网页应用-3") != std::string::npos &&
                    initialText.find(u8"车辆检测系统") != std::string::npos &&
                    initialText.find(u8"测试应用") != std::string::npos &&
                    initialText.find(u8"legacy log: 测试应用") != std::string::npos,
                "existing and new Chinese Packer log text must remain UTF-8");
        for (int index = 0; index < 80; ++index) {
            logger.Info("rotation-test-message-" + std::to_string(index) +
                        "-0123456789012345678901234567890123456789");
        }
        logger.Flush();
    }
    Require(std::filesystem::is_regular_file(logFile), "rotating logger writes the current file");
    Require(std::filesystem::is_regular_file(logFile.parent_path() / "rotation.1.log"),
            "rotating logger creates the first archive");
    Require(!std::filesystem::exists(logFile.parent_path() / "rotation.3.log"),
            "rotating logger honors the archive retention limit");
    for (const auto& rotated : {logFile, logFile.parent_path() / "rotation.1.log"}) {
        const auto bytes = ReadBinary(rotated);
        Require(bytes.size() >= 3U && bytes[0] == 0xefU && bytes[1] == 0xbbU && bytes[2] == 0xbfU,
                "current and rotated log files must start with a UTF-8 BOM");
    }

    Require(lw::web2android::WideToUtf8(L"我的网页应用-3") == u8"我的网页应用-3",
            "Win32 UTF-16 GUI text must convert directly to UTF-8");
    Require(lw::web2android::Utf8ToWide(u8"车辆检测系统") == L"车辆检测系统",
            "UTF-8 Core text must convert directly to Win32 UTF-16");
}

void TestReleaseMetadata() {
    lw::web2android::ProjectConfig config;
    config.name = "Demo: 中文 / Web";
    config.versionName = "1.2.3 beta";
    Require(lw::web2android::DefaultApkFileName(config) == "Demo-中文-Web-1.2.3-beta-android.apk",
            "default APK filename must use a portable app name and version");
    Require(lw::web2android::MakeReleaseFileStem("CON", "1.0") == "_CON-1.0",
            "Windows reserved device names must be escaped");

    const lw::web2android::ReleaseMetadata metadata{1,
                                                    "Demo | Web",
                                                    "com.example.demo",
                                                    "1.2.3",
                                                    12,
                                                    "Demo-1.2.3-android.apk",
                                                    std::string(64, 'a'),
                                                    std::string(64, 'b'),
                                                    "1",
                                                    "0.2.6-1",
                                                    "2026-08-18T01:02:03Z"};
    const auto json = metadata.ToJson();
    Require(json.find("\"toolchainVersion\": \"0.2.6-1\"") != std::string::npos,
            "release JSON must record the toolchain version");
    Require(json.find("\"apkSha256\": \"" + std::string(64, 'a') + "\"") != std::string::npos,
            "release JSON must record the APK digest");
    const auto markdown = metadata.ToMarkdown();
    Require(markdown.find("Demo \\| Web") != std::string::npos, "release Markdown must escape table cells");
    Require(markdown.find("2026-08-18T01:02:03Z") != std::string::npos,
            "release Markdown must record the UTC build time");
}

void TestMinimalToolchainResolution(const std::filesystem::path& root) {
    const auto toolchain = root / "minimal-toolchain";
#ifdef _WIN32
    const auto aapt2 = toolchain / "aapt2.exe";
    const auto zipalign = toolchain / "zipalign.exe";
    const auto java = toolchain / "jre" / "bin" / "java.exe";
#else
    const auto aapt2 = toolchain / "aapt2";
    const auto zipalign = toolchain / "zipalign";
    const auto java = toolchain / "jre" / "bin" / "java";
#endif
    lw::web2android::WriteTextFile(aapt2, "test");
    lw::web2android::WriteTextFile(zipalign, "test");
    lw::web2android::WriteTextFile(java, "test");
    lw::web2android::WriteTextFile(toolchain / "android.jar", "test");
    lw::web2android::WriteTextFile(toolchain / "apksigner" / "apksigner.jar", "test");
    Require(lw::web2android::IsMinimalToolchainDirectory(toolchain), "minimal toolchain layout detection");

    lw::web2android::ToolchainLock lock;
    lock.toolchainVersion = "test-1";
    lock.platformApi = 35;
    lock.buildToolsVersion = "35.0.0";
    lock.runtimeVersion = "1";
    const auto resolved = lw::web2android::AndroidToolchain::Resolve(lock, toolchain);
    Require(resolved.aapt2 == aapt2, "minimal AAPT2 resolution");
    Require(resolved.zipalign == zipalign, "minimal zipalign resolution");
    Require(resolved.androidJar == toolchain / "android.jar", "minimal android.jar resolution");
    Require(resolved.apksignerJar == toolchain / "apksigner" / "apksigner.jar",
            "minimal apksigner resolution");
    Require(resolved.java == java, "minimal JRE resolution");
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        TempDirectory temporary;
        if (argc == 2 && std::string(argv[1]) == "--logging-only") {
            TestRotatingLog(temporary.path);
            std::cout << "Packer rotating log test passed" << std::endl;
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--generators-only") {
            TestProjectAndGenerators(temporary.path);
            TestExternalContentConfiguration(temporary.path);
            std::cout << "Packer generator tests passed" << std::endl;
            return 0;
        }
        TestProjectAndGenerators(temporary.path);
        TestExternalContentConfiguration(temporary.path);
        TestZipAssembler(temporary.path);
        TestGuiProjectModel(temporary.path);
        TestSigningIdentity(temporary.path);
        TestSha256(temporary.path);
        TestRotatingLog(temporary.path);
        TestReleaseMetadata();
        TestMinimalToolchainResolution(temporary.path);
        std::cout << "All Packer tests passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
