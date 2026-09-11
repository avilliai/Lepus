#include <fstream>
#pragma once
#include <iostream>
#include <string>
#include <format>
#include <chrono>
#include <mutex>
#include <sstream>

namespace lepus::utils {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void set_level(LogLevel level) {
        level_ = level;
    }

    LogLevel get_level() const {
        return level_;
    }

    template<typename... Args>
    void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
        if (level < level_) return;
        
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif

        char time_str[32];
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

        std::string msg = std::format(fmt, std::forward<Args>(args)...);

        std::lock_guard<std::mutex> lock(mutex_);
        std::string line = std::format("[{}.{:03d}] [{}] {}\n", time_str, ms.count(), level_to_string(level), msg);
        std::cout << line;
        std::cout.flush();
        static std::ofstream log_file("D:/coding/Lepus/bin/lepus.log", std::ios::app);
        if (log_file.is_open()) {
            log_file << line;
            log_file.flush();
        }
    }

private:
    Logger() : level_(LogLevel::Info) {}

    static const char* level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            default:              return "LOG  ";
        }
    }

    LogLevel level_{LogLevel::Info};
    std::mutex mutex_;
};

#define LEPUS_LOG_TRACE(fmt, ...) ::lepus::utils::Logger::instance().log(::lepus::utils::LogLevel::Trace, fmt, ##__VA_ARGS__)
#define LEPUS_LOG_DEBUG(fmt, ...) ::lepus::utils::Logger::instance().log(::lepus::utils::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LEPUS_LOG_INFO(fmt, ...)  ::lepus::utils::Logger::instance().log(::lepus::utils::LogLevel::Info,  fmt, ##__VA_ARGS__)
#define LEPUS_LOG_WARN(fmt, ...)  ::lepus::utils::Logger::instance().log(::lepus::utils::LogLevel::Warn,  fmt, ##__VA_ARGS__)
#define LEPUS_LOG_ERROR(fmt, ...) ::lepus::utils::Logger::instance().log(::lepus::utils::LogLevel::Error, fmt, ##__VA_ARGS__)
#define LEPUS_LOG_FATAL(fmt, ...) ::lepus::utils::Logger::instance().log(::lepus::utils::LogLevel::Fatal, fmt, ##__VA_ARGS__)
#define LEPUS_INFO LEPUS_LOG_INFO
#define LEPUS_WARN LEPUS_LOG_WARN
#define LEPUS_ERROR LEPUS_LOG_ERROR
#define LEPUS_DEBUG LEPUS_LOG_DEBUG
#define LEPUS_TRACE LEPUS_LOG_TRACE

} // namespace lepus::utils
