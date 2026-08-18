#include "core/ApkAssembler.h"
#include "core/Generators.h"
#include "core/Hash.h"
#include "core/ProjectConfig.h"
#include "core/ProjectValidator.h"
#include "core/SigningKeyManager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
    Require(manifest.find("INTERNET") == std::string::npos, "local manifest must not request INTERNET");
    const auto runtime = lw::web2android::RuntimeConfigGenerator::Generate(config);
    Require(runtime.find("\"fullscreen\": true") != std::string::npos, "runtime fullscreen config");

    const auto resources = root / "generated-res";
    const auto assets = root / "generated-assets";
    lw::web2android::ResourceGenerator::Generate(config, resources);
    lw::web2android::WebAssetManager::Prepare(config, assets);
    Require(std::filesystem::is_regular_file(resources / "drawable" / "ic_launcher.xml"), "generated icon");
    Require(std::filesystem::is_regular_file(assets / "www" / "index.html"), "copied web entry");
    Require(!std::filesystem::exists(assets / "www" / "project.json"), "project config must not become a web asset");
}

void TestZipAssembler(const std::filesystem::path& root) {
    const auto resourceApk = root / "resources.apk";
    const auto dex = root / "classes.dex";
    const auto webEntry = root / "web" / "index.html";
    const auto canonicalApk = root / "canonical.apk";
    const auto output = root / "assembled.apk";
    WriteBinary(resourceApk, EmptyZip());
    WriteBinary(dex, {'d', 'e', 'x', '\n', '0', '3', '9', 0, 1, 2, 3, 4});
    lw::web2android::WriteTextFile(webEntry, "<!doctype html>");
    lw::web2android::ApkAssembler::InjectEntries(
        resourceApk, {{webEntry, "assets/www/index.html"}}, canonicalApk);
    lw::web2android::ApkAssembler::InjectFiles(canonicalApk, {dex}, output);
    const auto names = lw::web2android::ApkAssembler::ListEntries(output);
    Require(names.size() == 2U, "canonical asset and DEX entry count");
    Require(std::find(names.begin(), names.end(), "assets/www/index.html") != names.end(),
            "canonical web asset must be present in assembled ZIP");
    Require(std::find(names.begin(), names.end(), "classes.dex") != names.end(),
            "DEX must be present in assembled ZIP");

    bool duplicateRejected = false;
    try {
        lw::web2android::ApkAssembler::InjectFiles(output, {dex}, root / "duplicate.apk");
    } catch (const std::exception&) {
        duplicateRejected = true;
    }
    Require(duplicateRejected, "duplicate DEX must be rejected");
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

}  // namespace

int main() {
    try {
        TempDirectory temporary;
        TestProjectAndGenerators(temporary.path);
        TestZipAssembler(temporary.path);
        TestSigningIdentity(temporary.path);
        TestSha256(temporary.path);
        std::cout << "All Packer tests passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
