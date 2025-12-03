#ifndef EXECUTION_SIMULATOR_HPP
#define EXECUTION_SIMULATOR_HPP

#include "execution_algorithm.hpp"
#include "market_impact_calibration.hpp"
#include "microstructure_order_book.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

/**
 * @struct SimulationConfig
 * @brief Configuration for execution simulation
 */
struct SimulationConfig {
    double initial_price = 100.0;      ///< Starting price
    double volatility = 0.02;          ///< Daily volatility (annualized ~30%)
    double spread_bps = 5.0;           ///< Bid-ask spread in basis points
    uint64_t adv = 10000000;           ///< Average daily volume
    double tick_size = 0.01;           ///< Minimum price increment
    int ticks_per_second = 100;        ///< Market data frequency
    double fill_probability = 0.8;    ///< Probability of fill for limit orders
    bool apply_market_impact = true;   ///< Whether to model market impact
    unsigned int random_seed = 42;     ///< Random seed for reproducibility
};

/**
 * @struct SimulationResult
 * @brief Results from running an execution simulation
 */
struct SimulationResult {
    ExecutionReport report;
    std::vector<MarketData> price_path;     ///< Market data history
    std::vector<Fill> fills;                ///< All fills
    double realized_impact_bps = 0.0;       ///< Actual price move during execution
    double predicted_impact_bps = 0.0;      ///< Model-predicted impact
    bool completed = false;                  ///< Whether execution completed

    void print() const {
        report.print();
        std::cout << "  Realized impact: " << realized_impact_bps << " bps\n";
        std::cout << "  Predicted impact: " << predicted_impact_bps << " bps\n";
        std::cout << "  Completed: " << (completed ? "Yes" : "No") << "\n";
    }
};

/**
 * @class ExecutionSimulator
 * @brief Simulates execution of algorithms with market impact
 *
 * Provides a realistic simulation environment for testing execution
 * algorithms. Features include:
 * - Price simulation with volatility
 * - Market impact modeling (square-root law)
 * - Limit order fill simulation
 * - Performance measurement
 */
class ExecutionSimulator {
private:
    SimulationConfig config_;
    MarketImpactModel impact_model_;
    std::mt19937 rng_;

    // Simulation state
    double current_price_ = 0.0;
    double bid_price_ = 0.0;
    double ask_price_ = 0.0;
    uint64_t cumulative_volume_ = 0;
    TimePoint current_time_;

public:
    /**
     * @brief Constructor with configuration
     * @param config Simulation configuration
     */
    explicit ExecutionSimulator(const SimulationConfig& config = SimulationConfig())
        : config_(config), rng_(config.random_seed) {
        reset();
    }

    /**
     * @brief Constructor with impact model
     * @param model Market impact model to use
     * @param config Simulation configuration
     */
    ExecutionSimulator(const MarketImpactModel& model,
                       const SimulationConfig& config = SimulationConfig())
        : config_(config), impact_model_(model), rng_(config.random_seed) {
        reset();
    }

    /**
     * @brief Resets simulator to initial state
     */
    void reset() {
        current_price_ = config_.initial_price;
        update_quotes();
        cumulative_volume_ = 0;
        current_time_ = Clock::now();
        rng_.seed(config_.random_seed);
    }

    /**
     * @brief Sets the market impact model
     * @param model Impact model to use
     */
    void set_impact_model(const MarketImpactModel& model) {
        impact_model_ = model;
    }

    /**
     * @brief Sets the simulation configuration
     * @param config New configuration
     */
    void set_config(const SimulationConfig& config) {
        config_ = config;
        reset();
    }

    /**
     * @brief Runs a simulation for an execution algorithm
     * @param algo Execution algorithm to test
     * @param duration_ms Simulation duration in milliseconds
     * @return SimulationResult with performance metrics
     */
    SimulationResult run_simulation(ExecutionAlgorithm& algo,
                                    std::chrono::milliseconds duration_ms) {
        reset();
        algo.reset();

        SimulationResult result;
        auto start_price = current_price_;

        // Calculate time step
        auto tick_duration = std::chrono::milliseconds(1000 / config_.ticks_per_second);
        int num_ticks = static_cast<int>(duration_ms.count() * config_.ticks_per_second / 1000);

        // Run simulation
        for (int tick = 0; tick < num_ticks && !algo.is_complete(); ++tick) {
            // Advance time
            current_time_ += tick_duration;

            // Simulate price movement
            simulate_price_tick();

            // Generate market data
            MarketData data = get_current_market_data();
            result.price_path.push_back(data);

            // Get orders from algorithm
            auto orders = algo.on_market_data(data);

            // Simulate order execution
            for (auto& order : orders) {
                auto fills = simulate_order_execution(order, data);
                for (auto& fill : fills) {
                    algo.on_fill(fill);
                    result.fills.push_back(fill);

                    // Apply market impact
                    if (config_.apply_market_impact) {
                        apply_market_impact(fill.quantity);
                    }
                }
            }
        }

        // Generate report
        result.report = algo.generate_report();
        result.completed = algo.is_complete();

        // Calculate realized impact
        result.realized_impact_bps =
            ((current_price_ - start_price) / start_price) * 10000.0;

        // Calculate predicted impact
        result.predicted_impact_bps =
            impact_model_.estimate_total_impact(algo.get_executed_quantity(), config_.adv);

        return result;
    }

    /**
     * @brief Runs simulation with custom market data sequence
     * @param algo Execution algorithm to test
     * @param market_data Sequence of market data points
     * @return SimulationResult with performance metrics
     */
    SimulationResult run_simulation(ExecutionAlgorithm& algo,
                                    const std::vector<MarketData>& market_data) {
        algo.reset();

        SimulationResult result;
        result.price_path = market_data;

        if (market_data.empty()) {
            return result;
        }

        auto start_price = market_data.front().price;
        current_price_ = start_price;

        // Process each market data point
        for (const auto& data : market_data) {
            if (algo.is_complete()) break;

            current_price_ = data.price;
            current_time_ = data.timestamp;

            // Get orders from algorithm
            auto orders = algo.on_market_data(data);

            // Simulate order execution
            for (auto& order : orders) {
                auto fills = simulate_order_execution(order, data);
                for (auto& fill : fills) {
                    algo.on_fill(fill);
                    result.fills.push_back(fill);
                }
            }
        }

        // Generate report
        result.report = algo.generate_report();
        result.completed = algo.is_complete();

        // Calculate realized impact
        if (!market_data.empty()) {
            result.realized_impact_bps =
                ((market_data.back().price - start_price) / start_price) * 10000.0;
        }

        return result;
    }

    /**
     * @brief Compares multiple algorithms on the same market scenario
     * @param algorithms Vector of algorithms to compare
     * @param duration_ms Simulation duration
     * @return Vector of results, one per algorithm
     */
    std::vector<SimulationResult> compare_algorithms(
        std::vector<std::unique_ptr<ExecutionAlgorithm>>& algorithms,
        std::chrono::milliseconds duration_ms
    ) {
        std::vector<SimulationResult> results;

        // Generate shared price path
        reset();
        auto tick_duration = std::chrono::milliseconds(1000 / config_.ticks_per_second);
        int num_ticks = static_cast<int>(duration_ms.count() * config_.ticks_per_second / 1000);

        std::vector<MarketData> shared_path;
        for (int tick = 0; tick < num_ticks; ++tick) {
            current_time_ += tick_duration;
            simulate_price_tick();
            shared_path.push_back(get_current_market_data());
        }

        // Run each algorithm on the same path
        for (auto& algo : algorithms) {
            results.push_back(run_simulation(*algo, shared_path));
        }

        return results;
    }

    /**
     * @brief Prints comparison of algorithm results
     * @param results Vector of simulation results
     */
    static void print_comparison(const std::vector<SimulationResult>& results) {
        std::cout << "\n=== Execution Algorithm Comparison ===\n";
        std::cout << std::left
                  << std::setw(20) << "Algorithm"
                  << std::setw(15) << "Avg Price"
                  << std::setw(20) << "Impl. Shortfall"
                  << std::setw(12) << "Fill Rate"
                  << std::setw(12) << "Num Fills"
                  << "\n";
        std::cout << std::string(79, '-') << "\n";

        for (const auto& result : results) {
            std::cout << std::left
                      << std::setw(20) << result.report.algorithm_name
                      << std::setw(15) << std::fixed << std::setprecision(2)
                      << result.report.avg_execution_price
                      << std::setw(20) << std::setprecision(2)
                      << result.report.implementation_shortfall_bps << " bps"
                      << std::setw(12) << std::setprecision(1)
                      << (result.report.fill_rate * 100) << "%"
                      << std::setw(12) << result.report.num_fills
                      << "\n";
        }
    }

    /**
     * @brief Gets current market data snapshot
     * @return MarketData object
     */
    MarketData get_current_market_data() const {
        MarketData data;
        data.price = current_price_;
        data.bid_price = bid_price_;
        data.ask_price = ask_price_;
        data.spread = ask_price_ - bid_price_;
        data.total_volume = cumulative_volume_;
        data.timestamp = current_time_;
        return data;
    }

    /**
     * @brief Calculates naive execution cost (single market order)
     * @param quantity Order quantity
     * @return Estimated cost in basis points
     */
    double estimate_naive_cost(uint64_t quantity) const {
        // Spread cost + full impact
        double spread_cost = config_.spread_bps / 2.0;
        double impact_cost = impact_model_.estimate_total_impact(quantity, config_.adv);
        return spread_cost + impact_cost;
    }

private:
    /**
     * @brief Simulates one tick of price movement
     */
    void simulate_price_tick() {
        // Geometric Brownian motion
        std::normal_distribution<double> dist(0.0, 1.0);
        double dt = 1.0 / (config_.ticks_per_second * 252 * 6.5 * 3600);  // Fraction of year
        double drift = 0.0;  // No drift
        double diffusion = config_.volatility * std::sqrt(dt) * dist(rng_);

        current_price_ *= std::exp(drift * dt + diffusion);

        // Round to tick size
        current_price_ = std::round(current_price_ / config_.tick_size) * config_.tick_size;
        current_price_ = std::max(config_.tick_size, current_price_);

        update_quotes();
    }

    /**
     * @brief Updates bid/ask quotes based on current price
     */
    void update_quotes() {
        double half_spread = current_price_ * (config_.spread_bps / 20000.0);
        bid_price_ = current_price_ - half_spread;
        ask_price_ = current_price_ + half_spread;

        // Round to tick size
        bid_price_ = std::floor(bid_price_ / config_.tick_size) * config_.tick_size;
        ask_price_ = std::ceil(ask_price_ / config_.tick_size) * config_.tick_size;
    }

    /**
     * @brief Simulates execution of an order
     * @param order Order to execute
     * @param data Current market data
     * @return Vector of fills (may be empty or partial)
     */
    std::vector<Fill> simulate_order_execution(Order& order, const MarketData& data) {
        std::vector<Fill> fills;

        if (order.is_market_order()) {
            // Market orders always fill at current price
            double fill_price = (order.side == Side::BUY) ? data.ask_price : data.bid_price;
            Fill fill(order.id, order.id, fill_price, order.quantity);
            fill.timestamp = current_time_;
            fills.push_back(fill);
            cumulative_volume_ += order.quantity;
        } else {
            // Limit orders have probabilistic fill
            std::uniform_real_distribution<double> dist(0.0, 1.0);

            bool would_fill = false;
            double fill_price = order.price;

            if (order.side == Side::BUY) {
                // Buy limit fills if price <= limit
                would_fill = (data.ask_price <= order.price);
                fill_price = std::min(order.price, data.ask_price);
            } else {
                // Sell limit fills if price >= limit
                would_fill = (data.bid_price >= order.price);
                fill_price = std::max(order.price, data.bid_price);
            }

            if (would_fill && dist(rng_) < config_.fill_probability) {
                Fill fill(order.id, order.id, fill_price, order.quantity);
                fill.timestamp = current_time_;
                fills.push_back(fill);
                cumulative_volume_ += order.quantity;
            }
        }

        return fills;
    }

    /**
     * @brief Applies market impact to price
     * @param quantity Executed quantity
     */
    void apply_market_impact(uint64_t quantity) {
        double impact_bps = impact_model_.estimate_temporary_impact(quantity, config_.adv);
        double impact_factor = impact_bps / 10000.0;

        // Impact moves price against the trader (up for buys, down for sells)
        current_price_ *= (1.0 + impact_factor);
        update_quotes();
    }
};

/**
 * @brief Generates synthetic market data for testing
 * @param num_points Number of data points
 * @param config Simulation configuration
 * @return Vector of market data points
 */
inline std::vector<MarketData> generate_synthetic_market_data(
    size_t num_points,
    const SimulationConfig& config = SimulationConfig()
) {
    std::vector<MarketData> data;
    data.reserve(num_points);

    std::mt19937 rng(config.random_seed);
    std::normal_distribution<double> dist(0.0, 1.0);

    double price = config.initial_price;
    auto timestamp = Clock::now();
    auto tick_duration = std::chrono::milliseconds(1000 / config.ticks_per_second);

    for (size_t i = 0; i < num_points; ++i) {
        // Price movement
        double dt = 1.0 / (config.ticks_per_second * 252 * 6.5 * 3600);
        double diffusion = config.volatility * std::sqrt(dt) * dist(rng);
        price *= std::exp(diffusion);
        price = std::round(price / config.tick_size) * config.tick_size;
        price = std::max(config.tick_size, price);

        // Create market data
        double half_spread = price * (config.spread_bps / 20000.0);
        MarketData md;
        md.price = price;
        md.bid_price = std::floor((price - half_spread) / config.tick_size) * config.tick_size;
        md.ask_price = std::ceil((price + half_spread) / config.tick_size) * config.tick_size;
        md.spread = md.ask_price - md.bid_price;
        md.timestamp = timestamp;

        data.push_back(md);
        timestamp += tick_duration;
    }

    return data;
}

#endif // EXECUTION_SIMULATOR_HPP
