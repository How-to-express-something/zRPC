#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cinttypes>

class MetricsCollector {
public:
    static MetricsCollector& Instance();

    void RecordRequest()  { total_requests_.fetch_add(1, std::memory_order_relaxed); }
    void RecordSuccess()  { success_count_.fetch_add(1, std::memory_order_relaxed); }
    void RecordError()    { error_count_.fetch_add(1,   std::memory_order_relaxed); }

    void RecordServerLatencyUs(int64_t latency_us);
    void RecordClientLatencyUs(int64_t latency_us);
    void RecordQueueDepth(int depth);
    void RecordTaskWaitUs(int64_t wait_us);
    void RecordTaskExecUs(int64_t exec_us);

    void Clear();
    std::string Report() const;

private:
    MetricsCollector();
    MetricsCollector(const MetricsCollector&) = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;

    static std::string PercentileReport(const std::vector<int64_t>& samples);

    std::chrono::steady_clock::time_point start_time_;

    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> success_count_{0};
    std::atomic<uint64_t> error_count_{0};

    mutable std::mutex server_mutex_;
    std::vector<int64_t> server_latency_us_;

    mutable std::mutex client_mutex_;
    std::vector<int64_t> client_latency_us_;

    mutable std::mutex tp_mutex_;
    std::vector<int>     queue_depth_samples_;
    std::vector<int64_t> task_wait_us_;
    std::vector<int64_t> task_exec_us_;
};

// ---- Inline implementations ----

inline MetricsCollector& MetricsCollector::Instance() {
    static MetricsCollector instance;
    return instance;
}

inline MetricsCollector::MetricsCollector()
    : start_time_(std::chrono::steady_clock::now()) {}

inline void MetricsCollector::Clear() {
    start_time_ = std::chrono::steady_clock::now();
    total_requests_.store(0, std::memory_order_relaxed);
    success_count_.store(0, std::memory_order_relaxed);
    error_count_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(server_mutex_);
        server_latency_us_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_latency_us_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(tp_mutex_);
        queue_depth_samples_.clear();
        task_wait_us_.clear();
        task_exec_us_.clear();
    }
}

inline void MetricsCollector::RecordServerLatencyUs(int64_t latency_us) {
    std::lock_guard<std::mutex> lock(server_mutex_);
    server_latency_us_.push_back(latency_us);
}

inline void MetricsCollector::RecordClientLatencyUs(int64_t latency_us) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    client_latency_us_.push_back(latency_us);
}

inline void MetricsCollector::RecordQueueDepth(int depth) {
    std::lock_guard<std::mutex> lock(tp_mutex_);
    queue_depth_samples_.push_back(depth);
}

inline void MetricsCollector::RecordTaskWaitUs(int64_t wait_us) {
    std::lock_guard<std::mutex> lock(tp_mutex_);
    task_wait_us_.push_back(wait_us);
}

inline void MetricsCollector::RecordTaskExecUs(int64_t exec_us) {
    std::lock_guard<std::mutex> lock(tp_mutex_);
    task_exec_us_.push_back(exec_us);
}

inline std::string MetricsCollector::PercentileReport(const std::vector<int64_t>& samples) {
    if (samples.empty()) {
        return "  (no samples)\n";
    }
    std::vector<int64_t> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    size_t n = sorted.size();

    size_t idx_p50 = n * 50 / 100;
    size_t idx_p99 = n * 99 / 100;
    if (idx_p50 >= n) idx_p50 = n - 1;
    if (idx_p99 >= n) idx_p99 = n - 1;

    int64_t p50 = sorted[idx_p50];
    int64_t p99 = sorted[idx_p99];

    int64_t sum = 0;
    for (auto v : sorted) sum += v;
    int64_t avg = sum / static_cast<int64_t>(n);

    std::ostringstream ss;
    ss << "  count=" << n
       << " min=" << sorted.front() << "us"
       << " max=" << sorted.back()  << "us"
       << " avg=" << avg << "us"
       << " p50=" << p50 << "us"
       << " p99=" << p99 << "us\n";
    return ss.str();
}

inline std::string MetricsCollector::Report() const {
    auto now = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(now - start_time_).count();

    uint64_t total   = total_requests_.load(std::memory_order_relaxed);
    uint64_t success = success_count_.load(std::memory_order_relaxed);
    uint64_t errors  = error_count_.load(std::memory_order_relaxed);
    double qps = (elapsed_s > 0.0) ? (static_cast<double>(total) / elapsed_s) : 0.0;

    std::vector<int64_t> server_copy, client_copy;
    std::vector<int>     depth_copy;
    std::vector<int64_t> wait_copy, exec_copy;
    {
        std::lock_guard<std::mutex> l1(server_mutex_);
        server_copy = server_latency_us_;
    }
    {
        std::lock_guard<std::mutex> l2(client_mutex_);
        client_copy = client_latency_us_;
    }
    {
        std::lock_guard<std::mutex> l3(tp_mutex_);
        depth_copy = queue_depth_samples_;
        wait_copy  = task_wait_us_;
        exec_copy  = task_exec_us_;
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "\n==================== Metrics Report ====================\n";
    ss << "Uptime: " << std::setprecision(4) << elapsed_s << " seconds\n";
    ss << "Requests: total=" << total
       << " success=" << success
       << " error=" << errors << "\n";
    ss << "QPS: " << qps << "\n\n";

    ss << "Server-side Handler Latency (us):\n";
    ss << PercentileReport(server_copy);

    ss << "\nClient-side Round-trip Latency (us):\n";
    ss << PercentileReport(client_copy);

    ss << "\nThread Pool:\n";
    if (!depth_copy.empty()) {
        int64_t sum_d = 0;
        int max_d = 0;
        for (auto d : depth_copy) { sum_d += d; if (d > max_d) max_d = d; }
        double avg_d = static_cast<double>(sum_d) / depth_copy.size();
        ss << "  Queue depth: avg=" << avg_d << " max=" << max_d
           << " (samples=" << depth_copy.size() << ")\n";
    } else {
        ss << "  Queue depth: (no samples)\n";
    }
    ss << "  Task wait time (us):\n" << PercentileReport(wait_copy);
    ss << "  Task exec time (us):\n" << PercentileReport(exec_copy);
    ss << "========================================================\n";
    return ss.str();
}
