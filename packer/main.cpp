#include "core/BuildPipeline.h"
#include "core/ProcessRunner.h"
#include "core/ProjectConfig.h"
#include "core/ProjectValidator.h"
#include "core/SigningKeyManager.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

void PrintUsage() {
    std::cout <<
        "lw.Web2Android Packer v0.1 (M4)\n\n"
        "Usage:\n"
        "  lw.Web2Android.exe validate <project.json>\n"
        "  lw.Web2Android.exe build <project.json> [options]\n"
        "  lw.Web2Android.exe signing info <package-name> [--keys-dir <directory>]\n"
        "  lw.Web2Android.exe signing export <package-name> <backup.pfx|backup.p12> [--keys-dir <directory>]\n\n"
        "Options:\n"
        "  --android-sdk <directory>  Override ANDROID_SDK_ROOT\n"
        "  --java-home <directory>    Override JAVA_HOME\n"
        "  --runtime <directory>      Override the Runtime Bundle directory\n"
        "  --keys-dir <directory>     Override DPAPI signing identity storage\n"
        "  --keep-work-dir            Keep generated intermediate files\n";
}

std::filesystem::path ParseKeysDirectory(int argc, wchar_t* argv[], int firstOption) {
    std::filesystem::path keysDirectory;
    for (int index = firstOption; index < argc; ++index) {
        if (std::wstring(argv[index]) != L"--keys-dir" || ++index >= argc) {
            throw std::runtime_error("Signing commands only accept --keys-dir <directory>");
        }
        keysDirectory = std::filesystem::path(argv[index]);
    }
    return keysDirectory;
}

class SecretText {
public:
    SecretText() = default;
    SecretText(const SecretText&) = delete;
    SecretText& operator=(const SecretText&) = delete;
    SecretText(SecretText&& other) noexcept : value(std::move(other.value)) {}
    SecretText& operator=(SecretText&&) = delete;
    ~SecretText() {
#ifdef _WIN32
        if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
#endif
    }
    std::wstring value;
};

SecretText ReadPassword(const wchar_t* prompt) {
#ifdef _WIN32
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD originalMode = 0;
    if (input == INVALID_HANDLE_VALUE || input == nullptr || !GetConsoleMode(input, &originalMode)) {
        throw std::runtime_error("A password must be entered in an interactive Windows console");
    }
    std::wcout << prompt << std::flush;
    if (!SetConsoleMode(input, originalMode & ~ENABLE_ECHO_INPUT)) {
        throw std::runtime_error("Unable to disable console password echo");
    }
    SecretText secret;
    try {
        std::getline(std::wcin, secret.value);
        SetConsoleMode(input, originalMode);
    } catch (...) {
        SetConsoleMode(input, originalMode);
        std::wcout << std::endl;
        throw;
    }
    std::wcout << std::endl;
    if (!std::wcin) throw std::runtime_error("Unable to read backup password");
    return secret;
#else
    (void)prompt;
    throw std::runtime_error("Signing identity management is supported on Windows only");
#endif
}

int RunSigningCommand(int argc, wchar_t* argv[]) {
    if (argc < 4) {
        PrintUsage();
        return 2;
    }
    const std::wstring action = argv[2];
    const auto packageName = lw::web2android::WideToUtf8(argv[3]);
    if (action == L"info") {
        const auto keysDirectory = ParseKeysDirectory(argc, argv, 4);
        const lw::web2android::SigningKeyManager manager(keysDirectory);
        const auto identity = manager.Load(packageName);
        std::cout << "Package: " << identity.packageName << '\n'
                  << "Certificate SHA-256: " << identity.certificateSha256 << '\n'
                  << "Created (UTC): " << identity.createdAtUtc << '\n'
                  << "Directory: " << identity.directory.u8string() << std::endl;
        return 0;
    }
    if (action == L"export") {
        if (argc < 5) throw std::runtime_error("signing export requires a .pfx or .p12 destination");
        const std::filesystem::path destination = argv[4];
        auto extension = destination.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        if (extension != L".pfx" && extension != L".p12") {
            throw std::runtime_error("PKCS#12 backup destination must end in .pfx or .p12");
        }
        const auto keysDirectory = ParseKeysDirectory(argc, argv, 5);
        const lw::web2android::SigningKeyManager manager(keysDirectory);
        const auto identity = manager.Load(packageName);
        auto password = ReadPassword(L"Backup password (minimum 8 characters): ");
        if (password.value.size() < 8U) throw std::runtime_error("Backup password must contain at least 8 characters");
        auto confirmation = ReadPassword(L"Confirm backup password: ");
        if (password.value != confirmation.value) throw std::runtime_error("Backup passwords do not match");
        manager.ExportPkcs12(identity, destination, password.value);
        std::cout << "PKCS#12 backup created: " << std::filesystem::absolute(destination).u8string() << '\n'
                  << "Certificate SHA-256: " << identity.certificateSha256 << std::endl;
        return 0;
    }
    throw std::runtime_error("Unknown signing command; expected info or export");
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
        if (command == L"signing") return RunSigningCommand(argc, argv);
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
