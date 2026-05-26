#include "log.h"

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_level_ = level;
}

void Logger::SetOutputFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
    file_stream_.open(filepath, std::ios::out | std::ios::app);
    file_output_enabled_ = file_stream_.is_open();
    if (!file_output_enabled_) {
        std::cerr << "[Logger] Warning: failed to open log file: "
                  << filepath << std::endl;
    }
}

std::string Logger::LevelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
    }
    return "UNKNOWN";
}

std::string Logger::CurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream ss;
    ss << std::put_time(std::localtime(&tt), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (level < current_level_) return;

    std::string formatted = "[" + CurrentTimestamp() + "] ["
                          + LevelToString(level) + "] " + message;

    if (level >= LogLevel::Error) {
        std::cerr << formatted << std::endl;
    } else {
        std::cout << formatted << std::endl;
    }

    if (file_output_enabled_ && file_stream_.is_open()) {
        file_stream_ << formatted << std::endl;
    }
}
