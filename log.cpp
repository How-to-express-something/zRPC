#include "log.h"
#include <iostream>

Logger* Logger::instance = nullptr;

void Logger::Log()
{
	while(!log_events.empty())
	{
		LogEvent event = log_events.front();
		log_events.pop();
		std::string log_str = event.PrintLog();
		// 将log_str写入日志文件或输出到控制台
		std::cout << log_str << std::endl;
	}
}
