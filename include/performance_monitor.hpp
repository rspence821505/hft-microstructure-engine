#ifndef PERFORMANCE_MONITOR_HPP
#define PERFORMANCE_MONITOR_HPP

/**
 * @file performance_monitor.hpp
 * @brief Performance monitoring and latency tracking for the analytics platform
 *
 * Provides low-overhead performance measurement including:
 * - Latency histogram tracking
 * - Throughput measurement
 * - Component-level timing
 */

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

/**
 * @class PerformanceMonitor
 * @brief Tracks performance metrics with low overhead
 *
 * Uses atomic operations for thread-safe statistics collection
 * with minimal contention. Latency is bucketed into 100ns
 * intervals for fast histogram updates.
 */
class PerformanceMonitor {
public:
    static constexpr size_t HISTOGRAM_BUCKETS = 100;
    static constexpr uint64_t BUCKET_WIDTH_NS = 100;  // 100ns per bucket

private:
    // Latency histogram (each bucket = 100ns, max tracked = 10us)
    std::array<std::atomic<uint64_t>, HISTOGRAM_BUCKETS> latency_histogram_;

    // Event counters
    std::atomic<uint64_t> events_processed_{0};
    std::atomic<uint64_t> events_dropped_{0};

    // Timing
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_report_time_;

    // Component-level tracking
    struct ComponentStats {
        std::atomic<uint64_t> event_count{0};
        std::atomic<uint64_t> total_time_ns{0};
        std::atomic<uint64_t> max_time_ns{0};
    };
    std::unordered_map<std::string, ComponentStats> component_stats_;
    std::mutex component_mutex_;

    bool enabled_ = true;

public:
    /**
     * @brief Constructs and starts the performance monitor
     */
    PerformanceMonitor() {
        for (auto& bucket : latency_histogram_) {
            bucket.store(0, std::memory_order_relaxed);
        }
        start_time_ = std::chrono::steady_clock::now();
        last_report_time_ = start_time_;
    }

    /**
     * @brief Enables or disables monitoring
     * @param enabled Whether to track metrics
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }

    /**
     * @brief Checks if monitoring is enabled
     * @return true if enabled
     */
    bool is_enabled() const { return enabled_; }

    /**
     * @brief Records an event latency
     * @param latency_ns Latency in nanoseconds
     */
    void record_event_latency(uint64_t latency_ns) {
        if (!enabled_) return;

        // Bucket index: latency_ns / 100, capped at max bucket
        size_t bucket = std::min(latency_ns / BUCKET_WIDTH_NS,
                                  static_cast<uint64_t>(HISTOGRAM_BUCKETS - 1));
        latency_histogram_[bucket].fetch_add(1, std::memory_order_relaxed);
        events_processed_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Records an event latency from chrono duration
     * @param latency Duration to record
     */
    void record_event_latency(std::chrono::nanoseconds latency) {
        record_event_latency(static_cast<uint64_t>(latency.count()));
    }

    /**
     * @brief Records a dropped event
     */
    void record_dropped_event() {
        if (!enabled_) return;
        events_dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Records component-level timing
     * @param component Component name
     * @param time_ns Time spent in component
     */
    void record_component_time(const std::string& component, uint64_t time_ns) {
        if (!enabled_) return;

        std::lock_guard<std::mutex> lock(component_mutex_);
        auto& stats = component_stats_[component];
        stats.event_count.fetch_add(1, std::memory_order_relaxed);
        stats.total_time_ns.fetch_add(time_ns, std::memory_order_relaxed);

        // Update max atomically
        uint64_t current_max = stats.max_time_ns.load(std::memory_order_relaxed);
        while (time_ns > current_max &&
               !stats.max_time_ns.compare_exchange_weak(
                   current_max, time_ns,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {
            // Retry until successful
        }
    }

    /**
     * @brief Gets total events processed
     * @return Event count
     */
    uint64_t events_processed() const {
        return events_processed_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Gets total events dropped
     * @return Dropped count
     */
    uint64_t events_dropped() const {
        return events_dropped_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Gets elapsed time since start
     * @return Duration since monitoring started
     */
    std::chrono::nanoseconds elapsed() const {
        return std::chrono::steady_clock::now() - start_time_;
    }

    /**
     * @brief Gets current throughput
     * @return Events per second
     */
    double throughput() const {
        auto elapsed_ns = elapsed().count();
        if (elapsed_ns <= 0) return 0.0;
        return events_processed_.load() * 1e9 / elapsed_ns;
    }

    /**
     * @brief Gets latency percentile from histogram
     * @param p Percentile (0-100)
     * @return Latency in nanoseconds at percentile
     */
    uint64_t latency_percentile(int p) const {
        uint64_t total = events_processed_.load(std::memory_order_relaxed);
        if (total == 0) return 0;

        uint64_t target = total * p / 100;
        uint64_t cumulative = 0;

        for (size_t i = 0; i < HISTOGRAM_BUCKETS; ++i) {
            cumulative += latency_histogram_[i].load(std::memory_order_relaxed);
            if (cumulative >= target) {
                return (i + 1) * BUCKET_WIDTH_NS;  // Upper bound of bucket
            }
        }
        return HISTOGRAM_BUCKETS * BUCKET_WIDTH_NS;  // Max tracked
    }

    /**
     * @brief Gets mean latency from histogram
     * @return Approximate mean latency in nanoseconds
     */
    double mean_latency() const {
        uint64_t total_count = events_processed_.load(std::memory_order_relaxed);
        if (total_count == 0) return 0.0;

        double weighted_sum = 0.0;
        for (size_t i = 0; i < HISTOGRAM_BUCKETS; ++i) {
            uint64_t count = latency_histogram_[i].load(std::memory_order_relaxed);
            double bucket_midpoint = (i + 0.5) * BUCKET_WIDTH_NS;
            weighted_sum += count * bucket_midpoint;
        }
        return weighted_sum / total_count;
    }

    /**
     * @brief Prints latency percentiles
     */
    void print_latency_percentiles() const {
        std::cout << "Latency percentiles:\n";
        std::cout << "  p50:  " << latency_percentile(50) << " ns\n";
        std::cout << "  p90:  " << latency_percentile(90) << " ns\n";
        std::cout << "  p95:  " << latency_percentile(95) << " ns\n";
        std::cout << "  p99:  " << latency_percentile(99) << " ns\n";
        std::cout << "  p99.9: " << latency_percentile(999) / 10 << " ns\n";
        std::cout << "  Mean: " << std::fixed << std::setprecision(1)
                  << mean_latency() << " ns\n";
    }

    /**
     * @brief Prints full performance statistics
     */
    void print_statistics() const {
        auto elapsed_s = elapsed().count() / 1e9;

        std::cout << "\n========================================\n";
        std::cout << "      PERFORMANCE MONITOR REPORT        \n";
        std::cout << "========================================\n";

        std::cout << "\n--- Throughput ---\n";
        std::cout << "  Duration: " << std::fixed << std::setprecision(2)
                  << elapsed_s << " sec\n";
        std::cout << "  Events processed: " << events_processed_.load() << "\n";
        std::cout << "  Events dropped: " << events_dropped_.load() << "\n";
        std::cout << "  Throughput: " << std::setprecision(1)
                  << throughput() << " events/sec\n";

        std::cout << "\n--- Latency ---\n";
        print_latency_percentiles();

        if (!component_stats_.empty()) {
            std::cout << "\n--- Component Timing ---\n";
            std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(component_mutex_));
            for (const auto& [name, stats] : component_stats_) {
                uint64_t count = stats.event_count.load(std::memory_order_relaxed);
                uint64_t total_ns = stats.total_time_ns.load(std::memory_order_relaxed);
                uint64_t max_ns = stats.max_time_ns.load(std::memory_order_relaxed);
                double avg_ns = count > 0 ? static_cast<double>(total_ns) / count : 0.0;

                std::cout << "  " << name << ":\n";
                std::cout << "    Count: " << count << "\n";
                std::cout << "    Avg: " << std::setprecision(1) << avg_ns << " ns\n";
                std::cout << "    Max: " << max_ns << " ns\n";
            }
        }

        std::cout << "========================================\n";
    }

    /**
     * @brief Resets all statistics
     */
    void reset() {
        for (auto& bucket : latency_histogram_) {
            bucket.store(0, std::memory_order_relaxed);
        }
        events_processed_.store(0, std::memory_order_relaxed);
        events_dropped_.store(0, std::memory_order_relaxed);
        start_time_ = std::chrono::steady_clock::now();
        last_report_time_ = start_time_;

        std::lock_guard<std::mutex> lock(component_mutex_);
        component_stats_.clear();
    }

    /**
     * @brief Gets statistics as a formatted string
     * @return Statistics string
     */
    std::string to_string() const {
        std::ostringstream oss;
        oss << "Events: " << events_processed_.load()
            << " | Throughput: " << std::fixed << std::setprecision(0)
            << throughput() << " evt/s"
            << " | Latency p50: " << latency_percentile(50) << "ns"
            << " p99: " << latency_percentile(99) << "ns";
        return oss.str();
    }
};

/**
 * @class ScopedTimer
 * @brief RAII timer for measuring operation duration
 */
class ScopedTimer {
    PerformanceMonitor& monitor_;
    std::string component_;
    std::chrono::steady_clock::time_point start_;

public:
    ScopedTimer(PerformanceMonitor& monitor, const std::string& component)
        : monitor_(monitor), component_(component),
          start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_);
        monitor_.record_component_time(component_, duration.count());
    }
};

#endif // PERFORMANCE_MONITOR_HPP
