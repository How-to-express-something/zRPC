#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <atomic>
#include <functional>
#include "ThreadPool.h"

// ==================== 计时工具 ====================
class Timer {
public:
    Timer(const std::string& name) : name_(name),
        start_(std::chrono::high_resolution_clock::now()) {
    }

    ~Timer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        std::cout << "[Timer] " << name_ << " 耗时: "
            << std::fixed << std::setprecision(2)
            << duration.count() / 1000.0 << " ms" << std::endl;
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};

// ==================== 模拟任务 ====================
// 轻量级任务：模拟短时计算
int light_task(int id) {
    volatile int sum = 0;
    for (int i = 0; i < 10000; ++i) {
        sum += i * id;
    }
    return sum;
}

// 中等任务：模拟一般业务逻辑
int medium_task(int id) {
    volatile int sum = 0;
    for (int i = 0; i < 100000; ++i) {
        sum += i * id;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    return sum;
}

// 重量级任务：模拟复杂计算
int heavy_task(int id) {
    volatile int sum = 0;
    for (int i = 0; i < 500000; ++i) {
        sum += i * id;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(500));
    return sum;
}

// ==================== 测试函数 ====================
// 测试1：使用线程池执行任务
void test_thread_pool(int thread_count, int task_count) {
    std::cout << "\n========== 线程池测试 ==========" << std::endl;
    std::cout << "线程数: " << thread_count << ", 任务数: " << task_count << std::endl;

    ThreadPool pool(thread_count);
    pool.init();

    {
        Timer t("线程池执行 " + std::to_string(task_count) + " 个轻量任务");
        std::vector<std::future<int>> futures;

        for (int i = 0; i < task_count; ++i) {
            futures.push_back(pool.submit(light_task, i));
        }

        // 等待所有任务完成
        volatile long long total = 0;
        for (auto& f : futures) {
            total += f.get();
        }
    }

    pool.shutdown();
}

// 测试2：直接创建/销毁线程
void test_raw_threads(int task_count) {
    std::cout << "\n========== 原始线程测试 ==========" << std::endl;
    std::cout << "任务数: " << task_count << std::endl;

    {
        Timer t("直接创建/销毁 " + std::to_string(task_count) + " 个线程");
        std::vector<std::thread> threads;
        std::vector<int> results(task_count);

        for (int i = 0; i < task_count; ++i) {
            threads.emplace_back([i, &results]() {
                results[i] = light_task(i);
                });
        }

        for (auto& t : threads) {
            t.join();
        }

        volatile long long total = 0;
        for (auto r : results) {
            total += r;
        }
    }
}

// 测试3：混合任务类型
void test_mixed_workload() {
    std::cout << "\n========== 混合负载测试 ==========" << std::endl;

    const int THREAD_COUNT = 8;
    const int TASK_COUNT = 100;

    ThreadPool pool(THREAD_COUNT);
    pool.init();

    // 随机选择任务类型
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> task_dist(0, 2);
    std::uniform_int_distribution<int> id_dist(1, 1000);

    {
        Timer t("线程池执行混合任务");
        std::vector<std::future<int>> futures;

        for (int i = 0; i < TASK_COUNT; ++i) {
            int task_type = task_dist(gen);
            int id = id_dist(gen);

            switch (task_type) {
            case 0:
                futures.push_back(pool.submit(light_task, id));
                break;
            case 1:
                futures.push_back(pool.submit(medium_task, id));
                break;
            case 2:
                futures.push_back(pool.submit(heavy_task, id));
                break;
            }
        }

        int completed = 0;
        for (auto& f : futures) {
            try {
                f.get();
                completed++;
            }
            catch (const std::exception& e) {
                std::cerr << "任务异常: " << e.what() << std::endl;
            }
        }
        std::cout << "成功完成: " << completed << " / " << TASK_COUNT << " 个任务" << std::endl;
    }

    pool.shutdown();
}

// 测试4：频繁提交小批量任务（模拟高并发场景）
void test_high_frequency_submit() {
    std::cout << "\n========== 高频提交测试 ==========" << std::endl;

    ThreadPool pool(4);
    pool.init();

    {
        Timer t("高频提交 1000 个任务");
        std::vector<std::future<int>> futures;

        // 模拟高频提交
        for (int batch = 0; batch < 100; ++batch) {
            for (int i = 0; i < 10; ++i) {
                futures.push_back(pool.submit(light_task, batch * 10 + i));
            }
            // 短暂间隔提交下一批
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }

        for (auto& f : futures) {
            f.get();
        }
    }

    pool.shutdown();
}

// 测试5：使用 ref 和返回值两种方式
void test_ref_and_return() {
    std::cout << "\n========== 参数传递测试 ==========" << std::endl;

    ThreadPool pool(2);
    pool.init();

    // 测试引用参数
    int output1 = 0, output2 = 0;
    auto f1 = pool.submit([](int& out, int a, int b) {
        out = a * b;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }, std::ref(output1), 7, 8);

    auto f2 = pool.submit([](int& out, int a, int b) {
        out = a + b;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }, std::ref(output2), 10, 20);

    // 测试返回值
    auto f3 = pool.submit([](int a, int b) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return a * b;
        }, 6, 7);

    auto f4 = pool.submit([](const std::string& s, int n) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return s + std::to_string(n);
        }, std::string("Number"), 42);

    f1.get();
    f2.get();
    int res3 = f3.get();
    std::string res4 = f4.get();

    std::cout << "引用输出1 (7*8): " << output1 << std::endl;
    std::cout << "引用输出2 (10+20): " << output2 << std::endl;
    std::cout << "返回值3 (6*7): " << res3 << std::endl;
    std::cout << "返回值4 (Number+42): " << res4 << std::endl;

    pool.shutdown();
}

// 测试6：线程池重复使用
void test_reuse() {
    std::cout << "\n========== 重复使用测试 ==========" << std::endl;

    ThreadPool pool(4);
    pool.init();

    // 第一轮任务
    {
        Timer t("第一轮 50 个任务");
        std::vector<std::future<int>> futures;
        for (int i = 0; i < 50; ++i) {
            futures.push_back(pool.submit(light_task, i));
        }
        for (auto& f : futures) f.get();
    }

    // 短暂休眠确保上一轮全部完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 第二轮任务（同一个线程池）
    {
        Timer t("第二轮 50 个任务");
        std::vector<std::future<int>> futures;
        for (int i = 0; i < 50; ++i) {
            futures.push_back(pool.submit(medium_task, i));
        }
        for (auto& f : futures) f.get();
    }

    pool.shutdown();
}

// ==================== 主函数 ====================
int main() {
    std::cout << "╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║      线程池性能与功能对比测试             ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝" << std::endl;
    std::cout << "硬件并发数: " << std::thread::hardware_concurrency() << std::endl;

    // 测试1：线程池 vs 原始线程（轻量任务）
    test_thread_pool(8, 1000);
    test_raw_threads(1000);  // 注意：1000个线程可能很慢！

    // 测试2：混合负载
    test_mixed_workload();

    // 测试3：高频提交
    test_high_frequency_submit();

    // 测试4：参数传递方式
    test_ref_and_return();

    // 测试5：线程池重复使用
    test_reuse();

    // 压测：大量任务
    std::cout << "\n========== 压力测试 ==========" << std::endl;
    const int STRESS_THREADS = 16;
    const int STRESS_TASKS = 5000;

    ThreadPool stress_pool(STRESS_THREADS);
    stress_pool.init();

    {
        Timer t("压力测试 " + std::to_string(STRESS_TASKS) + " 个任务");
        std::vector<std::future<int>> futures;

        for (int i = 0; i < STRESS_TASKS; ++i) {
            futures.push_back(stress_pool.submit(light_task, i));
        }

        int completed = 0;
        for (auto& f : futures) {
            try {
                f.get();
                completed++;
            }
            catch (const std::exception& e) {
                std::cerr << "错误: " << e.what() << std::endl;
            }
        }
        std::cout << "成功: " << completed << " / " << STRESS_TASKS << std::endl;
    }

    stress_pool.shutdown();

    std::cout << "\n所有测试完成!" << std::endl;
    return 0;
}