#pragma once

#include "core/ProjectConfig.h"

#include <filesystem>
#include <string>

namespace lw::web2android {

class ManifestGenerator {
public:
    static std::string Generate(const ProjectConfig& config);
};

class RuntimeConfigGenerator {
public:
    static std::string Generate(const ProjectConfig& config);
};

class ResourceGenerator {
public:
    static void Generate(const ProjectConfig& config, const std::filesystem::path& resourceDirectory);
};

class WebAssetManager {
public:
    static void Prepare(const ProjectConfig& config, const std::filesystem::path& assetsDirectory);
};

void WriteTextFile(const std::filesystem::path& file, const std::string& content);

}  // namespace lw::web2android
