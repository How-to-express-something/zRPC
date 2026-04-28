#pragma once

#include<functional>
#include<algorithm>
#include<iostream>
//函数闭包 将某些要传递出去的值保存在函数闭包中

template<typename T,typename ...Args>
class FunctionTest
{
public:


	void TakeSomething(int a, int b, std::function<void()>& func)
	{	
		int max = std::max(a, b);
		func = [=]() {
			std::cout << "the bigger one of " << a << " and " << b << " is: " << max << std::endl;
			};
	}

	auto Exeute(Args ...args, std::function<T(args...)> func) -> decltype(func(args...))
	{
		func(args...);
	}


private:

};

