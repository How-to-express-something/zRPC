#pragma once

#include<string>
#include<memory>
#include<cstdio>
#include<utility>
#include<queue>

template<typename ...Args>
std::string  FormatString(const std::string& fmt, Args&&... args)
{
	// 计算格式化后的字符串长度
	int size = std::snprintf(nullptr, 0, fmt.c_str(), std::forward<Args>(args)...) + 1; // +1 for null terminator
	if (size <= 0) {
		return ""; // 格式化失败，返回空字符串
	}
	// 创建一个足够大的缓冲区来存储格式化后的字符串
	std::unique_ptr<char[]> buf(new char[size]);
	std::snprintf(buf.get(), size, fmt.c_str(), std::forward<Args>(args)...);
	return std::string(buf.get(), buf.get() + size - 1); //
}


enum LogLevel
{
	LogLevel_Debug,
	LogLevel_Info,
	LogLevel_Warning,
	LogLevel_Error,
};


// 获取信息的类，包含日志级别、日志消息等属性，以及打印日志信息的函数
class LogEvent
{
private:

	std::string log_message;

public:
	LogEvent(LogLevel level, const std::string& message)
	{
		log_message = "[" + log_level_to_string(level) + "] " + message;
	}

	std::string log_level_to_string(LogLevel level) const
	{
		switch (level)
		{
		case LogLevel_Debug:
			return "DEBUG";
		case LogLevel_Info:
			return "INFO";
		case LogLevel_Warning:
			return "WARNING";
		case LogLevel_Error:
			return "ERROR";
		default:
			return "UNKNOWN";
		}
	}

	std::string PrintLog() const
	{
		return log_message;
	}


};


//对日志进行管理的类，包含日志事件队列、日志写入函数等功能
class Logger
{
public:
	
	

	template<typename ...Args>
	void Info(const std::string& message, Args&&... args)
	{
		log_events.emplace(LogLevel_Info, FormatString(message, std::forward<Args>(args)...));
	}

	void Log();

	static Logger* GetInstance()
	{
		if (!instance) {
			instance = new Logger;
		}
		return instance;
	}

private:
	Logger() = default;
	~Logger() = default;
	std::queue<LogEvent> log_events;
	static Logger* instance;
	LogLevel current_log_level = LogLevel_Info;
};



#define LOG_INFO(message, ...) Logger::GetInstance()->Info(message, ##__VA_ARGS__)
