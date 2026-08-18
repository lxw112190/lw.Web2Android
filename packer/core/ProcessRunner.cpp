#include "core/ProcessRunner.h"

#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace lw::web2android {
namespace {

#ifdef _WIN32
std::wstring QuoteWindowsArgument(const std::wstring& argument) {
    if (argument.empty()) return L"\"\"";
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            quoted.append(backslashes * 2U + 1U, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2U, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}
#endif

}  // namespace

std::wstring Utf8ToWide(const std::string& value) {
#ifdef _WIN32
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
#else
    return std::wstring(value.begin(), value.end());
#endif
}

std::string WideToUtf8(const std::wstring& value) {
#ifdef _WIN32
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) throw std::runtime_error("Invalid UTF-16 text");
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
#else
    return std::string(value.begin(), value.end());
#endif
}

void ProcessRunner::Run(const std::filesystem::path& executable,
                        const std::vector<std::wstring>& arguments,
                        const std::filesystem::path& workingDirectory) {
#ifdef _WIN32
    std::wstring commandLine = QuoteWindowsArgument(executable.wstring());
    for (const auto& argument : arguments) {
        commandLine.push_back(L' ');
        commandLine += QuoteWindowsArgument(argument);
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto working = workingDirectory.empty() ? std::wstring() : workingDirectory.wstring();
    const BOOL started = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE, 0,
                                        nullptr, working.empty() ? nullptr : working.c_str(), &startup, &process);
    if (!started) {
        throw std::runtime_error("Unable to start tool (Windows error " + std::to_string(GetLastError()) + "): " +
                                 executable.u8string());
    }
    CloseHandle(process.hThread);
    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    const BOOL readExitCode = GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (waitResult != WAIT_OBJECT_0 || !readExitCode) {
        throw std::runtime_error("Failed while waiting for tool: " + executable.u8string());
    }
    if (exitCode != 0) {
        throw std::runtime_error("Tool exited with code " + std::to_string(exitCode) + ": " + executable.u8string());
    }
#else
    (void)executable;
    (void)arguments;
    (void)workingDirectory;
    throw std::runtime_error("The v0.1 Packer is supported on Windows only");
#endif
}

void Aapt2Runner::Compile(const std::filesystem::path& aapt2,
                          const std::filesystem::path& resources,
                          const std::filesystem::path& outputArchive) {
    ProcessRunner::Run(aapt2, {L"compile", L"--dir", resources.wstring(), L"-o", outputArchive.wstring()});
}

void Aapt2Runner::Link(const std::filesystem::path& aapt2,
                       const std::filesystem::path& androidJar,
                       const std::filesystem::path& manifest,
                       const std::filesystem::path& assets,
                       const std::filesystem::path& compiledResources,
                       const std::filesystem::path& outputApk,
                       int minSdk,
                       int targetSdk,
                       int versionCode,
                       const std::string& versionName) {
    ProcessRunner::Run(aapt2,
                       {L"link", L"-o", outputApk.wstring(), L"--manifest", manifest.wstring(), L"-I",
                        androidJar.wstring(), L"-A", assets.wstring(), L"--min-sdk-version",
                        std::to_wstring(minSdk), L"--target-sdk-version", std::to_wstring(targetSdk),
                        L"--version-code", std::to_wstring(versionCode), L"--version-name", Utf8ToWide(versionName),
                        L"--auto-add-overlay", compiledResources.wstring()});
}

void ZipAlignRunner::AlignAndVerify(const std::filesystem::path& zipalign,
                                    const std::filesystem::path& inputApk,
                                    const std::filesystem::path& outputApk) {
    ProcessRunner::Run(zipalign, {L"-f", L"4", inputApk.wstring(), outputApk.wstring()});
    ProcessRunner::Run(zipalign, {L"-c", L"-v", L"4", outputApk.wstring()});
}

void ApkSignerRunner::Sign(const std::filesystem::path& java,
                           const std::filesystem::path& apksignerJar,
                           const std::filesystem::path& privateKey,
                           const std::filesystem::path& certificate,
                           const std::filesystem::path& inputApk,
                           const std::filesystem::path& outputApk) {
    ProcessRunner::Run(java,
                       {L"-jar", apksignerJar.wstring(), L"sign", L"--key", privateKey.wstring(), L"--cert",
                        certificate.wstring(), L"--v4-signing-enabled", L"false", L"--out", outputApk.wstring(),
                        inputApk.wstring()});
}

void ApkSignerRunner::Verify(const std::filesystem::path& java,
                             const std::filesystem::path& apksignerJar,
                             const std::filesystem::path& apk) {
    ProcessRunner::Run(java,
                       {L"-jar", apksignerJar.wstring(), L"verify", L"--verbose", L"--print-certs", apk.wstring()});
}

}  // namespace lw::web2android
