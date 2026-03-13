#include "utils/logger.h"

#include <ctime>

namespace aiminer::utils {

void Logger::init(LogLevel level, const std::string& file_path) {
    auto& self = instance();
    self.level_ = level;
    if (!file_path.empty()) {
        self.file_.open(file_path, std::ios::app);
    }
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::write(LogLevel level, std::string_view msg) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    char time_buf[64];
    std::tm tm_buf{};

#if defined(_MSC_VER)
    // MSVC: localtime_s has (tm*, time_t*) parameter order
    localtime_s(&tm_buf, &time_t_now);
#elif defined(__STDC_LIB_EXT1__) || defined(_POSIX_C_SOURCE) || defined(__linux__) || defined(__APPLE__)
    localtime_r(&time_t_now, &tm_buf);
#else
    // Fallback: mutex already held above, use localtime with our lock
    {
        std::tm* tmp = std::localtime(&time_t_now);
        if (tmp) tm_buf = *tmp;
    }
#endif

    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    std::lock_guard lock(mtx_);
    auto line = std::format("[{}] [{}] {}\n", time_buf, level_str(level), msg);
    std::cout << line;
    if (file_.is_open()) {
        file_ << line;
        file_.flush();
    }
}

const char* Logger::level_str(LogLevel l) {
    switch (l) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERR:   return "ERROR";
    }
    return "?????";
}

}  // namespace aiminer::utils
