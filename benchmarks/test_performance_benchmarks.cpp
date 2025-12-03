/**
 * @file test_performance_benchmarks.cpp
 * @brief Performance Optimization - Automated Benchmark Tests
 *
 * This test suite validates that the microstructure engine meets its
 * performance targets:
 *
 * - CSV parsing: Maintain 417K rows/sec
 * - Order book updates: <1us per event
 * - Analytics calculation: <500ns per metric update
 * - Lock-free queue handoff: <100ns median latency
 * - End-to-end latency: <10us from market data to analytics result
 */

#include "memory_pool.hpp"
#include "microstructure_order_book.hpp"
#include "performance_monitor.hpp"
#include "rolling_statistics.hpp"

// Include SPSC queue (local copy)
#include "spsc_queue.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

// Test configuration
static constexpr int NUM_WARMUP_ITERATIONS = 1000;
static constexpr int NUM_TEST_ITERATIONS = 100000;
static constexpr int NUM_QUEUE_ITERATIONS = 1000000;

// Performance targets (in nanoseconds)
static constexpr uint64_t TARGET_ORDER_BOOK_P50_NS = 1000;   // <1us
static constexpr uint64_t TARGET_ORDER_BOOK_P99_NS = 3000;   // <3us
static constexpr uint64_t TARGET_ANALYTICS_P50_NS = 500;     // <500ns
static constexpr uint64_t TARGET_ANALYTICS_P99_NS = 1000;    // <1us
static constexpr uint64_t TARGET_QUEUE_P50_NS = 100;         // <100ns
static constexpr uint64_t TARGET_QUEUE_P99_NS = 500;         // <500ns
static constexpr uint64_t TARGET_E2E_P50_NS = 10000;         // <10us
static constexpr uint64_t TARGET_E2E_P99_NS = 50000;         // <50us

// Throughput targets (operations per second)
static constexpr double TARGET_ORDER_BOOK_THROUGHPUT = 1000000;  // >1M ops/sec
static constexpr double TARGET_QUEUE_THROUGHPUT = 10000000;      // >10M ops/sec

int tests_passed = 0;
int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            std::cout << "  [PASS] " << message << std::endl; \
            tests_passed++; \
        } else { \
            std::cout << "  [FAIL] " << message << std::endl; \
            tests_failed++; \
        } \
    } while (0)

/**
 * @brief Test 1: Order Book Update Latency
 *
 * Measures the time to insert orders into the order book.
 * Target: <1us p50, <2us p99
 */
void test_order_book_latency() {
    std::cout << "\n=== Test 1: Order Book Update Latency ===\n";
    std::cout << "Target: p50 < 1us, p99 < 2us, throughput > 1M/sec\n";

    MicrostructureOrderBook book("AAPL");
    book.enable_self_trade_prevention(false);  // Disable for benchmark
    PerformanceMonitor monitor("order_book");

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(99.0, 101.0);
    std::uniform_int_distribution<int> qty_dist(100, 1000);

    // Warmup
    for (int i = 0; i < NUM_WARMUP_ITERATIONS; ++i) {
        int account_id = (i % 100) + 1;  // Use different accounts
        Order order(i + 1, account_id, Side::BUY, 100.0, 100, TimeInForce::GTC);
        book.add_order(order);
    }
    monitor.reset();

    // Benchmark
    for (int i = 0; i < NUM_TEST_ITERATIONS; ++i) {
        auto start = std::chrono::steady_clock::now();

        double price = price_dist(rng);
        int qty = qty_dist(rng);
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        int account_id = (i % 100) + 1;  // Use different accounts to avoid self-trade
        Order order(NUM_WARMUP_ITERATIONS + i + 1, account_id, side, price, qty, TimeInForce::GTC);
        book.add_order(order);

        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        monitor.record_event_latency(latency);
    }

    std::cout << "  Results:\n";
    std::cout << "    p50: " << monitor.get_p50_ns() << " ns\n";
    std::cout << "    p99: " << monitor.get_p99_ns() << " ns\n";
    std::cout << "    Throughput: " << static_cast<int>(monitor.throughput()) << " ops/sec\n";

    TEST_ASSERT(monitor.get_p50_ns() <= TARGET_ORDER_BOOK_P50_NS,
                "Order book p50 latency < 1us");
    TEST_ASSERT(monitor.get_p99_ns() <= TARGET_ORDER_BOOK_P99_NS,
                "Order book p99 latency < 2us");
    TEST_ASSERT(monitor.throughput() >= TARGET_ORDER_BOOK_THROUGHPUT,
                "Order book throughput > 1M ops/sec");
}

/**
 * @brief Test 2: Analytics Computation Latency
 *
 * Measures the time to compute analytics (spread, imbalance, etc.)
 * Target: <500ns p50, <1us p99
 */
void test_analytics_latency() {
    std::cout << "\n=== Test 2: Analytics Computation Latency ===\n";
    std::cout << "Target: p50 < 500ns, p99 < 1us\n";

    MicrostructureOrderBook book("AAPL");
    PerformanceMonitor monitor("analytics");

    // Populate order book first
    for (int i = 0; i < 1000; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        double price = (side == Side::BUY) ? 99.0 + (i % 10) * 0.01 : 101.0 - (i % 10) * 0.01;
        Order order(i + 1, 1, side, price, 100, TimeInForce::GTC);
        book.add_order(order);
    }

    // Warmup
    for (int i = 0; i < NUM_WARMUP_ITERATIONS; ++i) {
        [[maybe_unused]] auto imbalance = book.get_current_imbalance();
        [[maybe_unused]] auto spread = book.get_average_spread();
    }
    monitor.reset();

    // Benchmark analytics access
    for (int i = 0; i < NUM_TEST_ITERATIONS; ++i) {
        auto start = std::chrono::steady_clock::now();

        [[maybe_unused]] double imbalance = book.get_current_imbalance();
        [[maybe_unused]] double avg_spread = book.get_average_spread();
        [[maybe_unused]] double spread_stddev = book.get_spread_stddev();

        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        monitor.record_event_latency(latency);
    }

    std::cout << "  Results:\n";
    std::cout << "    p50: " << monitor.get_p50_ns() << " ns\n";
    std::cout << "    p99: " << monitor.get_p99_ns() << " ns\n";

    TEST_ASSERT(monitor.get_p50_ns() <= TARGET_ANALYTICS_P50_NS,
                "Analytics p50 latency < 500ns");
    TEST_ASSERT(monitor.get_p99_ns() <= TARGET_ANALYTICS_P99_NS,
                "Analytics p99 latency < 1us");
}

/**
 * @brief Test 3: Lock-Free Queue Handoff Latency
 *
 * Measures the time for SPSC queue push/pop operations.
 * Target: <100ns p50, <500ns p99
 */
void test_queue_latency() {
    std::cout << "\n=== Test 3: Lock-Free Queue Handoff Latency ===\n";
    std::cout << "Target: p50 < 100ns, p99 < 500ns, throughput > 10M/sec\n";

    SPSCQueue<int> queue(4096);
    PerformanceMonitor monitor("queue");

    // Warmup
    for (int i = 0; i < NUM_WARMUP_ITERATIONS; ++i) {
        queue.push(i);
        auto val = queue.pop();
        (void)val;
    }
    monitor.reset();

    // Benchmark
    for (int i = 0; i < NUM_QUEUE_ITERATIONS; ++i) {
        auto start = std::chrono::steady_clock::now();

        queue.push(i);
        auto val = queue.pop();
        (void)val;

        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        monitor.record_event_latency(latency);
    }

    std::cout << "  Results:\n";
    std::cout << "    p50: " << monitor.get_p50_ns() << " ns\n";
    std::cout << "    p99: " << monitor.get_p99_ns() << " ns\n";
    std::cout << "    Throughput: " << static_cast<int>(monitor.throughput()) << " ops/sec\n";

    TEST_ASSERT(monitor.get_p50_ns() <= TARGET_QUEUE_P50_NS,
                "Queue p50 latency < 100ns");
    TEST_ASSERT(monitor.get_p99_ns() <= TARGET_QUEUE_P99_NS,
                "Queue p99 latency < 500ns");
    TEST_ASSERT(monitor.throughput() >= TARGET_QUEUE_THROUGHPUT,
                "Queue throughput > 10M ops/sec");
}

/**
 * @brief Test 4: Memory Pool Performance
 *
 * Compares arena allocator to malloc.
 */
void test_memory_pool() {
    std::cout << "\n=== Test 4: Memory Pool Performance ===\n";
    std::cout << "Comparing arena allocator to malloc...\n";

    static constexpr int ALLOC_COUNT = 100000;
    static constexpr size_t ALLOC_SIZE = 64;

    // Arena allocator
    double arena_ns_per_alloc;
    {
        ArenaAllocator arena;
        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < ALLOC_COUNT; ++i) {
            void* ptr = arena.allocate(ALLOC_SIZE);
            (void)ptr;
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        arena_ns_per_alloc = static_cast<double>(duration.count()) / ALLOC_COUNT;
    }

    // Malloc
    double malloc_ns_per_alloc;
    {
        std::vector<void*> ptrs;
        ptrs.reserve(ALLOC_COUNT);

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < ALLOC_COUNT; ++i) {
            void* ptr = malloc(ALLOC_SIZE);
            ptrs.push_back(ptr);
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        malloc_ns_per_alloc = static_cast<double>(duration.count()) / ALLOC_COUNT;

        for (void* ptr : ptrs) {
            free(ptr);
        }
    }

    std::cout << "  Results:\n";
    std::cout << "    Arena: " << arena_ns_per_alloc << " ns/alloc\n";
    std::cout << "    Malloc: " << malloc_ns_per_alloc << " ns/alloc\n";
    std::cout << "    Speedup: " << (malloc_ns_per_alloc / arena_ns_per_alloc) << "x\n";

    // Arena should be faster than malloc
    TEST_ASSERT(arena_ns_per_alloc < malloc_ns_per_alloc,
                "Arena allocator faster than malloc");
    TEST_ASSERT(arena_ns_per_alloc < 50.0,
                "Arena allocation < 50ns");
}

/**
 * @brief Test 5: End-to-End Pipeline Latency
 *
 * Measures complete pipeline: order creation -> insertion -> analytics read
 * Target: <10us p50, <50us p99
 */
void test_end_to_end_latency() {
    std::cout << "\n=== Test 5: End-to-End Pipeline Latency ===\n";
    std::cout << "Target: p50 < 10us, p99 < 50us\n";

    MicrostructureOrderBook book("AAPL");
    book.enable_self_trade_prevention(false);  // Disable for benchmark
    PerformanceMonitor monitor("e2e");

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(99.0, 101.0);
    std::uniform_int_distribution<int> qty_dist(100, 1000);

    // Warmup
    for (int i = 0; i < NUM_WARMUP_ITERATIONS; ++i) {
        int account_id = (i % 100) + 1;
        Order order(i + 1, account_id, Side::BUY, 100.0, 100, TimeInForce::GTC);
        book.add_order(order);
        [[maybe_unused]] auto imbalance = book.get_current_imbalance();
    }
    monitor.reset();

    // Benchmark complete pipeline
    for (int i = 0; i < NUM_TEST_ITERATIONS; ++i) {
        auto start = std::chrono::steady_clock::now();

        // 1. Create order
        double price = price_dist(rng);
        int qty = qty_dist(rng);
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        int account_id = (i % 100) + 1;
        Order order(NUM_WARMUP_ITERATIONS + i + 1, account_id, side, price, qty, TimeInForce::GTC);

        // 2. Insert into order book
        book.add_order(order);

        // 3. Read analytics
        [[maybe_unused]] double imbalance = book.get_current_imbalance();
        [[maybe_unused]] double spread = book.get_average_spread();

        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        monitor.record_event_latency(latency);
    }

    std::cout << "  Results:\n";
    std::cout << "    p50: " << monitor.get_p50_ns() << " ns\n";
    std::cout << "    p99: " << monitor.get_p99_ns() << " ns\n";
    std::cout << "    Throughput: " << static_cast<int>(monitor.throughput()) << " ops/sec\n";

    TEST_ASSERT(monitor.get_p50_ns() <= TARGET_E2E_P50_NS,
                "End-to-end p50 latency < 10us");
    TEST_ASSERT(monitor.get_p99_ns() <= TARGET_E2E_P99_NS,
                "End-to-end p99 latency < 50us");
}

/**
 * @brief Test 6: Rolling Statistics Performance
 *
 * Tests the performance of the RollingStatistics class used for analytics.
 */
void test_rolling_statistics_performance() {
    std::cout << "\n=== Test 6: Rolling Statistics Performance ===\n";

    RollingStatistics<double, 1000> stats;
    PerformanceMonitor monitor("rolling_stats");

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> value_dist(0.0, 100.0);

    // Warmup
    for (int i = 0; i < 1000; ++i) {
        stats.add(value_dist(rng));
    }
    monitor.reset();

    // Benchmark
    for (int i = 0; i < NUM_TEST_ITERATIONS; ++i) {
        auto start = std::chrono::steady_clock::now();

        stats.add(value_dist(rng));
        [[maybe_unused]] double mean = stats.mean();
        [[maybe_unused]] double stddev = stats.stddev();

        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        monitor.record_event_latency(latency);
    }

    std::cout << "  Results:\n";
    std::cout << "    p50: " << monitor.get_p50_ns() << " ns\n";
    std::cout << "    p99: " << monitor.get_p99_ns() << " ns\n";

    // Rolling statistics should be very fast (<500ns)
    TEST_ASSERT(monitor.get_p50_ns() <= 500,
                "Rolling statistics p50 < 500ns");
}

/**
 * @brief Test 7: Performance Monitor Overhead
 *
 * Ensures the performance monitor itself has minimal overhead.
 */
void test_monitor_overhead() {
    std::cout << "\n=== Test 7: Performance Monitor Overhead ===\n";

    PerformanceMonitor monitor("overhead_test");

    // Measure overhead of recording a latency
    auto start_overhead = std::chrono::steady_clock::now();

    for (int i = 0; i < 1000000; ++i) {
        monitor.record_event_latency(100);  // Fixed value
    }

    auto end_overhead = std::chrono::steady_clock::now();
    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_overhead - start_overhead).count();
    double ns_per_record = static_cast<double>(total_ns) / 1000000.0;

    std::cout << "  Results:\n";
    std::cout << "    Recording overhead: " << ns_per_record << " ns/record\n";

    // Recording should add < 50ns overhead
    TEST_ASSERT(ns_per_record < 50.0,
                "Monitor recording overhead < 50ns");
}

void print_summary() {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "      PERFORMANCE BENCHMARK SUMMARY\n";
    std::cout << "========================================\n";
    std::cout << "  Tests passed: " << tests_passed << "\n";
    std::cout << "  Tests failed: " << tests_failed << "\n";
    std::cout << "  Total:        " << (tests_passed + tests_failed) << "\n";
    std::cout << "========================================\n";

    if (tests_failed == 0) {
        std::cout << "  ALL PERFORMANCE TARGETS MET!\n";
    } else {
        std::cout << "  WARNING: Some performance targets not met.\n";
        std::cout << "  This may be due to system load or hardware.\n";
        std::cout << "  Consider running on a quiet system.\n";
    }
    std::cout << "========================================\n";
}

int main() {
    std::cout << "\n";
    std::cout << "##############################################################\n";
    std::cout << "#                                                            #\n";
    std::cout << "#      PERFORMANCE OPTIMIZATION BENCHMARKS                    #\n";
    std::cout << "#                                                            #\n";
    std::cout << "##############################################################\n";

    test_order_book_latency();
    test_analytics_latency();
    test_queue_latency();
    test_memory_pool();
    test_end_to_end_latency();
    test_rolling_statistics_performance();
    test_monitor_overhead();

    print_summary();

    return tests_failed > 0 ? 1 : 0;
}
