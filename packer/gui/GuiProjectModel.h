#pragma once

#include "core/ProjectConfig.h"

#include <filesystem>
#include <string>

namespace lw::web2android::gui {

struct GuiEnvironment {
    std::filesystem::path applicationRoot;
    std::filesystem::path toolchainLock;
    std::filesystem::path toolchainDirectory;
    std::filesystem::path runtimeDirectory;
    std::filesystem::path defaultIcon;

    static GuiEnvironment Discover(const std::filesystem::path& executable,
                                   const std::filesystem::path& currentDirectory);
};

struct GuiProjectInput {
    bool remote = false;
    std::filesystem::path sourceDirectory;
    std::string remoteUrl;
    std::string name;
    std::string packageName;
    std::filesystem::path icon;
    std::string versionName = "1.0.0";
    int versionCode = 1;
    std::string orientation = "auto";
    bool fullscreen = false;
    bool allowHttp = false;
    bool receiveSharedText = false;
    bool openExternalFiles = false;
    std::string externalContentPreset = "text-config";
    std::filesystem::path outputDirectory;
};

ProjectConfig CreateProjectConfig(const GuiProjectInput& input, const GuiEnvironment& environment);

}  // namespace lw::web2android::gui
