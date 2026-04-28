#include "ThreadPool.h"

void ThreadPool::init()
{
	for (size_t i = 0; i < thread_pool.size(); i++)
	{
		thread_pool.at(i) = std::thread(ThreadWorker(i,this));
	}

}

void ThreadPool::shutdown()
{
	is_shut_down = true;
	queue_cv.notify_all();

	for (size_t i = 0; i < thread_pool.size(); i++)
	{
		if (thread_pool[i].joinable())
		{
			thread_pool[i].join();
		}
	}
}
