#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lw::web2android {

struct ArchiveEntrySource {
    std::filesystem::path sourceFile;
    std::string archiveName;
};

class ApkAssembler {
public:
    static void InjectFiles(const std::filesystem::path& resourceApk,
                            const std::vector<std::filesystem::path>& files,
                            const std::filesystem::path& outputApk);
    static void InjectEntries(const std::filesystem::path& sourceApk,
                              const std::vector<ArchiveEntrySource>& entries,
                              const std::filesystem::path& outputApk);
    static std::vector<std::string> ListEntries(const std::filesystem::path& archive);
};

}  // namespace lw::web2android
