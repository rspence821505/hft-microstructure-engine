#pragma once

/**
 * @file multi_feed_aggregator.hpp
 * @brief Aggregates market data from multiple TCP feeds
 *
 * This component provides a standalone feed aggregator interface that:
 * - Defines common tick and feed configuration structures
 * - Uses lock-free queues for efficient tick processing
 * - Supports multiple feed sources with statistics tracking
 * - Provides a callback-based architecture
 *
 * Note: This header avoids including net/feed.hpp directly to prevent
 * OrderBook name collision with Matching-Engine. Instead it defines
 * its own lightweight structures for feed handling.
 */

// Queue and protocol includes (local copies from TCP-Socket)
#include "spsc_queue.hpp"
#include "common.hpp"
#include "text_protocol.hpp"
#include "binary_protocol.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

/**
 * @enum FeedProtocol
 * @brief Protocol type for feed connections
 */
enum class FeedProtocol {
    TEXT,
    BINARY
};

/**
 * @struct FeedSource
 * @brief Configuration for a single feed source
 */
struct FeedSource {
    std::string name;           ///< Exchange/source identifier
    std::string host;           ///< Server hostname or IP
    uint16_t port;              ///< Server port
    FeedProtocol protocol;      ///< TEXT or BINARY protocol
    bool enabled = true;        ///< Whether this feed is active

    FeedSource() : port(0), protocol(FeedProtocol::TEXT) {}

    FeedSource(const std::string& n, const std::string& h, uint16_t p,
               FeedProtocol proto = FeedProtocol::TEXT)
        : name(n), host(h), port(p), protocol(proto) {}
};

/**
 * @struct FeedTick
 * @brief Unified tick structure for the aggregator
 */
struct FeedTick {
    uint64_t timestamp;         ///< Exchange timestamp
    char symbol[8];             ///< Trading symbol
    double price;               ///< Trade/quote price
    int64_t volume;             ///< Volume
    uint64_t recv_timestamp_ns; ///< Local receive timestamp

    FeedTick() : timestamp(0), price(0.0), volume(0), recv_timestamp_ns(0) {
        symbol[0] = '\0';
    }

    FeedTick(uint64_t ts, const char* sym, double p, int64_t vol, uint64_t recv_ts = 0)
        : timestamp(ts), price(p), volume(vol), recv_timestamp_ns(recv_ts) {
        std::strncpy(symbol, sym, sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }

    // Construct from text protocol tick
    explicit FeedTick(const TextTick& tt, uint64_t recv_ts = 0)
        : timestamp(tt.timestamp), price(tt.price), volume(tt.volume),
          recv_timestamp_ns(recv_ts) {
        std::memcpy(symbol, tt.symbol, sizeof(symbol));
    }

    // Construct from binary protocol tick
    explicit FeedTick(const TickPayload& tp, uint64_t recv_ts = 0)
        : timestamp(tp.timestamp), price(static_cast<double>(tp.price)),
          volume(tp.volume), recv_timestamp_ns(recv_ts) {
        std::memcpy(symbol, tp.symbol, 4);
        symbol[4] = '\0';
    }
};

/**
 * @struct FeedStatistics
 * @brief Statistics for a single feed
 */
struct FeedStatistics {
    std::string name;
    uint64_t messages_received = 0;
    uint64_t messages_processed = 0;
    uint64_t parse_errors = 0;
    double throughput = 0.0;
    double avg_latency_ns = 0.0;
    bool connected = false;
    std::chrono::steady_clock::time_point last_message_time;
};

/**
 * @struct AggregatedTick
 * @brief Tick with source information for cross-feed analysis
 */
struct AggregatedTick {
    FeedTick tick;              ///< The underlying tick data
    std::string source;         ///< Feed source name
    uint64_t aggregator_recv_ns; ///< Timestamp when aggregator received it
    size_t source_index;        ///< Index in feed list

    AggregatedTick() : aggregator_recv_ns(0), source_index(0) {}

    AggregatedTick(const FeedTick& t, const std::string& src, size_t idx)
        : tick(t), source(src), aggregator_recv_ns(now_ns()), source_index(idx) {}
};

/// Callback type for aggregated ticks
using AggregatedTickCallback = std::function<void(const AggregatedTick&)>;

/**
 * @class MultiFeedAggregator
 * @brief Aggregates and normalizes market data from multiple TCP feeds
 *
 * This class manages multiple feed sources, aggregating their tick data
 * into a unified stream with source attribution. It supports:
 *
 * - Dynamic feed addition/removal
 * - Per-feed and aggregate statistics
 * - Unified callback interface
 * - Cross-feed latency measurement
 *
 * Architecture:
 *   Feed1 (TCP) ──┐
 *   Feed2 (TCP) ──┼──> Aggregator Queue ──> Callback
 *   Feed3 (TCP) ──┘
 *
 * Note: Actual TCP connection functionality requires linking with
 * TCP-Socket library. This class provides the framework and interface.
 */
class MultiFeedAggregator {
public:
    static constexpr size_t DEFAULT_QUEUE_SIZE = 1024 * 1024;

private:
    std::vector<FeedSource> sources_;
    std::vector<FeedStatistics> stats_;

    SPSCQueue<AggregatedTick> aggregated_queue_;
    std::atomic<bool> should_stop_{false};
    std::atomic<bool> running_{false};

    std::thread processor_thread_;
    AggregatedTickCallback callback_;

    // Aggregate statistics
    std::atomic<uint64_t> total_messages_{0};
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point end_time_;

    bool verbose_ = false;

public:
    /**
     * @brief Constructs an empty aggregator
     * @param queue_size Size of internal aggregation queue
     */
    explicit MultiFeedAggregator(size_t queue_size = DEFAULT_QUEUE_SIZE)
        : aggregated_queue_(queue_size) {}

    ~MultiFeedAggregator() {
        stop();
    }

    /**
     * @brief Adds a feed source to the aggregator
     * @param name Source identifier
     * @param host Server hostname
     * @param port Server port
     * @param protocol TEXT or BINARY
     * @return Index of the added feed
     */
    size_t add_feed(const std::string& name, const std::string& host,
                    uint16_t port, FeedProtocol protocol = FeedProtocol::TEXT) {
        sources_.emplace_back(name, host, port, protocol);
        stats_.emplace_back();
        stats_.back().name = name;
        return sources_.size() - 1;
    }

    /**
     * @brief Adds a feed source from configuration
     * @param source Feed source configuration
     * @return Index of the added feed
     */
    size_t add_feed(const FeedSource& source) {
        sources_.push_back(source);
        stats_.emplace_back();
        stats_.back().name = source.name;
        return sources_.size() - 1;
    }

    /**
     * @brief Sets the callback for aggregated ticks
     * @param callback Function to call for each tick
     */
    void set_tick_callback(AggregatedTickCallback callback) {
        callback_ = std::move(callback);
    }

    /**
     * @brief Sets verbose mode for debugging
     * @param verbose Enable verbose logging
     */
    void set_verbose(bool verbose) {
        verbose_ = verbose;
    }

    /**
     * @brief Injects a tick directly (for testing or simulation)
     * @param tick The tick to inject
     * @param source_index Index of the source feed
     */
    void inject_tick(const FeedTick& tick, size_t source_index = 0) {
        if (source_index >= sources_.size()) return;

        AggregatedTick agg_tick(tick, sources_[source_index].name, source_index);
        enqueue_tick(agg_tick);
        stats_[source_index].messages_received++;
    }

    /**
     * @brief Starts the aggregator processor
     * @return true if started successfully
     *
     * Note: This starts the internal processor thread. For actual TCP
     * connections, use the TCP-Socket library's FeedHandler separately.
     */
    bool start_all() {
        if (running_) return true;
        if (sources_.empty()) {
            if (verbose_) {
                std::cerr << "[Aggregator] No feeds configured\n";
            }
            return false;
        }

        should_stop_ = false;
        running_ = true;
        start_time_ = std::chrono::steady_clock::now();

        // Start the aggregator processor thread
        processor_thread_ = std::thread([this]() { processor_loop(); });

        if (verbose_) {
            std::cout << "[Aggregator] Started with " << sources_.size() << " feed sources\n";
        }

        return true;
    }

    /**
     * @brief Stops the aggregator
     */
    void stop() {
        should_stop_ = true;

        // Wait for processor thread
        if (processor_thread_.joinable()) {
            processor_thread_.join();
        }

        end_time_ = std::chrono::steady_clock::now();
        running_ = false;
    }

    /**
     * @brief Waits for the aggregator to complete
     */
    void wait() {
        should_stop_ = true;
        if (processor_thread_.joinable()) {
            processor_thread_.join();
        }
        end_time_ = std::chrono::steady_clock::now();
        running_ = false;
    }

    /**
     * @brief Checks if the aggregator is running
     * @return true if running
     */
    bool is_running() const { return running_; }

    /**
     * @brief Gets the number of configured feeds
     * @return Feed count
     */
    size_t feed_count() const { return sources_.size(); }

    /**
     * @brief Gets statistics for a specific feed
     * @param index Feed index
     * @return Feed statistics
     */
    const FeedStatistics& get_feed_stats(size_t index) const {
        return stats_[index];
    }

    /**
     * @brief Gets all feed statistics
     * @return Vector of feed statistics
     */
    const std::vector<FeedStatistics>& get_all_stats() const {
        return stats_;
    }

    /**
     * @brief Gets total messages processed across all feeds
     * @return Message count
     */
    uint64_t total_messages() const {
        return total_messages_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Gets aggregate throughput across all feeds
     * @return Messages per second
     */
    double aggregate_throughput() const {
        auto duration = std::chrono::steady_clock::now() - start_time_;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        return ms > 0 ? total_messages_.load() * 1000.0 / ms : 0.0;
    }

    /**
     * @brief Prints aggregator statistics
     */
    void print_stats() const {
        std::cout << "\n========================================\n";
        std::cout << "    MULTI-FEED AGGREGATOR STATISTICS    \n";
        std::cout << "========================================\n";

        std::cout << "\n--- Per-Feed Statistics ---\n";
        for (size_t i = 0; i < stats_.size(); ++i) {
            const auto& s = stats_[i];
            std::cout << "Feed [" << s.name << "]:\n";
            std::cout << "  Connected: " << (s.connected ? "Yes" : "No") << "\n";
            std::cout << "  Messages received: " << s.messages_received << "\n";
            std::cout << "  Messages processed: " << s.messages_processed << "\n";
            std::cout << "  Parse errors: " << s.parse_errors << "\n";
            std::cout << "  Throughput: " << s.throughput << " msg/sec\n";
        }

        std::cout << "\n--- Aggregate Statistics ---\n";
        std::cout << "Total feeds: " << sources_.size() << "\n";
        std::cout << "Connected feeds: " << connected_count() << "\n";
        std::cout << "Total messages: " << total_messages_.load() << "\n";
        std::cout << "Aggregate throughput: " << aggregate_throughput() << " msg/sec\n";
    }

private:
    /**
     * @brief Enqueues a tick to the aggregation queue
     */
    void enqueue_tick(const AggregatedTick& tick) {
        // Non-blocking enqueue with retry
        int retries = 0;
        while (!aggregated_queue_.push(tick) && !should_stop_) {
            if (++retries > 100) {
                std::this_thread::yield();
                retries = 0;
            }
        }
    }

    /**
     * @brief Main processor loop for aggregated ticks
     */
    void processor_loop() {
        while (!should_stop_ || !aggregated_queue_.empty()) {
            auto tick_opt = aggregated_queue_.pop();
            if (tick_opt) {
                total_messages_.fetch_add(1, std::memory_order_relaxed);

                if (callback_) {
                    callback_(*tick_opt);
                }

                // Update per-feed stats
                if (tick_opt->source_index < stats_.size()) {
                    stats_[tick_opt->source_index].last_message_time =
                        std::chrono::steady_clock::now();
                    stats_[tick_opt->source_index].messages_processed++;
                }
            } else {
                std::this_thread::yield();
            }
        }
    }

    /**
     * @brief Counts connected feeds
     */
    size_t connected_count() const {
        size_t count = 0;
        for (const auto& s : stats_) {
            if (s.connected) count++;
        }
        return count;
    }
};
