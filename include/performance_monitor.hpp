#ifndef PERFORMANCE_MONITOR_HPP
#define PERFORMANCE_MONITOR_HPP

/**
 * @file performance_monitor.hpp
 * @brief Week 4.2: Performance Optimization - Comprehensive monitoring and benchmarking
 *
 * Provides low-overhead performance measurement including:
 * - Lock-free latency histogram tracking (nanosecond resolution)
 * - Throughput measurement
 * - Component-level timing breakdown
 * - Performance target verification
 *
 * Performance targets from Week 4.2:
 * - CSV parsing: Maintain 417K rows/sec
 * - Order book updates: <1us per event
 * - Analytics calculation: <500ns per metric update
 * - Lock-free queue handoff: <100ns median latency
 * - End-to-end latency: <10us from market data to analytics result
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @class PerformanceMonitor
 * @brief Lock-free performance monitoring with latency histograms and throughput tracking
 *
 * Uses atomic operations for thread-safe statistics collection with minimal contention.
 * Latency is bucketed into 100ns intervals for fast histogram updates.
 *
 * Features:
 * - Lock-free latency histogram (100 buckets, 100ns resolution)
 * - Extended microsecond histogram for larger latencies
 * - Lock-free min/max tracking
 * - Throughput measurement
 * - Percentile calculations (p50, p95, p99, p99.9)
 */
class PerformanceMonitor {
public:
    static constexpr size_t NUM_BUCKETS = 100;
    static constexpr uint64_t BUCKET_SIZE_NS = 100;  // 100ns per bucket
    static constexpr uint64_t MAX_TRACKED_NS = NUM_BUCKETS * BUCKET_SIZE_NS;  // 10us max

    // Extended histogram for larger latencies (microsecond buckets)
    static constexpr size_t NUM_US_BUCKETS = 100;
    static constexpr uint64_t US_BUCKET_SIZE_NS = 1000;  // 1us per bucket

private:
    // Lock-free latency histogram (nanosecond resolution, 0-10us)
    alignas(64) std::array<std::atomic<uint64_t>, NUM_BUCKETS> latency_histogram_;

    // Extended microsecond histogram for larger latencies (10us-110us)
    alignas(64) std::array<std::atomic<uint64_t>, NUM_US_BUCKETS> us_latency_histogram_;

    // Overflow counter for latencies > 110us
    alignas(64) std::atomic<uint64_t> overflow_count_{0};

    // Event counters
    alignas(64) std::atomic<uint64_t> events_processed_{0};
    alignas(64) std::atomic<uint64_t> events_dropped_{0};

    // Running sums for mean calculation (lock-free)
    alignas(64) std::atomic<uint64_t> total_latency_ns_{0};

    // Min/max tracking (lock-free)
    alignas(64) std::atomic<uint64_t> min_latency_ns_{UINT64_MAX};
    alignas(64) std::atomic<uint64_t> max_latency_ns_{0};

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
    mutable std::mutex component_mutex_;

    bool enabled_ = true;
    std::string name_ = "default";

public:
    /**
     * @brief Default constructor
     */
    PerformanceMonitor() {
        reset();
    }

    /**
     * @brief Constructor with name
     * @param name Identifier for this monitor
     */
    explicit PerformanceMonitor(const std::string& name) : name_(name) {
        reset();
    }

    /**
     * @brief Resets all statistics
     */
    void reset() {
        for (auto& bucket : latency_histogram_) {
            bucket.store(0, std::memory_order_relaxed);
        }
        for (auto& bucket : us_latency_histogram_) {
            bucket.store(0, std::memory_order_relaxed);
        }
        overflow_count_.store(0, std::memory_order_relaxed);
        events_processed_.store(0, std::memory_order_relaxed);
        events_dropped_.store(0, std::memory_order_relaxed);
        total_latency_ns_.store(0, std::memory_order_relaxed);
        min_latency_ns_.store(UINT64_MAX, std::memory_order_relaxed);
        max_latency_ns_.store(0, std::memory_order_relaxed);
        start_time_ = std::chrono::steady_clock::now();
        last_report_time_ = start_time_;

        std::lock_guard<std::mutex> lock(component_mutex_);
        component_stats_.clear();
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
     * @brief Sets the monitor name
     * @param name Name identifier
     */
    void set_name(const std::string& name) { name_ = name; }

    /**
     * @brief Gets the monitor name
     * @return Name identifier
     */
    const std::string& get_name() const { return name_; }

    // ========================================================================
    // LATENCY RECORDING
    // ========================================================================

    /**
     * @brief Records an event latency (nanoseconds)
     * @param latency_ns Latency in nanoseconds
     *
     * Lock-free recording using atomic operations.
     * O(1) time complexity.
     */
    void record_event_latency(uint64_t latency_ns) {
        if (!enabled_) return;

        events_processed_.fetch_add(1, std::memory_order_relaxed);
        total_latency_ns_.fetch_add(latency_ns, std::memory_order_relaxed);

        // Update min (lock-free CAS loop)
        uint64_t current_min = min_latency_ns_.load(std::memory_order_relaxed);
        while (latency_ns < current_min) {
            if (min_latency_ns_.compare_exchange_weak(current_min, latency_ns,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {
                break;
            }
        }

        // Update max (lock-free CAS loop)
        uint64_t current_max = max_latency_ns_.load(std::memory_order_relaxed);
        while (latency_ns > current_max) {
            if (max_latency_ns_.compare_exchange_weak(current_max, latency_ns,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {
                break;
            }
        }

        // Bucket into appropriate histogram
        if (latency_ns < MAX_TRACKED_NS) {
            // Nanosecond histogram (0-10us)
            size_t bucket = latency_ns / BUCKET_SIZE_NS;
            latency_histogram_[bucket].fetch_add(1, std::memory_order_relaxed);
        } else if (latency_ns < MAX_TRACKED_NS + NUM_US_BUCKETS * US_BUCKET_SIZE_NS) {
            // Microsecond histogram (10us-110us)
            size_t bucket = (latency_ns - MAX_TRACKED_NS) / US_BUCKET_SIZE_NS;
            us_latency_histogram_[bucket].fetch_add(1, std::memory_order_relaxed);
        } else {
            // Overflow (>110us)
            overflow_count_.fetch_add(1, std::memory_order_relaxed);
        }
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
        }
    }

    // ========================================================================
    // STATISTICS QUERIES
    // ========================================================================

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
        return events_processed_.load(std::memory_order_relaxed) * 1e9 / elapsed_ns;
    }

    /**
     * @brief Gets minimum latency in nanoseconds
     */
    uint64_t get_min_latency_ns() const {
        uint64_t min_val = min_latency_ns_.load(std::memory_order_relaxed);
        return min_val == UINT64_MAX ? 0 : min_val;
    }

    /**
     * @brief Gets maximum latency in nanoseconds
     */
    uint64_t get_max_latency_ns() const {
        return max_latency_ns_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Gets mean latency from histogram
     * @return Mean latency in nanoseconds
     */
    double mean_latency() const {
        uint64_t count = events_processed_.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(total_latency_ns_.load(std::memory_order_relaxed)) / count;
    }

    /**
     * @brief Calculates a percentile from the histogram
     * @param percentile Percentile (0.0 to 1.0)
     * @return Latency at the given percentile in nanoseconds
     */
    uint64_t get_percentile_ns(double percentile) const {
        uint64_t total = events_processed_.load(std::memory_order_relaxed);
        if (total == 0) return 0;

        uint64_t target = static_cast<uint64_t>(percentile * total);
        uint64_t cumulative = 0;

        // Check nanosecond histogram (0-10us)
        for (size_t i = 0; i < NUM_BUCKETS; ++i) {
            cumulative += latency_histogram_[i].load(std::memory_order_relaxed);
            if (cumulative >= target) {
                return i * BUCKET_SIZE_NS + BUCKET_SIZE_NS / 2;
            }
        }

        // Check microsecond histogram (10us-110us)
        for (size_t i = 0; i < NUM_US_BUCKETS; ++i) {
            cumulative += us_latency_histogram_[i].load(std::memory_order_relaxed);
            if (cumulative >= target) {
                return MAX_TRACKED_NS + i * US_BUCKET_SIZE_NS + US_BUCKET_SIZE_NS / 2;
            }
        }

        // In overflow
        return MAX_TRACKED_NS + NUM_US_BUCKETS * US_BUCKET_SIZE_NS;
    }

    /**
     * @brief Gets latency percentile (legacy interface, percentile 0-100)
     * @param p Percentile (0-100)
     * @return Latency in nanoseconds at percentile
     */
    uint64_t latency_percentile(int p) const {
        return get_percentile_ns(p / 100.0);
    }

    /**
     * @brief Returns p50 (median) latency in nanoseconds
     */
    uint64_t get_p50_ns() const { return get_percentile_ns(0.50); }

    /**
     * @brief Returns p95 latency in nanoseconds
     */
    uint64_t get_p95_ns() const { return get_percentile_ns(0.95); }

    /**
     * @brief Returns p99 latency in nanoseconds
     */
    uint64_t get_p99_ns() const { return get_percentile_ns(0.99); }

    /**
     * @brief Returns p99.9 latency in nanoseconds
     */
    uint64_t get_p999_ns() const { return get_percentile_ns(0.999); }

    /**
     * @brief Returns overflow count (latencies > 110us)
     */
    uint64_t get_overflow_count() const {
        return overflow_count_.load(std::memory_order_relaxed);
    }

    // ========================================================================
    // REPORTING
    // ========================================================================

    /**
     * @brief Prints latency percentiles
     */
    void print_latency_percentiles() const {
        std::cout << "Latency percentiles:\n";
        std::cout << "  Min:   " << get_min_latency_ns() << " ns\n";
        std::cout << "  p50:   " << get_p50_ns() << " ns\n";
        std::cout << "  p90:   " << latency_percentile(90) << " ns\n";
        std::cout << "  p95:   " << get_p95_ns() << " ns\n";
        std::cout << "  p99:   " << get_p99_ns() << " ns\n";
        std::cout << "  p99.9: " << get_p999_ns() << " ns\n";
        std::cout << "  Max:   " << get_max_latency_ns() << " ns\n";
        std::cout << "  Mean:  " << std::fixed << std::setprecision(1)
                  << mean_latency() << " ns\n";
        if (get_overflow_count() > 0) {
            std::cout << "  Overflow (>110us): " << get_overflow_count() << "\n";
        }
    }

    /**
     * @brief Prints full performance statistics
     */
    void print_statistics() const {
        auto elapsed_s = elapsed().count() / 1e9;

        std::cout << "\n========================================\n";
        std::cout << "  PERFORMANCE MONITOR: " << name_ << "\n";
        std::cout << "========================================\n";

        std::cout << "\n--- Throughput ---\n";
        std::cout << "  Duration: " << std::fixed << std::setprecision(2)
                  << elapsed_s << " sec\n";
        std::cout << "  Events processed: " << events_processed_.load() << "\n";
        std::cout << "  Events dropped: " << events_dropped_.load() << "\n";
        std::cout << "  Throughput: " << std::setprecision(0)
                  << throughput() << " events/sec\n";

        std::cout << "\n--- Latency ---\n";
        print_latency_percentiles();

        if (!component_stats_.empty()) {
            std::cout << "\n--- Component Timing ---\n";
            std::lock_guard<std::mutex> lock(component_mutex_);
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
     * @brief Prints latency histogram ASCII art
     * @param width Width of the histogram bars
     */
    void print_histogram(size_t width = 40) const {
        std::cout << "\n--- Latency Histogram: " << name_ << " ---\n";

        // Find max bucket for scaling
        uint64_t max_count = 0;
        for (const auto& bucket : latency_histogram_) {
            max_count = std::max(max_count, bucket.load(std::memory_order_relaxed));
        }
        if (max_count == 0) {
            std::cout << "  (no data)\n";
            return;
        }

        // Print nanosecond histogram (consolidate into 1us buckets)
        std::cout << "  [0-10us range, 1us buckets]:\n";
        for (size_t i = 0; i < 10; ++i) {
            uint64_t count = 0;
            for (size_t j = 0; j < 10; ++j) {
                count += latency_histogram_[i * 10 + j].load(std::memory_order_relaxed);
            }
            if (count > 0) {
                size_t bar_len = static_cast<size_t>((count * width) / max_count);
                bar_len = std::max(bar_len, static_cast<size_t>(1));
                std::cout << "    " << std::setw(2) << i << "-" << std::setw(2) << (i+1)
                          << "us: " << std::string(bar_len, '#')
                          << " (" << count << ")\n";
            }
        }
    }

    /**
     * @brief Prints CSV-formatted statistics
     */
    void print_csv() const {
        std::cout << name_ << ","
                  << events_processed_.load() << ","
                  << std::fixed << std::setprecision(1) << mean_latency() << ","
                  << get_p50_ns() << ","
                  << get_p95_ns() << ","
                  << get_p99_ns() << ","
                  << get_p999_ns() << ","
                  << get_min_latency_ns() << ","
                  << get_max_latency_ns() << ","
                  << std::setprecision(0) << throughput() << "\n";
    }

    /**
     * @brief Prints CSV header
     */
    static void print_csv_header() {
        std::cout << "name,events,mean_ns,p50_ns,p95_ns,p99_ns,p999_ns,min_ns,max_ns,throughput\n";
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
            << " | Latency p50: " << get_p50_ns() << "ns"
            << " p99: " << get_p99_ns() << "ns";
        return oss.str();
    }

    /**
     * @brief Checks if performance targets are met
     * @param target_p50_ns Target p50 latency in nanoseconds
     * @param target_p99_ns Target p99 latency in nanoseconds
     * @param target_throughput Target throughput in events/sec
     * @return true if all targets are met
     */
    bool check_targets(uint64_t target_p50_ns, uint64_t target_p99_ns,
                       double target_throughput) const {
        bool p50_ok = get_p50_ns() <= target_p50_ns;
        bool p99_ok = get_p99_ns() <= target_p99_ns;
        bool throughput_ok = throughput() >= target_throughput;

        std::cout << "  " << name_ << ":\n";
        std::cout << "    p50 <= " << target_p50_ns << "ns: "
                  << (p50_ok ? "PASS" : "FAIL")
                  << " (actual: " << get_p50_ns() << "ns)\n";
        std::cout << "    p99 <= " << target_p99_ns << "ns: "
                  << (p99_ok ? "PASS" : "FAIL")
                  << " (actual: " << get_p99_ns() << "ns)\n";
        std::cout << "    throughput >= " << std::fixed << std::setprecision(0)
                  << target_throughput << "/sec: "
                  << (throughput_ok ? "PASS" : "FAIL")
                  << " (actual: " << throughput() << "/sec)\n";

        return p50_ok && p99_ok && throughput_ok;
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
    bool record_latency_;

public:
    /**
     * @brief Constructor for component timing
     */
    ScopedTimer(PerformanceMonitor& monitor, const std::string& component)
        : monitor_(monitor), component_(component),
          start_(std::chrono::steady_clock::now()), record_latency_(false) {}

    /**
     * @brief Constructor for latency recording
     */
    explicit ScopedTimer(PerformanceMonitor& monitor, bool record_latency = true)
        : monitor_(monitor), component_(""),
          start_(std::chrono::steady_clock::now()), record_latency_(record_latency) {}

    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_);
        if (record_latency_) {
            monitor_.record_event_latency(duration);
        }
        if (!component_.empty()) {
            monitor_.record_component_time(component_, duration.count());
        }
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

/**
 * @class ComponentLatencyTracker
 * @brief Tracks latencies for multiple components in the pipeline
 *
 * Used to measure end-to-end latency breakdown:
 * - CSV parsing
 * - Order book updates
 * - Analytics computation
 * - Queue handoff
 */
class ComponentLatencyTracker {
public:
    enum ComponentIndex : size_t {
        CSV_PARSING = 0,
        ORDER_BOOK_UPDATE = 1,
        ANALYTICS_COMPUTE = 2,
        QUEUE_HANDOFF = 3,
        IMPACT_CALIBRATION = 4,
        EXECUTION_STRATEGY = 5,
        END_TO_END = 6,
        CUSTOM = 7,
        MAX_COMPONENTS = 8
    };

private:
    std::array<PerformanceMonitor, MAX_COMPONENTS> monitors_;

public:
    ComponentLatencyTracker() {
        monitors_[CSV_PARSING].set_name("csv_parsing");
        monitors_[ORDER_BOOK_UPDATE].set_name("order_book_update");
        monitors_[ANALYTICS_COMPUTE].set_name("analytics_compute");
        monitors_[QUEUE_HANDOFF].set_name("queue_handoff");
        monitors_[IMPACT_CALIBRATION].set_name("impact_calibration");
        monitors_[EXECUTION_STRATEGY].set_name("execution_strategy");
        monitors_[END_TO_END].set_name("end_to_end");
        monitors_[CUSTOM].set_name("custom");
    }

    PerformanceMonitor& csv_parsing() { return monitors_[CSV_PARSING]; }
    PerformanceMonitor& order_book_update() { return monitors_[ORDER_BOOK_UPDATE]; }
    PerformanceMonitor& analytics_compute() { return monitors_[ANALYTICS_COMPUTE]; }
    PerformanceMonitor& queue_handoff() { return monitors_[QUEUE_HANDOFF]; }
    PerformanceMonitor& impact_calibration() { return monitors_[IMPACT_CALIBRATION]; }
    PerformanceMonitor& execution_strategy() { return monitors_[EXECUTION_STRATEGY]; }
    PerformanceMonitor& end_to_end() { return monitors_[END_TO_END]; }
    PerformanceMonitor& custom() { return monitors_[CUSTOM]; }

    const PerformanceMonitor& csv_parsing() const { return monitors_[CSV_PARSING]; }
    const PerformanceMonitor& order_book_update() const { return monitors_[ORDER_BOOK_UPDATE]; }
    const PerformanceMonitor& analytics_compute() const { return monitors_[ANALYTICS_COMPUTE]; }
    const PerformanceMonitor& queue_handoff() const { return monitors_[QUEUE_HANDOFF]; }
    const PerformanceMonitor& impact_calibration() const { return monitors_[IMPACT_CALIBRATION]; }
    const PerformanceMonitor& execution_strategy() const { return monitors_[EXECUTION_STRATEGY]; }
    const PerformanceMonitor& end_to_end() const { return monitors_[END_TO_END]; }
    const PerformanceMonitor& custom() const { return monitors_[CUSTOM]; }

    PerformanceMonitor& get(ComponentIndex idx) { return monitors_[idx]; }
    const PerformanceMonitor& get(ComponentIndex idx) const { return monitors_[idx]; }

    void reset_all() {
        for (auto& m : monitors_) {
            m.reset();
        }
    }

    void print_all_statistics() const {
        std::cout << "\n========================================\n";
        std::cout << "    COMPONENT LATENCY BREAKDOWN\n";
        std::cout << "========================================\n";

        for (const auto& m : monitors_) {
            if (m.events_processed() > 0) {
                m.print_statistics();
            }
        }
    }

    void print_csv_summary() const {
        PerformanceMonitor::print_csv_header();
        for (const auto& m : monitors_) {
            if (m.events_processed() > 0) {
                m.print_csv();
            }
        }
    }

    /**
     * @brief Verifies Week 4.2 performance targets
     *
     * Targets from the integration plan:
     * - CSV parsing: Maintain 417K rows/sec
     * - Order book updates: <1us per event
     * - Analytics calculation: <500ns per metric update
     * - Lock-free queue handoff: <100ns median latency
     * - End-to-end latency: <10us from market data to analytics result
     */
    void verify_week42_targets() const {
        std::cout << "\n========================================\n";
        std::cout << "    WEEK 4.2 PERFORMANCE TARGETS\n";
        std::cout << "========================================\n";

        bool all_pass = true;

        // CSV parsing: 417K rows/sec
        if (monitors_[CSV_PARSING].events_processed() > 0) {
            std::cout << "\n[CSV Parsing - Target: 417K rows/sec]\n";
            bool ok = monitors_[CSV_PARSING].check_targets(
                5000,   // p50 < 5us (reasonable)
                10000,  // p99 < 10us
                400000  // >400K rows/sec
            );
            all_pass &= ok;
        }

        // Order book updates: <1us per event
        if (monitors_[ORDER_BOOK_UPDATE].events_processed() > 0) {
            std::cout << "\n[Order Book Update - Target: <1us per event]\n";
            bool ok = monitors_[ORDER_BOOK_UPDATE].check_targets(
                1000,   // p50 < 1us
                2000,   // p99 < 2us
                1000000 // >1M ops/sec
            );
            all_pass &= ok;
        }

        // Analytics calculation: <500ns per metric update
        if (monitors_[ANALYTICS_COMPUTE].events_processed() > 0) {
            std::cout << "\n[Analytics Compute - Target: <500ns per update]\n";
            bool ok = monitors_[ANALYTICS_COMPUTE].check_targets(
                500,    // p50 < 500ns
                1000,   // p99 < 1us
                500000  // >500K ops/sec
            );
            all_pass &= ok;
        }

        // Lock-free queue handoff: <100ns median latency
        if (monitors_[QUEUE_HANDOFF].events_processed() > 0) {
            std::cout << "\n[Queue Handoff - Target: <100ns median]\n";
            bool ok = monitors_[QUEUE_HANDOFF].check_targets(
                100,     // p50 < 100ns
                500,     // p99 < 500ns
                10000000 // >10M ops/sec
            );
            all_pass &= ok;
        }

        // End-to-end latency: <10us
        if (monitors_[END_TO_END].events_processed() > 0) {
            std::cout << "\n[End-to-End - Target: <10us]\n";
            bool ok = monitors_[END_TO_END].check_targets(
                10000,   // p50 < 10us
                50000,   // p99 < 50us
                100000   // >100K events/sec
            );
            all_pass &= ok;
        }

        std::cout << "\n========================================\n";
        std::cout << "  OVERALL: " << (all_pass ? "ALL TARGETS MET" : "SOME TARGETS MISSED") << "\n";
        std::cout << "========================================\n";
    }

    /**
     * @brief Prints a compact summary table
     */
    void print_summary_table() const {
        std::cout << "\n";
        std::cout << std::left
                  << std::setw(20) << "Component"
                  << std::setw(12) << "Events"
                  << std::setw(12) << "p50 (ns)"
                  << std::setw(12) << "p99 (ns)"
                  << std::setw(15) << "Throughput"
                  << "\n";
        std::cout << std::string(71, '-') << "\n";

        for (const auto& m : monitors_) {
            if (m.events_processed() > 0) {
                std::cout << std::left
                          << std::setw(20) << m.get_name()
                          << std::setw(12) << m.events_processed()
                          << std::setw(12) << m.get_p50_ns()
                          << std::setw(12) << m.get_p99_ns()
                          << std::setw(15) << std::fixed << std::setprecision(0)
                          << m.throughput() << "/s"
                          << "\n";
            }
        }
    }
};

#endif // PERFORMANCE_MONITOR_HPP
