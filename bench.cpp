#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <event2/thread.h>

#include "coder.h"
#include "protocol.h"
#include "rpcserver.h"
#include "rpcclient.h"
#include "person.pb.h"
#include "log.h"
#include "metrics.h"

struct BenchConfig {
    int server_workers = 4;
    int client_threads = 8;
    int duration_sec   = 10;
    int warmup_sec     = 1;
    short server_port  = 9001;
};

struct ThreadResult {
    int64_t requests  = 0;
    int64_t errors    = 0;
    int64_t elapsed_us = 0;
};

void run_benchmark_server(short port, int workers, std::atomic<bool>& ready) {
    RPCServer server(port, "127.0.0.1", workers);

    server.register_typedhandler<AddRequest, AddResponse>(
        "Calculator", "Add",
        [](const AddRequest& req, AddResponse& resp, std::string*) -> bool {
            resp.set_result(req.a() + req.b());
            return true;
        });

    LOG_INFO("[Bench] Server starting on port %d with %d workers...", port, workers);
    ready.store(true);
    server.start();
}

void client_worker(int thread_id, short port, int duration_sec,
                   std::atomic<bool>& start_flag, ThreadResult& result) {
    while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    RpcClient client;
    if (!client.Connect("127.0.0.1", port)) {
        LOG_ERROR("[Bench] Client %d: connect failed", thread_id);
        return;
    }

    int64_t count = 0;
    int64_t errors = 0;
    auto bench_start = std::chrono::steady_clock::now();
    auto deadline = bench_start + std::chrono::seconds(duration_sec);

    while (std::chrono::steady_clock::now() < deadline) {
        AddRequest req;
        req.set_a(thread_id * 1000 + count);
        req.set_b(count);

        AddResponse resp;
        auto rpc_resp = client.Call("Calculator", "Add", req, resp,
                                     SerializationType::Protobuf, 5000);

        if (rpc_resp.status_code == 0) {
            count++;
        } else {
            errors++;
        }
    }

    auto bench_end = std::chrono::steady_clock::now();
    result.requests = count;
    result.errors = errors;
    result.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        bench_end - bench_start).count();

    client.Disconnect();
}

std::string FormatNumber(int64_t n) {
    if (n < 1000) return std::to_string(n);
    std::string s = std::to_string(n);
    int pos = s.length() - 3;
    while (pos > 0) {
        s.insert(pos, ",");
        pos -= 3;
    }
    return s;
}

int main(int argc, char* argv[]) {
    evthread_use_pthreads();

    Logger::GetInstance().SetLevel(LogLevel::Info);

    BenchConfig config;

    if (argc > 1) config.server_workers = std::atoi(argv[1]);
    if (argc > 2) config.client_threads = std::atoi(argv[2]);
    if (argc > 3) config.duration_sec   = std::atoi(argv[3]);
    if (argc > 4) config.warmup_sec     = std::atoi(argv[4]);

    std::cout << "\n";
    std::cout << "==========================================================\n";
    std::cout << "  RPC Framework Benchmark\n";
    std::cout << "==========================================================\n";
    std::cout << "  Server workers:    " << config.server_workers << "\n";
    std::cout << "  Client threads:    " << config.client_threads << "\n";
    std::cout << "  Duration:          " << config.duration_sec << "s\n";
    std::cout << "  Warmup:            " << config.warmup_sec << "s\n";
    std::cout << "----------------------------------------------------------\n\n";

    // Start server
    std::atomic<bool> server_ready{false};
    std::thread server_thread(run_benchmark_server, config.server_port,
                              config.server_workers, std::ref(server_ready));
    while (!server_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Warmup
    std::cout << "[Warmup] " << config.warmup_sec << "s with 2 clients...\n";
    {
        std::atomic<bool> warmup_start{false};
        std::vector<ThreadResult> warmup_results(2);
        std::vector<std::thread> warmup_threads;

        for (int i = 0; i < 2; i++) {
            warmup_threads.emplace_back(client_worker, i, config.server_port,
                                        config.warmup_sec,
                                        std::ref(warmup_start),
                                        std::ref(warmup_results[i]));
        }

        warmup_start.store(true, std::memory_order_release);

        for (auto& t : warmup_threads) t.join();

        int64_t warmup_total = 0;
        for (auto& r : warmup_results) warmup_total += r.requests;
        std::cout << "[Warmup] " << FormatNumber(warmup_total) << " requests completed.\n\n";
    }

    MetricsCollector::Instance().Clear();

    // Benchmark
    std::cout << "[Bench] Starting " << config.duration_sec << "s benchmark with "
              << config.client_threads << " concurrent clients...\n";

    std::atomic<bool> bench_start{false};
    std::vector<ThreadResult> results(config.client_threads);
    std::vector<std::thread> client_threads;

    auto wall_start = std::chrono::steady_clock::now();

    for (int i = 0; i < config.client_threads; i++) {
        client_threads.emplace_back(client_worker, i, config.server_port,
                                    config.duration_sec,
                                    std::ref(bench_start),
                                    std::ref(results[i]));
    }

    bench_start.store(true, std::memory_order_release);

    for (auto& t : client_threads) t.join();

    auto wall_end = std::chrono::steady_clock::now();
    double wall_sec = std::chrono::duration<double>(wall_end - wall_start).count();

    // Aggregate
    int64_t total_requests = 0;
    int64_t total_errors = 0;
    int64_t max_thread_elapsed = 0;
    std::vector<int64_t> per_thread_qps;

    for (int i = 0; i < config.client_threads; i++) {
        total_requests += results[i].requests;
        total_errors += results[i].errors;
        if (results[i].elapsed_us > max_thread_elapsed)
            max_thread_elapsed = results[i].elapsed_us;
        double elapsed_s = results[i].elapsed_us / 1'000'000.0;
        double thread_qps = (elapsed_s > 0) ? (results[i].requests / elapsed_s) : 0;
        per_thread_qps.push_back(static_cast<int64_t>(thread_qps));
    }

    double effective_sec = max_thread_elapsed / 1'000'000.0;
    double qps = (effective_sec > 0) ? (total_requests / effective_sec) : 0;

    int64_t min_thread_qps = per_thread_qps.empty() ? 0 :
        *std::min_element(per_thread_qps.begin(), per_thread_qps.end());
    int64_t max_thread_qps = per_thread_qps.empty() ? 0 :
        *std::max_element(per_thread_qps.begin(), per_thread_qps.end());

    // Report
    std::cout << "\n";
    std::cout << "==========================================================\n";
    std::cout << "  Benchmark Results\n";
    std::cout << "==========================================================\n";
    std::cout << "  Wall clock:      " << std::fixed << std::setprecision(3)
              << wall_sec << "s\n";
    std::cout << "  Total requests:  " << FormatNumber(total_requests) << "\n";
    std::cout << "  Errors:          " << total_errors << "\n";
    std::cout << "  QPS:             " << std::fixed << std::setprecision(1)
              << qps << "\n";
    std::cout << "  Per-thread QPS:  min=" << FormatNumber(min_thread_qps)
              << "  max=" << FormatNumber(max_thread_qps) << "\n";
    std::cout << "----------------------------------------------------------\n";

    std::cout << MetricsCollector::Instance().Report();

    server_thread.detach();
    return 0;
}
