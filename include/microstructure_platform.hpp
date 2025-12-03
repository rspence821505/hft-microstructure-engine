#ifndef MICROSTRUCTURE_PLATFORM_HPP
#define MICROSTRUCTURE_PLATFORM_HPP

/**
 * @file microstructure_platform.hpp
 * @brief Week 4.1: Connect All Components - Main Integration Platform
 *
 * This is the central integration point for the Microstructure Analytics Platform.
 * It connects:
 *   - MicrostructureBacktester (Historical analysis from Data-Parser)
 *   - MultiFeedAggregator (Real-time feeds from TCP-Socket)
 *   - MicrostructureOrderBook (Order book from Matching-Engine)
 *   - MicrostructureAnalytics (Flow tracking, impact calibration)
 *   - ExecutionSimulator (Strategy testing)
 *   - PerformanceMonitor (Latency tracking)
 *
 * Architecture:
 *   Historical Layer:
 *     CSV Parser --> Event Timeline --> Backtesting Engine --> Impact Model Calibration
 *
 *   Real-Time Layer:
 *     TCP Feed --> Ring Buffer --> Lock-Free Queue --> Protocol Parser --> Analytics Engine
 *
 *   Execution Layer:
 *     Order Book --> Fill Router --> Execution Algorithms --> Cost Analyzer
 */

#include "execution_algorithm.hpp"
#include "execution_simulator.hpp"
#include "market_impact_calibration.hpp"
#include "microstructure_analytics.hpp"
#include "microstructure_backtester.hpp"
#include "microstructure_order_book.hpp"
#include "multi_feed_aggregator.hpp"
#include "performance_monitor.hpp"
#include "twap_strategy.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/**
 * @struct PlatformConfig
 * @brief Configuration for the analytics platform
 */
struct PlatformConfig {
    // Historical analysis
    std::string historical_data_file = "";
    uint64_t assumed_adv = 10000000;
    double impact_coefficient = 0.01;

    // Real-time feeds
    std::vector<FeedSource> feed_sources;

    // Analytics
    int flow_window_seconds = 60;
    bool track_per_symbol = true;
    bool auto_calibrate_impact = false;

    // Performance
    bool enable_performance_monitoring = true;
    bool verbose = false;

    // Callbacks
    bool enable_analytics_updates = true;
    int analytics_update_interval_ms = 10000;  // 10 seconds
};

/**
 * @struct AnalyticsSnapshot
 * @brief Point-in-time snapshot of analytics state
 */
struct AnalyticsSnapshot {
    // Order book state
    double spread = 0.0;
    double best_bid = 0.0;
    double best_ask = 0.0;
    double order_imbalance = 0.0;

    // Order flow
    struct FlowState {
        double imbalance = 0.0;
        double buy_ratio = 0.0;
        int64_t buy_volume = 0;
        int64_t sell_volume = 0;
    } flow;

    // Market impact
    double estimated_impact_100k = 0.0;
    double estimated_impact_1m = 0.0;

    // Performance
    uint64_t events_processed = 0;
    double throughput = 0.0;
    uint64_t latency_p50_ns = 0;
    uint64_t latency_p99_ns = 0;

    // Timestamp
    std::chrono::steady_clock::time_point timestamp;
};

/**
 * @struct ExecutionComparison
 * @brief Results from comparing multiple execution algorithms
 */
struct ExecutionComparison {
    struct AlgorithmResult {
        std::string name;
        double avg_price = 0.0;
        double implementation_shortfall_bps = 0.0;
        double fill_rate = 0.0;
        size_t num_trades = 0;
        double execution_time_ms = 0.0;
    };

    std::vector<AlgorithmResult> results;
    double arrival_price = 0.0;
    uint64_t target_quantity = 0;
    std::string symbol;

    void print() const {
        std::cout << "\n=== Execution Strategy Comparison ===\n";
        std::cout << "Symbol: " << symbol << "\n";
        std::cout << "Target quantity: " << target_quantity << "\n";
        std::cout << "Arrival price: " << arrival_price << "\n\n";

        std::cout << std::left
                  << std::setw(20) << "Algorithm"
                  << std::setw(15) << "Avg Price"
                  << std::setw(18) << "Shortfall (bps)"
                  << std::setw(12) << "Fill Rate"
                  << std::setw(10) << "Trades"
                  << "\n";
        std::cout << std::string(75, '-') << "\n";

        for (const auto& r : results) {
            std::cout << std::left
                      << std::setw(20) << r.name
                      << std::setw(15) << std::fixed << std::setprecision(2) << r.avg_price
                      << std::setw(18) << std::setprecision(2) << r.implementation_shortfall_bps
                      << std::setw(12) << std::setprecision(1) << (r.fill_rate * 100) << "%"
                      << std::setw(10) << r.num_trades
                      << "\n";
        }
    }
};

/**
 * @class MicrostructureAnalyticsPlatform
 * @brief Main integration class connecting all components
 *
 * This class provides:
 * 1. Historical data analysis with market impact calibration
 * 2. Real-time feed processing with flow analytics
 * 3. Execution strategy testing and comparison
 * 4. Comprehensive performance monitoring
 */
class MicrostructureAnalyticsPlatform {
private:
    PlatformConfig config_;

    // Historical analysis
    std::unique_ptr<MicrostructureBacktester> backtester_;

    // Real-time processing
    std::unique_ptr<MultiFeedAggregator> feed_aggregator_;
    std::unique_ptr<MicrostructureOrderBook> order_book_;
    std::unique_ptr<MicrostructureAnalytics> analytics_;

    // Execution testing
    std::unique_ptr<ExecutionSimulator> simulator_;

    // Performance monitoring
    std::unique_ptr<PerformanceMonitor> performance_monitor_;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::thread analytics_update_thread_;
    std::mutex state_mutex_;

    // Calibrated impact model
    MarketImpactModel calibrated_impact_model_;
    bool has_calibrated_model_ = false;

public:
    /**
     * @brief Constructs the platform with configuration
     * @param config Platform configuration
     */
    explicit MicrostructureAnalyticsPlatform(const PlatformConfig& config = PlatformConfig())
        : config_(config) {}

    ~MicrostructureAnalyticsPlatform() {
        stop();
    }

    /**
     * @brief Initializes all platform components
     */
    void initialize() {
        if (initialized_) return;

        std::lock_guard<std::mutex> lock(state_mutex_);

        // Initialize backtester
        BacktesterConfig bt_config;
        bt_config.input_filename = config_.historical_data_file;
        bt_config.assumed_adv = config_.assumed_adv;
        bt_config.impact_coefficient = config_.impact_coefficient;
        backtester_ = std::make_unique<MicrostructureBacktester>(bt_config);

        // Initialize order book
        order_book_ = std::make_unique<MicrostructureOrderBook>("DEFAULT");

        // Initialize analytics
        analytics_ = std::make_unique<MicrostructureAnalytics>(config_.flow_window_seconds);
        analytics_->set_per_symbol_tracking(config_.track_per_symbol);
        analytics_->set_auto_calibrate(config_.auto_calibrate_impact);

        // Connect analytics to order book
        analytics_->connect_to_order_book(*order_book_);

        // Initialize feed aggregator
        feed_aggregator_ = std::make_unique<MultiFeedAggregator>();
        feed_aggregator_->set_verbose(config_.verbose);

        // Add configured feeds
        for (const auto& source : config_.feed_sources) {
            feed_aggregator_->add_feed(source);
        }

        // Set up tick callback to update order book
        feed_aggregator_->set_tick_callback([this](const AggregatedTick& tick) {
            on_aggregated_tick(tick);
        });

        // Initialize execution simulator
        SimulationConfig sim_config;
        sim_config.adv = config_.assumed_adv;
        simulator_ = std::make_unique<ExecutionSimulator>(sim_config);

        // Initialize performance monitor
        performance_monitor_ = std::make_unique<PerformanceMonitor>();
        performance_monitor_->set_enabled(config_.enable_performance_monitoring);

        initialized_ = true;

        if (config_.verbose) {
            std::cout << "[Platform] Initialized successfully\n";
        }
    }

    // ========================================================================
    // HISTORICAL ANALYSIS
    // ========================================================================

    /**
     * @brief Loads historical data and builds event timeline
     * @param filename Path to CSV file
     */
    void load_historical_data(const std::string& filename) {
        ensure_initialized();

        if (config_.verbose) {
            std::cout << "[Platform] Loading historical data from: " << filename << "\n";
        }

        backtester_->build_event_timeline(filename);

        if (config_.verbose) {
            std::cout << "[Platform] Loaded " << backtester_->timeline_size() << " events\n";
        }
    }

    /**
     * @brief Calibrates market impact model from historical data
     * @param symbol Symbol to calibrate (uses all if empty)
     * @return Calibrated impact model
     */
    MarketImpactModel calibrate_impact_model(const std::string& symbol = "") {
        ensure_initialized();

        if (config_.verbose) {
            std::cout << "[Platform] Calibrating impact model for: "
                      << (symbol.empty() ? "ALL" : symbol) << "\n";
        }

        calibrated_impact_model_ = backtester_->calibrate_impact_model(symbol);
        has_calibrated_model_ = true;

        // Update simulator with calibrated model
        simulator_->set_impact_model(calibrated_impact_model_);

        // Update analytics
        analytics_->get_calibrated_impact_model() = calibrated_impact_model_;
        analytics_->set_use_calibrated_model(true);

        const auto& params = calibrated_impact_model_.get_parameters();
        if (config_.verbose) {
            std::cout << "[Platform] Calibration complete:\n";
            std::cout << "  Permanent coeff: " << params.permanent_impact_coeff << "\n";
            std::cout << "  Temporary coeff: " << params.temporary_impact_coeff << "\n";
            std::cout << "  Exponent: " << params.impact_exponent << "\n";
            std::cout << "  R²: " << params.r_squared << "\n";
        }

        return calibrated_impact_model_;
    }

    /**
     * @brief Analyzes historical data and prints summary
     * @param filename Path to CSV file
     */
    void analyze_historical_data(const std::string& filename) {
        ensure_initialized();

        std::cout << "\n=== Historical Microstructure Analysis ===\n";

        load_historical_data(filename);
        backtester_->print_timeline_stats();

        // Calibrate impact model
        auto impact_model = calibrate_impact_model();

        std::cout << "\nImpact model parameters:\n";
        const auto& ip = impact_model.get_parameters();
        std::cout << "  Permanent coefficient: " << ip.permanent_impact_coeff << "\n";
        std::cout << "  Temporary coefficient: " << ip.temporary_impact_coeff << "\n";
        std::cout << "  Impact exponent: " << ip.impact_exponent << "\n";
    }

    /**
     * @brief Gets the backtester for direct access
     * @return Reference to backtester
     */
    MicrostructureBacktester& get_backtester() {
        ensure_initialized();
        return *backtester_;
    }

    // ========================================================================
    // REAL-TIME MODE
    // ========================================================================

    /**
     * @brief Adds a feed source for real-time processing
     * @param name Source identifier
     * @param host Server hostname
     * @param port Server port
     */
    void add_feed(const std::string& name, const std::string& host, uint16_t port) {
        ensure_initialized();
        feed_aggregator_->add_feed(name, host, port);
    }

    /**
     * @brief Starts real-time mode with configured feeds
     * @return true if at least one feed connected
     */
    bool start_real_time_mode() {
        ensure_initialized();

        if (running_) return true;

        if (config_.verbose) {
            std::cout << "[Platform] Starting real-time mode...\n";
        }

        if (!feed_aggregator_->start_all()) {
            std::cerr << "[Platform] Failed to start feeds\n";
            return false;
        }

        running_ = true;

        // Start analytics update thread
        if (config_.enable_analytics_updates) {
            analytics_update_thread_ = std::thread([this]() {
                while (running_) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(config_.analytics_update_interval_ms));

                    if (running_) {
                        print_analytics_snapshot();
                    }
                }
            });
        }

        return true;
    }

    /**
     * @brief Stops real-time mode
     */
    void stop() {
        running_ = false;

        if (analytics_update_thread_.joinable()) {
            analytics_update_thread_.join();
        }

        if (feed_aggregator_) {
            feed_aggregator_->stop();
        }
    }

    /**
     * @brief Waits for real-time mode to complete
     */
    void wait() {
        if (feed_aggregator_) {
            feed_aggregator_->wait();
        }
        running_ = false;
    }

    /**
     * @brief Checks if platform is running
     * @return true if running
     */
    bool is_running() const { return running_; }

    /**
     * @brief Gets current analytics snapshot
     * @return Snapshot of current state
     */
    AnalyticsSnapshot get_snapshot() const {
        AnalyticsSnapshot snapshot;
        snapshot.timestamp = std::chrono::steady_clock::now();

        if (order_book_) {
            if (auto spread = order_book_->get_current_spread()) {
                snapshot.spread = *spread;
            }
            if (auto bid = order_book_->get_best_bid()) {
                snapshot.best_bid = bid->price;
            }
            if (auto ask = order_book_->get_best_ask()) {
                snapshot.best_ask = ask->price;
            }
            snapshot.order_imbalance = order_book_->get_current_imbalance();
        }

        if (analytics_) {
            snapshot.flow.imbalance = analytics_->get_flow_imbalance();
            snapshot.flow.buy_ratio = analytics_->get_buy_ratio();
            snapshot.flow.buy_volume = analytics_->get_flow_tracker().get_total_buy_volume();
            snapshot.flow.sell_volume = analytics_->get_flow_tracker().get_total_sell_volume();
        }

        if (has_calibrated_model_) {
            snapshot.estimated_impact_100k = calibrated_impact_model_.estimate_total_impact(
                100000, config_.assumed_adv);
            snapshot.estimated_impact_1m = calibrated_impact_model_.estimate_total_impact(
                1000000, config_.assumed_adv);
        }

        if (performance_monitor_) {
            snapshot.events_processed = performance_monitor_->events_processed();
            snapshot.throughput = performance_monitor_->throughput();
            snapshot.latency_p50_ns = performance_monitor_->latency_percentile(50);
            snapshot.latency_p99_ns = performance_monitor_->latency_percentile(99);
        }

        return snapshot;
    }

    /**
     * @brief Prints current analytics snapshot
     */
    void print_analytics_snapshot() const {
        auto snapshot = get_snapshot();

        std::cout << "\n=== Analytics Snapshot ===" << std::endl;

        std::cout << "Order Book:\n";
        std::cout << "  Spread: " << snapshot.spread << std::endl;
        std::cout << "  Best Bid: " << snapshot.best_bid << std::endl;
        std::cout << "  Best Ask: " << snapshot.best_ask << std::endl;
        std::cout << "  Order Imbalance: " << snapshot.order_imbalance << std::endl;

        std::cout << "Order Flow:\n";
        std::cout << "  Imbalance: " << snapshot.flow.imbalance << std::endl;
        std::cout << "  Buy ratio: " << snapshot.flow.buy_ratio << std::endl;
        std::cout << "  Buy volume: " << snapshot.flow.buy_volume << std::endl;
        std::cout << "  Sell volume: " << snapshot.flow.sell_volume << std::endl;

        if (has_calibrated_model_) {
            std::cout << "Market Impact:\n";
            std::cout << "  100K shares: " << snapshot.estimated_impact_100k << " bps\n";
            std::cout << "  1M shares: " << snapshot.estimated_impact_1m << " bps\n";
        }

        std::cout << "Performance:\n";
        std::cout << "  Events processed: " << snapshot.events_processed << std::endl;
        std::cout << "  Throughput: " << snapshot.throughput << " evt/sec\n";
        std::cout << "  Latency p50: " << snapshot.latency_p50_ns << " ns\n";
        std::cout << "  Latency p99: " << snapshot.latency_p99_ns << " ns\n";
    }

    // ========================================================================
    // EXECUTION STRATEGY TESTING
    // ========================================================================

    /**
     * @brief Tests execution strategies on historical data
     * @param replay_file Path to historical data file
     * @return Comparison of algorithm performance
     */
    ExecutionComparison test_execution_strategies(const std::string& replay_file) {
        ensure_initialized();

        std::cout << "\n=== Execution Strategy Testing ===" << std::endl;

        // Load data if not already loaded
        if (backtester_->timeline_size() == 0) {
            load_historical_data(replay_file);
        }

        // Calibrate if not already done
        if (!has_calibrated_model_) {
            calibrate_impact_model();
        }

        // Get first symbol from timeline
        std::string symbol = "AAPL";  // Default
        if (!backtester_->get_timeline().empty()) {
            symbol = backtester_->get_timeline()[0].symbol;
        }

        uint64_t target_qty = 100000;

        ExecutionComparison comparison;
        comparison.symbol = symbol;
        comparison.target_quantity = target_qty;

        // Test TWAP strategy (30 minutes)
        TWAPStrategy twap_strategy(target_qty, 30);
        auto twap_result = backtester_->test_execution_strategy(&twap_strategy, symbol, target_qty);

        comparison.arrival_price = twap_result.arrival_price;

        ExecutionComparison::AlgorithmResult twap_algo;
        twap_algo.name = twap_result.algorithm_name;
        twap_algo.avg_price = twap_result.avg_execution_price;
        twap_algo.implementation_shortfall_bps = twap_result.implementation_shortfall_bps;
        twap_algo.fill_rate = twap_result.fill_rate;
        twap_algo.num_trades = twap_result.num_trades;
        twap_algo.execution_time_ms = twap_result.execution_time.count();
        comparison.results.push_back(twap_algo);

        // Test TWAP with different time horizons
        for (int minutes : {15, 60, 120}) {
            TWAPStrategy twap_variant(target_qty, minutes);
            auto result = backtester_->test_execution_strategy(&twap_variant, symbol, target_qty);

            ExecutionComparison::AlgorithmResult algo;
            algo.name = "TWAP-" + std::to_string(minutes) + "min";
            algo.avg_price = result.avg_execution_price;
            algo.implementation_shortfall_bps = result.implementation_shortfall_bps;
            algo.fill_rate = result.fill_rate;
            algo.num_trades = result.num_trades;
            algo.execution_time_ms = result.execution_time.count();
            comparison.results.push_back(algo);
        }

        return comparison;
    }

    /**
     * @brief Gets the execution simulator
     * @return Reference to simulator
     */
    ExecutionSimulator& get_simulator() {
        ensure_initialized();
        return *simulator_;
    }

    // ========================================================================
    // COMPONENT ACCESS
    // ========================================================================

    /**
     * @brief Gets the order book
     * @return Reference to order book
     */
    MicrostructureOrderBook& get_order_book() {
        ensure_initialized();
        return *order_book_;
    }

    /**
     * @brief Gets the analytics engine
     * @return Reference to analytics
     */
    MicrostructureAnalytics& get_analytics() {
        ensure_initialized();
        return *analytics_;
    }

    /**
     * @brief Gets the feed aggregator
     * @return Reference to aggregator
     */
    MultiFeedAggregator& get_feed_aggregator() {
        ensure_initialized();
        return *feed_aggregator_;
    }

    /**
     * @brief Gets the performance monitor
     * @return Reference to monitor
     */
    PerformanceMonitor& get_performance_monitor() {
        ensure_initialized();
        return *performance_monitor_;
    }

    /**
     * @brief Gets the calibrated impact model
     * @return Reference to impact model
     */
    const MarketImpactModel& get_calibrated_impact_model() const {
        return calibrated_impact_model_;
    }

    // ========================================================================
    // REPORTING
    // ========================================================================

    /**
     * @brief Prints comprehensive platform statistics
     */
    void print_full_report() const {
        std::cout << "\n";
        std::cout << "========================================================\n";
        std::cout << "     MICROSTRUCTURE ANALYTICS PLATFORM - FULL REPORT    \n";
        std::cout << "========================================================\n";

        // Analytics summary
        if (analytics_) {
            analytics_->print_summary();
        }

        // Order book summary
        if (order_book_) {
            order_book_->print_analytics_summary();
        }

        // Feed aggregator stats
        if (feed_aggregator_ && feed_aggregator_->feed_count() > 0) {
            feed_aggregator_->print_stats();
        }

        // Performance stats
        if (performance_monitor_) {
            performance_monitor_->print_statistics();
        }

        // Impact model
        if (has_calibrated_model_) {
            std::cout << "\n--- Calibrated Impact Model ---\n";
            const auto& params = calibrated_impact_model_.get_parameters();
            std::cout << "  Permanent coefficient: " << params.permanent_impact_coeff << "\n";
            std::cout << "  Temporary coefficient: " << params.temporary_impact_coeff << "\n";
            std::cout << "  Impact exponent: " << params.impact_exponent << "\n";
            std::cout << "  R²: " << params.r_squared << "\n";
            std::cout << "  Observations: " << params.num_observations << "\n";
        }

        std::cout << "\n========================================================\n";
    }

private:
    /**
     * @brief Ensures platform is initialized
     */
    void ensure_initialized() {
        if (!initialized_) {
            initialize();
        }
    }

    /**
     * @brief Callback for aggregated ticks from feeds
     */
    void on_aggregated_tick(const AggregatedTick& tick) {
        auto start_time = std::chrono::steady_clock::now();

        // Create synthetic order from tick (for demonstration)
        // In production, this would come from actual order flow
        int order_id = static_cast<int>(performance_monitor_->events_processed() + 1);
        int account_id = 1;  // Default account
        Side side = (tick.tick.volume % 2 == 0) ? Side::BUY : Side::SELL;
        double price = tick.tick.price;
        int quantity = static_cast<int>(tick.tick.volume);

        // Use proper Order constructor
        Order order(order_id, account_id, side, price, quantity, TimeInForce::GTC);

        // Add to order book (this triggers analytics via FillRouter)
        order_book_->add_order(order);

        // Record latency
        auto end_time = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        performance_monitor_->record_event_latency(latency);
    }
};

#endif // MICROSTRUCTURE_PLATFORM_HPP
