#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lw::web2android {

class ProcessRunner {
public:
    static void Run(const std::filesystem::path& executable,
                    const std::vector<std::wstring>& arguments,
                    const std::filesystem::path& workingDirectory = {});
};

std::wstring Utf8ToWide(const std::string& value);

class Aapt2Runner {
public:
    static void Compile(const std::filesystem::path& aapt2,
                        const std::filesystem::path& resources,
                        const std::filesystem::path& outputArchive);
    static void Link(const std::filesystem::path& aapt2,
                     const std::filesystem::path& androidJar,
                     const std::filesystem::path& manifest,
                     const std::filesystem::path& assets,
                     const std::filesystem::path& compiledResources,
                     const std::filesystem::path& outputApk,
                     int minSdk,
                     int targetSdk,
                     int versionCode,
                     const std::string& versionName);
};

class ZipAlignRunner {
public:
    static void AlignAndVerify(const std::filesystem::path& zipalign,
                               const std::filesystem::path& inputApk,
                               const std::filesystem::path& outputApk);
};

}  // namespace lw::web2android
