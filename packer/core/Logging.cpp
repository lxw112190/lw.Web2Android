#include "core/Logging.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace lw::web2android {
namespace {

std::atomic<unsigned long long> loggerSequence{0};

std::filesystem::path CurrentExecutable() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768U, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) return {};
    return std::filesystem::path(std::wstring(buffer.data(), length));
#else
    return {};
#endif
}

void ReportLoggingFailure(const char* message) noexcept {
#ifdef _WIN32
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
#else
    (void)message;
#endif
}

void EnsureUtf8Bom(const std::filesystem::path& file, unsigned long long sequence) noexcept {
    try {
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error) || error ||
            std::filesystem::file_size(file, error) == 0U || error) {
            return;
        }
        std::ifstream input(file, std::ios::binary);
        unsigned char prefix[3]{};
        input.read(reinterpret_cast<char*>(prefix), sizeof(prefix));
        if (input.gcount() == static_cast<std::streamsize>(sizeof(prefix)) &&
            prefix[0] == 0xefU && prefix[1] == 0xbbU && prefix[2] == 0xbfU) {
            return;
        }
        input.clear();
        input.seekg(0);
        auto temporary = file;
        temporary += std::filesystem::u8path(".utf8-migration-" + std::to_string(sequence));
        std::filesystem::remove(temporary, error);
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        constexpr unsigned char bom[] = {0xefU, 0xbbU, 0xbfU};
        output.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        output << input.rdbuf();
        output.close();
        input.close();
        if (!output) {
            std::filesystem::remove(temporary, error);
            return;
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary, error);
        }
#else
        std::filesystem::rename(temporary, file, error);
        if (error) std::filesystem::remove(temporary, error);
#endif
    } catch (...) {
        // Encoding migration is best effort; logging must remain available.
    }
}

}  // namespace

Logger::Logger(std::shared_ptr<spdlog::logger> logger, std::filesystem::path file)
    : logger_(std::move(logger)), file_(std::move(file)) {}

Logger Logger::Rotating(const std::string& name,
                        const std::filesystem::path& file,
                        LogRotation rotation) {
    if (rotation.maxFileSize == 0U || rotation.maxFiles == 0U || rotation.maxFiles > 20U) {
        throw std::runtime_error("Invalid log rotation settings");
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error) throw std::runtime_error("Unable to create log directory: " + error.message());

    const auto sequence = ++loggerSequence;
    const auto uniqueName = name + "-" + std::to_string(sequence);
    EnsureUtf8Bom(file, sequence);
    spdlog::file_event_handlers fileEvents;
    fileEvents.after_open = [](const spdlog::filename_t&, std::FILE* stream) {
        if (std::fseek(stream, 0, SEEK_END) != 0 || std::ftell(stream) != 0L) return;
        constexpr unsigned char bom[] = {0xefU, 0xbbU, 0xbfU};
        if (std::fwrite(bom, 1U, sizeof(bom), stream) != sizeof(bom)) {
            throw std::runtime_error("Unable to write UTF-8 log marker");
        }
        std::fflush(stream);
    };
#ifdef _WIN32
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        file.wstring(), rotation.maxFileSize, rotation.maxFiles, false, fileEvents);
#else
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        file.string(), rotation.maxFileSize, rotation.maxFiles, false, fileEvents);
#endif
    auto logger = std::make_shared<spdlog::logger>(uniqueName, std::move(sink));
    logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [thread %t] %v");
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::info);
    logger->set_error_handler([](const std::string& message) { ReportLoggingFailure(message.c_str()); });
    return Logger(std::move(logger), file);
}

bool Logger::Enabled() const { return static_cast<bool>(logger_); }
const std::filesystem::path& Logger::File() const { return file_; }

void Logger::Debug(const std::string& message) const noexcept {
    try { if (logger_) logger_->log(spdlog::level::debug, message); } catch (...) {}
}
void Logger::Info(const std::string& message) const noexcept {
    try { if (logger_) logger_->log(spdlog::level::info, message); } catch (...) {}
}
void Logger::Warn(const std::string& message) const noexcept {
    try { if (logger_) logger_->log(spdlog::level::warn, message); } catch (...) {}
}
void Logger::Error(const std::string& message) const noexcept {
    try { if (logger_) logger_->log(spdlog::level::err, message); } catch (...) {}
}
void Logger::Flush() const noexcept {
    try { if (logger_) logger_->flush(); } catch (...) {}
}

std::filesystem::path PackerLogFile() {
    return PackerLogFileForExecutable(CurrentExecutable());
}

std::filesystem::path PackerLogFileForExecutable(const std::filesystem::path& executable) {
    auto executableDirectory = executable.empty()
                                   ? std::filesystem::current_path()
                                   : std::filesystem::absolute(executable).lexically_normal().parent_path();
    auto applicationRoot = executableDirectory;
    if (executableDirectory.filename() == "bin" &&
        std::filesystem::is_regular_file(executableDirectory.parent_path() / "toolchain.lock.json")) {
        applicationRoot = executableDirectory.parent_path();
    }
    return applicationRoot / "logs" / "packer.log";
}

Logger& PackerLogger() noexcept {
    static Logger logger = [] {
        try {
            return Logger::Rotating("lw.Web2Android.Packer", PackerLogFile());
        } catch (const std::exception& error) {
            ReportLoggingFailure(error.what());
            return Logger{};
        }
    }();
    return logger;
}

}  // namespace lw::web2android
