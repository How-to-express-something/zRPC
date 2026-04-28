#pragma once
#include<functional>
#include<mutex>
#include<condition_variable>
#include<vector>
#include"SafeQueue.h"
#include<thread>
#include<utility>
#include<future>
//====================================================================
//              等待队列
//                 |
//原有线程 ---   线程池（空时同时）--- 临时线程（临时产生销毁）

//等待队列：线程安全的队列



class ThreadPool
{

public:
	class ThreadWorker
	{
	public:
		ThreadWorker(int id_, ThreadPool* tp) :id(id_), ref_thread_pool(tp) {};

		int id;
		ThreadPool* ref_thread_pool;

		void operator()()
		{
			while (!ref_thread_pool->is_shut_down)
			{
				std::unique_lock<std::mutex> lock(ref_thread_pool->m_mutex);
				if (ref_thread_pool->task_queue.isEmpty())
				{
					ref_thread_pool->queue_cv.wait(lock);
				}

				std::function<void()> func;
				if (ref_thread_pool->task_queue.TakeElement(func))
				{
					func();
				}
			}
		}


	};
	ThreadPool(const int& num = 4) :thread_pool(std::vector<std::thread>(num)) {};

	ThreadPool(const ThreadPool&) = delete;
	//​
	//ThreadPool(ThreadPool&&) = delete;
	//​
	//ThreadPool& operator=(const ThreadPool&) = delete;
	//​
	//ThreadPool& operator=(ThreadPool&&) = delete;

	void init();
	void shutdown();

	template<typename F,typename ...Args>
	auto submit(F&& f, Args&&...args) -> std::future<decltype(f(args...))>
	{

		//统一为function<T()>;
		std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...); //完美转发 保持原来的引用

		//统一为function<void()> 通过task无需理会返回值实现
		auto task = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

		auto wrap_task = [task]() {
			(*task)();
			};

		task_queue.Push(wrap_task);
		queue_cv.notify_one();

		return task->get_future();
	}
private:
	SafeQueue<std::function<void()>> task_queue;
	std::mutex m_mutex;
	std::condition_variable queue_cv;
	std::vector<std::thread> thread_pool;
	bool is_shut_down = false;


};


