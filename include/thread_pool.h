#pragma once
#include <functional>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <thread>
#include <utility>
#include <future>
#include <chrono>
#include "safe_queue.h"
#include "metrics.h"

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

	void init();
	void shutdown();

	template<typename F,typename ...Args>
	auto submit(F&& f, Args&&...args) -> std::future<decltype(f(args...))>
	{
		MetricsCollector::Instance().RecordQueueDepth(task_queue.Size());

		std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

		auto task = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

		auto enqueue_time = std::chrono::steady_clock::now();
		auto wrap_task = [task, enqueue_time]() {
			auto dequeue_time = std::chrono::steady_clock::now();
			int64_t wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
				dequeue_time - enqueue_time).count();
			MetricsCollector::Instance().RecordTaskWaitUs(wait_us);

			(*task)();

			auto done_time = std::chrono::steady_clock::now();
			int64_t exec_us = std::chrono::duration_cast<std::chrono::microseconds>(
				done_time - dequeue_time).count();
			MetricsCollector::Instance().RecordTaskExecUs(exec_us);
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
