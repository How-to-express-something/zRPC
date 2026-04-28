#include"log.h"
#include<iostream>

int main()
{
	LOG_INFO("Hello %s, your score is %d", "Alice", 95);

	
	Logger::GetInstance()->Log();
	return 0;
}