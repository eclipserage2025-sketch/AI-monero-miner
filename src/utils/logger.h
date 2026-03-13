#pragma once

#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>

namespace aiminer::utils {

enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERR };

class Logger {
public:
    static void init(LogLevel level, const std::string& file_path = "");
    static Logger& instance();

    template <typename... Args>
    void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
        if (level < level_) return;
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        write(level, msg);
    }

private:
    Logger() = default;
    void write(LogLevel level, std::string_view msg);
    static const char* level_str(LogLevel l);

    LogLevel level_ = LogLevel::INFO;
    std::mutex mtx_;
    std::ofstream file_;
};

}  // namespace aiminer::utils

#define LOG_TRACE(...) ::aiminer::utils::Logger::instance().log(::aiminer::utils::LogLevel::TRACE, __VA_ARGS__)
#define LOG_DEBUG(...) ::aiminer::utils::Logger::instance().log(::aiminer::utils::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  ::aiminer::utils::Logger::instance().log(::aiminer::utils::LogLevel::INFO,  __VA_ARGS__)
#define LOG_WARN(...)  ::aiminer::utils::Logger::instance().log(::aiminer::utils::LogLevel::WARN,  __VA_ARGS__)
#define LOG_ERR(...)   ::aiminer::utils::Logger::instance().log(::aiminer::utils::LogLevel::ERR,   __VA_ARGS__)
