#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>

enum class LogLevel {
    Debug   = 0,
    Info    = 1,
    Warning = 2,
    Error   = 3
};

class Logger {
public:
    static Logger& GetInstance();

    void SetLevel(LogLevel level);
    void SetOutputFile(const std::string& filepath);

    void Log(LogLevel level, const std::string& message);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string LevelToString(LogLevel level) const;
    std::string CurrentTimestamp() const;

    LogLevel    current_level_      = LogLevel::Info;
    std::mutex  mutex_;
    std::ofstream file_stream_;
    bool        file_output_enabled_ = false;
};

inline std::string FormatString(const std::string& fmt) {
    return fmt;
}

template<typename ...Args>
std::string FormatString(const std::string& fmt, Args&&... args)
{
    int size = std::snprintf(nullptr, 0, fmt.c_str(),
                             std::forward<Args>(args)...) + 1;
    if (size <= 0) return "";
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, fmt.c_str(),
                  std::forward<Args>(args)...);
    return std::string(buf.get(), buf.get() + size - 1);
}

#define LOG_DEBUG(fmt, ...) \
    do { Logger::GetInstance().Log(LogLevel::Debug,   FormatString(fmt, ##__VA_ARGS__)); } while(0)

#define LOG_INFO(fmt, ...) \
    do { Logger::GetInstance().Log(LogLevel::Info,    FormatString(fmt, ##__VA_ARGS__)); } while(0)

#define LOG_WARN(fmt, ...) \
    do { Logger::GetInstance().Log(LogLevel::Warning, FormatString(fmt, ##__VA_ARGS__)); } while(0)

#define LOG_ERROR(fmt, ...) \
    do { Logger::GetInstance().Log(LogLevel::Error,   FormatString(fmt, ##__VA_ARGS__)); } while(0)
