#include "core/Logging.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <stdexcept>

#ifdef _WIN32
#include <ShlObj.h>
#include <Windows.h>
#endif

namespace lw::web2android {
namespace {

std::atomic<unsigned long long> loggerSequence{0};

std::filesystem::path LocalStateRoot() {
#ifdef _WIN32
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
        throw std::runtime_error("Unable to locate the local application data directory");
    }
    const std::filesystem::path root(localAppData);
    CoTaskMemFree(localAppData);
    return root / L"lw.Web2Android";
#else
    return std::filesystem::temp_directory_path() / "lw.Web2Android";
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

    const auto uniqueName = name + "-" + std::to_string(++loggerSequence);
#ifdef _WIN32
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        file.wstring(), rotation.maxFileSize, rotation.maxFiles);
#else
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        file.string(), rotation.maxFileSize, rotation.maxFiles);
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
    return LocalStateRoot() / "logs" / "packer.log";
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
