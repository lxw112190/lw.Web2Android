#include "core/BuildPipeline.h"
#include "core/ProjectConfig.h"
#include "core/ProjectValidator.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

void PrintUsage() {
    std::cout <<
        "lw.Web2Android Packer v0.1 (M3)\n\n"
        "Usage:\n"
        "  lw.Web2Android.exe validate <project.json>\n"
        "  lw.Web2Android.exe build <project.json> [options]\n\n"
        "Options:\n"
        "  --android-sdk <directory>  Override ANDROID_SDK_ROOT\n"
        "  --java-home <directory>    Override JAVA_HOME\n"
        "  --runtime <directory>      Override the Runtime Bundle directory\n"
        "  --keys-dir <directory>     Override DPAPI signing identity storage\n"
        "  --keep-work-dir            Keep generated intermediate files\n";
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    try {
        if (argc < 2 || std::wstring(argv[1]) == L"--help" || std::wstring(argv[1]) == L"-h") {
            PrintUsage();
            return argc < 2 ? 2 : 0;
        }
        const std::wstring command = argv[1];
        if ((command != L"build" && command != L"validate") || argc < 3) {
            PrintUsage();
            return 2;
        }
        const auto config = lw::web2android::ProjectConfig::Load(std::filesystem::path(argv[2]));
        if (command == L"validate") {
            if (argc != 3) throw std::runtime_error("validate does not accept build options");
            lw::web2android::ProjectValidator::Validate(config);
            std::cout << "Project is valid: " << config.configFile.u8string() << std::endl;
            return 0;
        }

        lw::web2android::BuildOptions options;
        for (int index = 3; index < argc; ++index) {
            const std::wstring option = argv[index];
            if (option == L"--android-sdk" || option == L"--java-home" || option == L"--runtime" ||
                option == L"--keys-dir") {
                if (++index >= argc) throw std::runtime_error("Missing value for build option");
                if (option == L"--android-sdk") options.androidSdk = std::filesystem::path(argv[index]);
                else if (option == L"--java-home") options.javaHome = std::filesystem::path(argv[index]);
                else if (option == L"--runtime") options.runtimeDirectory = std::filesystem::path(argv[index]);
                else options.keysDirectory = std::filesystem::path(argv[index]);
            } else if (option == L"--keep-work-dir") {
                options.keepWorkingDirectory = true;
            } else {
                throw std::runtime_error("Unknown build option");
            }
        }
        lw::web2android::BuildPipeline::Build(config, options);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << std::endl;
        return 1;
    }
}
