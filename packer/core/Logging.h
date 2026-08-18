#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace spdlog {
class logger;
}

namespace lw::web2android {

struct LogRotation {
    std::size_t maxFileSize = 2U * 1024U * 1024U;
    std::size_t maxFiles = 5U;
};

// Thin, non-throwing wrapper around spdlog's synchronous rotating file sink.
class Logger {
public:
    Logger() = default;

    static Logger Rotating(const std::string& name,
                           const std::filesystem::path& file,
                           LogRotation rotation = {});

    bool Enabled() const;
    const std::filesystem::path& File() const;
    void Debug(const std::string& message) const noexcept;
    void Info(const std::string& message) const noexcept;
    void Warn(const std::string& message) const noexcept;
    void Error(const std::string& message) const noexcept;
    void Flush() const noexcept;

private:
    Logger(std::shared_ptr<spdlog::logger> logger, std::filesystem::path file);

    std::shared_ptr<spdlog::logger> logger_;
    std::filesystem::path file_;
};

std::filesystem::path PackerLogFile();
Logger& PackerLogger() noexcept;

}  // namespace lw::web2android
