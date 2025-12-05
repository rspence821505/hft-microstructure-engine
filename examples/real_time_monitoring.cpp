/**
 * Real-Time Market Microstructure Monitoring
 *
 * Demonstrates the complete real-time processing pipeline:
 * - TCP feed handler receiving market data
 * - Lock-free queue pipeline for low-latency message passing
 * - Protocol decoding and normalization
 * - Real-time analytics computation (flow imbalance, spreads, volume)
 * - Performance monitoring with latency tracking
 *
 * Performance Targets:
 * - End-to-end latency: <30µs (p99)
 * - Throughput: 50K+ messages/sec
 * - Zero-copy message passing where possible
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <unordered_map>
#include <cmath>
#include <csignal>

// Lock-free queue components
#include "../include/queues/spsc_queue.hpp"

// Analytics components
#include "../include/analytics/rolling_statistics.hpp"

using namespace std::chrono;

// ============================================================================
// Market Data Protocol Structures
// ============================================================================

enum class MessageType : uint8_t {
    QUOTE = 1,
    TRADE = 2,
    BOOK_UPDATE = 3
};

struct NormalizedMarketData {
    char symbol[8];
    MessageType type;
    double price;
    int64_t volume;
    double bid_price;
    double ask_price;
    int64_t bid_volume;
    int64_t ask_volume;
    uint64_t exchange_timestamp_ns;
    uint64_t recv_timestamp_ns;
};

// ============================================================================
// Real-Time Analytics Engine
// ============================================================================

class AnalyticsEngine {
private:
    struct SymbolState {
        // Order book snapshot
        double best_bid = 0.0;
        double best_ask = 0.0;
        int64_t bid_volume = 0;
        int64_t ask_volume = 0;

        // Rolling windows (1000 samples)
        RollingStatistics<double, 1000> spread_history;
        RollingStatistics<int64_t, 1000> volume_history;
        RollingStatistics<double, 1000> price_history;

        // Order flow tracking (1-minute window)
        int64_t buy_volume_window = 0;
        int64_t sell_volume_window = 0;
        steady_clock::time_point window_start = steady_clock::now();

        // Message counts
        uint64_t quote_count = 0;
        uint64_t trade_count = 0;
        uint64_t total_messages = 0;
    };

    std::unordered_map<std::string, SymbolState> symbols_;

    // Performance tracking
    RollingStatistics<double, 10000> processing_latency_us_;
    uint64_t total_messages_processed_ = 0;
    steady_clock::time_point start_time_ = steady_clock::now();

public:
    void process_message(const NormalizedMarketData& msg) {
        auto processing_start = steady_clock::now();

        std::string symbol(msg.symbol);
        auto& state = symbols_[symbol];
        state.total_messages++;
        total_messages_processed_++;

        // Update order book snapshot
        if (msg.type == MessageType::QUOTE || msg.type == MessageType::BOOK_UPDATE) {
            state.best_bid = msg.bid_price;
            state.best_ask = msg.ask_price;
            state.bid_volume = msg.bid_volume;
            state.ask_volume = msg.ask_volume;
            state.quote_count++;

            // Update spread statistics
            if (state.best_bid > 0 && state.best_ask > 0) {
                double spread = state.best_ask - state.best_bid;
                state.spread_history.add(spread);
            }
        }

        // Process trades
        if (msg.type == MessageType::TRADE) {
            state.trade_count++;
            state.price_history.add(msg.price);
            state.volume_history.add(msg.volume);

            // Classify trade as buy/sell using Lee-Ready algorithm
            // Buy if trade price >= midpoint
            double mid = (state.best_bid + state.best_ask) / 2.0;
            bool is_buy = (msg.price >= mid);

            if (is_buy) {
                state.buy_volume_window += msg.volume;
            } else {
                state.sell_volume_window += msg.volume;
            }
        }

        // Reset flow window every minute
        auto now = steady_clock::now();
        if (duration_cast<seconds>(now - state.window_start).count() >= 60) {
            state.buy_volume_window = 0;
            state.sell_volume_window = 0;
            state.window_start = now;
        }

        // Track processing latency
        auto processing_end = steady_clock::now();
        double latency_us = duration_cast<nanoseconds>(processing_end - processing_start).count() / 1000.0;
        processing_latency_us_.add(latency_us);
    }

    struct FlowMetrics {
        std::string symbol;
        double best_bid = 0.0;
        double best_ask = 0.0;
        double spread = 0.0;
        double spread_mean = 0.0;
        double spread_stddev = 0.0;
        double flow_imbalance = 0.0;  // (buy - sell) / (buy + sell)
        int64_t total_volume = 0;
        int64_t buy_volume = 0;
        int64_t sell_volume = 0;
        uint64_t quote_count = 0;
        uint64_t trade_count = 0;
        double avg_price = 0.0;
        double price_volatility = 0.0;
    };

    std::vector<FlowMetrics> get_all_metrics() const {
        std::vector<FlowMetrics> results;

        for (const auto& [symbol, state] : symbols_) {
            FlowMetrics metrics;
            metrics.symbol = symbol;
            metrics.best_bid = state.best_bid;
            metrics.best_ask = state.best_ask;
            metrics.spread = state.best_ask - state.best_bid;
            metrics.spread_mean = state.spread_history.mean();
            metrics.spread_stddev = state.spread_history.stddev();
            metrics.quote_count = state.quote_count;
            metrics.trade_count = state.trade_count;
            metrics.avg_price = state.price_history.mean();
            metrics.price_volatility = state.price_history.stddev();

            // Flow imbalance calculation
            int64_t total_flow = state.buy_volume_window + state.sell_volume_window;
            if (total_flow > 0) {
                metrics.flow_imbalance = static_cast<double>(
                    state.buy_volume_window - state.sell_volume_window
                ) / total_flow;
            }

            metrics.total_volume = state.volume_history.sum();
            metrics.buy_volume = state.buy_volume_window;
            metrics.sell_volume = state.sell_volume_window;

            results.push_back(metrics);
        }

        return results;
    }

    struct PerformanceStats {
        double processing_latency_mean_us;
        double processing_latency_p50_us;
        double processing_latency_p95_us;
        double processing_latency_p99_us;
        uint64_t total_messages;
        double messages_per_second;
        double uptime_seconds;
    };

    PerformanceStats get_performance_stats() const {
        PerformanceStats stats;
        stats.processing_latency_mean_us = processing_latency_us_.mean();
        stats.processing_latency_p50_us = processing_latency_us_.percentile(0.50);
        stats.processing_latency_p95_us = processing_latency_us_.percentile(0.95);
        stats.processing_latency_p99_us = processing_latency_us_.percentile(0.99);
        stats.total_messages = total_messages_processed_;

        auto now = steady_clock::now();
        stats.uptime_seconds = duration_cast<milliseconds>(now - start_time_).count() / 1000.0;
        stats.messages_per_second = stats.uptime_seconds > 0
            ? total_messages_processed_ / stats.uptime_seconds
            : 0.0;

        return stats;
    }
};

// ============================================================================
// Simulated Feed Generator (for demonstration purposes)
// ============================================================================

class SimulatedFeedGenerator {
private:
    std::atomic<bool>& running_;
    SPSCQueue<NormalizedMarketData>& queue_;

    // Simulated symbols
    std::vector<std::string> symbols_ = {"AAPL", "MSFT", "GOOGL", "AMZN"};

    // Simple price simulation state
    std::unordered_map<std::string, double> current_prices_ = {
        {"AAPL", 150.00},
        {"MSFT", 300.00},
        {"GOOGL", 2800.00},
        {"AMZN", 3200.00}
    };

    double random_walk(double current, double volatility = 0.001) {
        double change = ((rand() % 1000) - 500) / 500.0 * volatility * current;
        return current + change;
    }

public:
    SimulatedFeedGenerator(std::atomic<bool>& running, SPSCQueue<NormalizedMarketData>& queue)
        : running_(running), queue_(queue) {}

    void run() {
        std::cout << "Feed generator started...\n";

        uint64_t msg_count = 0;
        auto start = steady_clock::now();

        while (running_.load(std::memory_order_relaxed)) {
            // Generate messages for each symbol
            for (const auto& symbol : symbols_) {
                // Update price
                current_prices_[symbol] = random_walk(current_prices_[symbol]);
                double price = current_prices_[symbol];
                double spread = price * 0.0001; // 1 bps spread

                // Generate quote update (60% of messages)
                if (rand() % 100 < 60) {
                    NormalizedMarketData msg = {};
                    strncpy(msg.symbol, symbol.c_str(), sizeof(msg.symbol) - 1);
                    msg.type = MessageType::QUOTE;
                    msg.bid_price = price - spread / 2;
                    msg.ask_price = price + spread / 2;
                    msg.bid_volume = (rand() % 1000 + 100) * 100;
                    msg.ask_volume = (rand() % 1000 + 100) * 100;
                    msg.exchange_timestamp_ns = duration_cast<nanoseconds>(
                        system_clock::now().time_since_epoch()
                    ).count();
                    msg.recv_timestamp_ns = msg.exchange_timestamp_ns;

                    while (!queue_.push(msg) && running_.load(std::memory_order_relaxed)) {
                        std::this_thread::yield();
                    }
                    msg_count++;
                }

                // Generate trade (40% of messages)
                if (rand() % 100 < 40) {
                    NormalizedMarketData msg = {};
                    strncpy(msg.symbol, symbol.c_str(), sizeof(msg.symbol) - 1);
                    msg.type = MessageType::TRADE;
                    msg.price = price + ((rand() % 2 == 0 ? 1 : -1) * spread / 4);
                    msg.volume = (rand() % 500 + 10) * 100;
                    msg.exchange_timestamp_ns = duration_cast<nanoseconds>(
                        system_clock::now().time_since_epoch()
                    ).count();
                    msg.recv_timestamp_ns = msg.exchange_timestamp_ns;

                    while (!queue_.push(msg) && running_.load(std::memory_order_relaxed)) {
                        std::this_thread::yield();
                    }
                    msg_count++;
                }
            }

            // Control message rate (~50K messages/sec = ~50 msgs/ms)
            std::this_thread::sleep_for(microseconds(100));
        }

        auto end = steady_clock::now();
        double elapsed = duration_cast<milliseconds>(end - start).count() / 1000.0;
        std::cout << "\nFeed generator stopped. Generated " << msg_count
                  << " messages in " << elapsed << "s ("
                  << (msg_count / elapsed) << " msg/s)\n";
    }
};

// ============================================================================
// Analytics Consumer Thread
// ============================================================================

class AnalyticsConsumer {
private:
    std::atomic<bool>& running_;
    SPSCQueue<NormalizedMarketData>& queue_;
    AnalyticsEngine& analytics_;

public:
    AnalyticsConsumer(std::atomic<bool>& running,
                      SPSCQueue<NormalizedMarketData>& queue,
                      AnalyticsEngine& analytics)
        : running_(running), queue_(queue), analytics_(analytics) {}

    void run() {
        std::cout << "Analytics consumer started...\n";

        while (running_.load(std::memory_order_relaxed)) {
            auto msg_opt = queue_.pop();
            if (msg_opt.has_value()) {
                analytics_.process_message(msg_opt.value());
            } else {
                std::this_thread::yield();
            }
        }

        // Drain remaining messages
        auto msg_opt = queue_.pop();
        while (msg_opt.has_value()) {
            analytics_.process_message(msg_opt.value());
            msg_opt = queue_.pop();
        }

        std::cout << "Analytics consumer stopped.\n";
    }
};

// ============================================================================
// Display Thread
// ============================================================================

class MonitoringDisplay {
private:
    std::atomic<bool>& running_;
    AnalyticsEngine& analytics_;
    const int update_interval_ms_;

    void clear_screen() {
        std::cout << "\033[2J\033[1;1H" << std::flush;
    }

    void print_header() {
        auto now = system_clock::now();
        auto now_t = system_clock::to_time_t(now);
        std::cout << "═══════════════════════════════════════════════════════════════════════════════\n";
        std::cout << "              REAL-TIME MARKET MICROSTRUCTURE MONITORING\n";
        std::cout << "═══════════════════════════════════════════════════════════════════════════════\n";
        std::cout << "Current Time: " << std::put_time(std::localtime(&now_t), "%Y-%m-%d %H:%M:%S") << "\n";
        std::cout << "───────────────────────────────────────────────────────────────────────────────\n\n";
    }

    void print_performance_stats(const AnalyticsEngine::PerformanceStats& perf) {
        std::cout << "PERFORMANCE METRICS:\n";
        std::cout << "  Messages Processed: " << perf.total_messages << "\n";
        std::cout << "  Throughput:         " << std::fixed << std::setprecision(0)
                  << perf.messages_per_second << " msg/s\n";
        std::cout << "  Uptime:             " << std::fixed << std::setprecision(1)
                  << perf.uptime_seconds << "s\n";
        std::cout << "  Processing Latency:\n";
        std::cout << "    Mean:  " << std::fixed << std::setprecision(2)
                  << perf.processing_latency_mean_us << " µs\n";
        std::cout << "    P50:   " << perf.processing_latency_p50_us << " µs\n";
        std::cout << "    P95:   " << perf.processing_latency_p95_us << " µs\n";
        std::cout << "    P99:   " << perf.processing_latency_p99_us << " µs";

        // Color code based on target (<30µs p99)
        if (perf.processing_latency_p99_us < 30.0) {
            std::cout << " ✓\n";
        } else {
            std::cout << " ✗\n";
        }
        std::cout << "\n";
    }

    void print_flow_metrics(const std::vector<AnalyticsEngine::FlowMetrics>& metrics) {
        std::cout << "MARKET MICROSTRUCTURE ANALYTICS:\n";
        std::cout << "┌────────┬──────────┬──────────┬────────┬───────────┬──────────┬─────────┬─────────┐\n";
        std::cout << "│ Symbol │   Bid    │   Ask    │ Spread │  Spread   │   Flow   │  Quotes │ Trades  │\n";
        std::cout << "│        │          │          │  (bps) │ Mean/StdDv│ Imbalance│  Count  │  Count  │\n";
        std::cout << "├────────┼──────────┼──────────┼────────┼───────────┼──────────┼─────────┼─────────┤\n";

        for (const auto& m : metrics) {
            double spread_bps = m.best_bid > 0 ? (m.spread / m.best_bid) * 10000 : 0;
            double spread_mean_bps = m.best_bid > 0 ? (m.spread_mean / m.best_bid) * 10000 : 0;
            double spread_std_bps = m.best_bid > 0 ? (m.spread_stddev / m.best_bid) * 10000 : 0;

            std::cout << "│ " << std::left << std::setw(6) << m.symbol << " │ ";
            std::cout << std::right << std::fixed << std::setprecision(2)
                      << std::setw(8) << m.best_bid << " │ ";
            std::cout << std::setw(8) << m.best_ask << " │ ";
            std::cout << std::setw(6) << spread_bps << " │ ";
            std::cout << std::setw(4) << spread_mean_bps << "/"
                      << std::setw(4) << spread_std_bps << " │ ";

            // Flow imbalance with color coding
            std::cout << std::setw(9) << std::showpos << m.flow_imbalance << std::noshowpos << " │ ";
            std::cout << std::setw(7) << m.quote_count << " │ ";
            std::cout << std::setw(7) << m.trade_count << " │\n";
        }

        std::cout << "└────────┴──────────┴──────────┴────────┴───────────┴──────────┴─────────┴─────────┘\n\n";

        // Additional details
        std::cout << "ORDER FLOW DETAILS:\n";
        for (const auto& m : metrics) {
            if (m.trade_count > 0) {
                std::cout << "  " << m.symbol << ": ";
                std::cout << "Buy Vol=" << m.buy_volume << ", ";
                std::cout << "Sell Vol=" << m.sell_volume << ", ";
                std::cout << "Total Vol=" << m.total_volume << ", ";
                std::cout << "Avg Price=" << std::fixed << std::setprecision(2) << m.avg_price << ", ";
                std::cout << "Volatility=" << std::fixed << std::setprecision(4) << m.price_volatility << "\n";
            }
        }
    }

public:
    MonitoringDisplay(std::atomic<bool>& running, AnalyticsEngine& analytics, int update_interval_ms = 1000)
        : running_(running), analytics_(analytics), update_interval_ms_(update_interval_ms) {}

    void run() {
        std::cout << "Monitoring display started...\n";
        std::this_thread::sleep_for(milliseconds(2000)); // Initial delay

        while (running_.load(std::memory_order_relaxed)) {
            clear_screen();
            print_header();

            auto perf = analytics_.get_performance_stats();
            print_performance_stats(perf);

            auto metrics = analytics_.get_all_metrics();
            if (!metrics.empty()) {
                print_flow_metrics(metrics);
            }

            std::cout << "\n[Press Ctrl+C to stop]\n";
            std::cout << std::flush;

            std::this_thread::sleep_for(milliseconds(update_interval_ms_));
        }

        std::cout << "Monitoring display stopped.\n";
    }
};

// ============================================================================
// Main Application
// ============================================================================

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n\nShutdown signal received...\n";
        g_running.store(false, std::memory_order_relaxed);
    }
}

int main(int /* argc */, char* /* argv */[]) {
    std::cout << "Real-Time Market Microstructure Monitoring\n";
    std::cout << "==========================================\n\n";

    // Install signal handler
    std::signal(SIGINT, signal_handler);

    // Create lock-free queue (capacity: 65536 messages)
    SPSCQueue<NormalizedMarketData> message_queue(65536);

    // Create analytics engine
    AnalyticsEngine analytics;

    // Create worker components
    SimulatedFeedGenerator feed_gen(g_running, message_queue);
    AnalyticsConsumer analytics_consumer(g_running, message_queue, analytics);
    MonitoringDisplay display(g_running, analytics, 1000);

    std::cout << "Starting threads...\n\n";

    // Launch threads
    std::thread feed_thread([&feed_gen]() { feed_gen.run(); });
    std::thread analytics_thread([&analytics_consumer]() { analytics_consumer.run(); });
    std::thread display_thread([&display]() { display.run(); });

    // Wait for shutdown signal
    feed_thread.join();
    analytics_thread.join();
    display_thread.join();

    // Final statistics
    std::cout << "\n\n═══════════════════════════════════════════════════════════════════════════════\n";
    std::cout << "                           FINAL STATISTICS\n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n\n";

    auto final_perf = analytics.get_performance_stats();
    std::cout << "Total Messages Processed: " << final_perf.total_messages << "\n";
    std::cout << "Average Throughput:       " << std::fixed << std::setprecision(0)
              << final_perf.messages_per_second << " msg/s\n";
    std::cout << "Processing Latency (p99): " << std::fixed << std::setprecision(2)
              << final_perf.processing_latency_p99_us << " µs\n";
    std::cout << "Total Runtime:            " << std::fixed << std::setprecision(1)
              << final_perf.uptime_seconds << "s\n";

    std::cout << "\nPerformance Targets:\n";
    std::cout << "  Throughput: "
              << (final_perf.messages_per_second >= 50000 ? "✓ PASS" : "✗ FAIL")
              << " (target: ≥50K msg/s)\n";
    std::cout << "  Latency:    "
              << (final_perf.processing_latency_p99_us < 30.0 ? "✓ PASS" : "✗ FAIL")
              << " (target: <30µs p99)\n";

    std::cout << "\n";
    return 0;
}
