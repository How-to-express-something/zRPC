#pragma once

#include<queue>
#include<mutex>
//

template<typename T>
class SafeQueue
{
public:



	bool isEmpty()
	{
		std::unique_lock<std::mutex> lock(q_mutex);
		return safe_queue.empty();
	}


	int Size()
	{
		std::unique_lock<std::mutex> lock(q_mutex);
		return safe_queue.size();
	}

	bool TakeElement(T& target)
	{	
		std::unique_lock<std::mutex> lock(q_mutex);
		if (safe_queue.empty()) return false;
		target = std::move(safe_queue.front());
		safe_queue.pop();
		return true;
	}

	void Push(const T& target)
	{
		std::unique_lock<std::mutex> lock(q_mutex);
		safe_queue.push(target);
	}

private:
	std::mutex q_mutex;
	std::queue<T> safe_queue;

};

