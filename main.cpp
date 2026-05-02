#include"log.h"
#include<iostream>

int main()
{
	LOG_INFO("你好 %s, your score is %d", "Alice", 95);

	
	Logger::GetInstance()->Log();
	return 0;
}