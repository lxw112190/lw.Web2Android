#include "gui/GuiProjectModel.h"

#include "core/ProjectValidator.h"

#include <stdexcept>
#include <vector>

namespace lw::web2android::gui {
namespace {

std::filesystem::path Normalize(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal();
}

bool IsEnvironmentRoot(const std::filesystem::path& root) {
    if (!std::filesystem::is_regular_file(root / "toolchain.lock.json")) return false;
    return std::filesystem::is_directory(root / "runtime" / "runtime-v1") ||
           std::filesystem::is_directory(root / "runtime-dist" / "runtime-v1");
}

}  // namespace

GuiEnvironment GuiEnvironment::Discover(const std::filesystem::path& executable,
                                        const std::filesystem::path& currentDirectory) {
    const auto executableDirectory = Normalize(executable).parent_path();
    const std::vector<std::filesystem::path> candidates = {
        executableDirectory.parent_path(),
        Normalize(currentDirectory),
        executableDirectory,
    };
    for (const auto& candidate : candidates) {
        if (!IsEnvironmentRoot(candidate)) continue;
        GuiEnvironment environment;
        environment.applicationRoot = candidate;
        environment.toolchainLock = candidate / "toolchain.lock.json";
        const auto packagedRuntime = candidate / "runtime" / "runtime-v1";
        environment.runtimeDirectory = std::filesystem::is_directory(packagedRuntime)
                                           ? packagedRuntime
                                           : candidate / "runtime-dist" / "runtime-v1";
        return environment;
    }
    throw std::runtime_error(
        "Unable to locate toolchain.lock.json and Runtime Bundle beside the application or in the current directory");
}

ProjectConfig CreateProjectConfig(const GuiProjectInput& input, const GuiEnvironment& environment) {
    if (input.outputDirectory.empty()) throw std::runtime_error("Invalid project: output directory is required");
    if (!input.remote && input.sourceDirectory.empty()) {
        throw std::runtime_error("Invalid project: source directory is required in local mode");
    }
    ProjectConfig config;
    config.schemaVersion = 1;
    config.mode = input.remote ? "remote" : "local";
    config.name = input.name;
    config.packageName = input.packageName;
    config.versionName = input.versionName;
    config.versionCode = input.versionCode;
    config.entry = "index.html";
    config.fullscreen = input.fullscreen;
    config.orientation = input.orientation;
    config.allowHttp = false;
    config.outputDirectory = Normalize(input.outputDirectory);
    config.toolchainLock = Normalize(environment.toolchainLock);
    config.runtimeDirectory = Normalize(environment.runtimeDirectory);
    config.configFile = environment.applicationRoot / "gui-project.json";
    if (input.remote) {
        config.url = input.remoteUrl;
    } else {
        config.source = Normalize(input.sourceDirectory);
    }
    ProjectValidator::Validate(config);
    return config;
}

}  // namespace lw::web2android::gui
