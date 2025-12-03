#ifndef MICROSTRUCTURE_BACKTESTER_HPP
#define MICROSTRUCTURE_BACKTESTER_HPP

#include "market_events.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @struct BacktesterConfig
 * @brief Configuration for the microstructure backtester
 */
struct BacktesterConfig {
    std::string input_filename = "";     ///< Path to input CSV file
    std::string filter_symbol = "";      ///< Optional symbol filter
    bool output_impact = false;          ///< Output impact estimates
    bool output_timeline = false;        ///< Output event timeline stats
    uint64_t assumed_adv = 10000000;     ///< Default ADV for impact calculation
    double impact_coefficient = 0.01;    ///< Market impact coefficient
};

/**
 * @struct FieldRange
 * @brief Represents a substring view into a CSV field without copying
 */
struct FieldRange {
    const char* start;  ///< Pointer to the first character
    size_t length;      ///< Number of characters in the field

    const char* end() const { return start + length; }
};

/**
 * @struct ParsedRow
 * @brief Represents a single parsed row from CSV input
 */
struct ParsedRow {
    std::string timestamp;
    std::string symbol;
    double price;
    long volume;
    bool is_valid;

    static ParsedRow invalid() { return {"", "", 0.0, 0, false}; }
};

/**
 * @class MicrostructureBacktester
 * @brief Extended CSV analyzer with nanosecond precision and event timeline
 *
 * This class builds on the CSV parsing infrastructure to provide:
 * 1. Nanosecond-precision timestamp parsing
 * 2. In-memory event timeline construction
 * 3. Basic market impact estimation
 *
 * The backtester is designed for replaying historical market data
 * with accurate timing for microstructure analysis.
 */
class MicrostructureBacktester {
private:
    BacktesterConfig config_;
    std::vector<MarketEvent> event_timeline_;
    SimpleImpactModel impact_model_;
    std::unordered_map<std::string, uint64_t> symbol_adv_;  ///< Per-symbol ADV

public:
    /**
     * @brief Constructs backtester with given configuration
     * @param config Configuration parameters
     */
    explicit MicrostructureBacktester(const BacktesterConfig& config)
        : config_(config), impact_model_(config.impact_coefficient) {}

    /**
     * @brief Parses timestamp string to nanoseconds since epoch
     * @param timestamp_str Timestamp format: "YYYY-MM-DD HH:MM:SS.nnnnnnnnn"
     * @return Nanoseconds since Unix epoch
     *
     * Supports both with and without nanosecond suffix:
     * - "2024-01-15 09:30:00" -> parsed to second precision, ns = 0
     * - "2024-01-15 09:30:00.123456789" -> full nanosecond precision
     *
     * The function handles variable nanosecond precision (3-9 digits).
     */
    uint64_t parse_timestamp_to_nanoseconds(const std::string& timestamp_str) const {
        struct tm tm = {};
        uint64_t nanoseconds = 0;

        // Parse date/time portion (first 19 characters: "YYYY-MM-DD HH:MM:SS")
        if (timestamp_str.length() >= 19) {
            // Manual parsing for portability (strptime not available on all platforms)
            // Format: "YYYY-MM-DD HH:MM:SS"
            //          0123456789...
            tm.tm_year = std::stoi(timestamp_str.substr(0, 4)) - 1900;
            tm.tm_mon = std::stoi(timestamp_str.substr(5, 2)) - 1;
            tm.tm_mday = std::stoi(timestamp_str.substr(8, 2));
            tm.tm_hour = std::stoi(timestamp_str.substr(11, 2));
            tm.tm_min = std::stoi(timestamp_str.substr(14, 2));
            tm.tm_sec = std::stoi(timestamp_str.substr(17, 2));
            tm.tm_isdst = -1;  // Let system determine DST
        }

        time_t seconds = mktime(&tm);
        if (seconds == -1) {
            return 0;  // Invalid timestamp
        }

        // Parse nanosecond portion if present (after the decimal point)
        // Format: ".123456789" starting at position 19
        if (timestamp_str.length() > 20 && timestamp_str[19] == '.') {
            std::string ns_str = timestamp_str.substr(20);

            // Pad or truncate to 9 digits for nanoseconds
            if (ns_str.length() < 9) {
                ns_str.append(9 - ns_str.length(), '0');
            } else if (ns_str.length() > 9) {
                ns_str = ns_str.substr(0, 9);
            }

            nanoseconds = std::stoull(ns_str);
        }

        return static_cast<uint64_t>(seconds) * 1'000'000'000ULL + nanoseconds;
    }

    /**
     * @brief Splits a CSV line into four fields without string allocation
     * @param line The CSV line to split
     * @return Array of 4 FieldRange objects
     */
    std::array<FieldRange, 4> split_csv_line(const std::string& line) const {
        const char* data = line.data();
        std::array<FieldRange, 4> fields = {};
        int field_index = 0;
        size_t start = 0;

        for (size_t i = 0; i <= line.length() && field_index < 4; ++i) {
            if (i == line.length() || line[i] == ',') {
                fields[field_index] = {data + start, i - start};
                field_index++;
                start = i + 1;
            }
        }

        return fields;
    }

    /**
     * @brief Parses a CSV line into structured data
     * @param line The CSV line to parse
     * @return ParsedRow with validity flag
     */
    ParsedRow parse_line(const std::string& line) const {
        auto fields = split_csv_line(line);

        // Validate field count
        if (fields[0].length == 0 || fields[1].length == 0) {
            return ParsedRow::invalid();
        }

        std::string timestamp(fields[0].start, fields[0].length);
        std::string symbol(fields[1].start, fields[1].length);

        // Parse price using strtod
        char* end_ptr;
        double price = strtod(fields[2].start, &end_ptr);
        if (end_ptr == fields[2].start) {
            return ParsedRow::invalid();
        }

        // Parse volume using from_chars
        long volume = 0;
        auto vol_result = std::from_chars(fields[3].start, fields[3].end(), volume);
        if (vol_result.ec != std::errc{}) {
            return ParsedRow::invalid();
        }

        return {timestamp, symbol, price, volume, true};
    }

    /**
     * @brief Builds an event timeline from CSV file
     * @param csv_file Path to the CSV file
     *
     * Reads the entire CSV file and constructs an in-memory timeline
     * of market events, sorted by nanosecond timestamp.
     *
     * This enables accurate replay of historical market data for
     * microstructure analysis and backtesting.
     */
    void build_event_timeline(const std::string& csv_file) {
        event_timeline_.clear();

        std::ifstream file(csv_file);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + csv_file);
        }

        std::string line;
        bool first_line = true;

        while (std::getline(file, line)) {
            // Skip header if present
            if (first_line) {
                first_line = false;
                // Check if this looks like a header (contains "timestamp" or doesn't parse)
                if (line.find("timestamp") != std::string::npos ||
                    line.find("symbol") != std::string::npos) {
                    continue;
                }
            }

            if (line.empty()) continue;

            auto row = parse_line(line);
            if (!row.is_valid) continue;

            // Apply symbol filter if configured
            if (!config_.filter_symbol.empty() &&
                row.symbol != config_.filter_symbol) {
                continue;
            }

            MarketEvent event;
            event.timestamp_ns = parse_timestamp_to_nanoseconds(row.timestamp);
            event.symbol = row.symbol;
            event.price = row.price;
            event.volume = static_cast<uint64_t>(row.volume);
            event.type = MarketEventType::TRADE;  // Default to TRADE for simple CSV

            event_timeline_.push_back(event);
        }

        // Sort chronologically by nanosecond timestamp
        std::sort(event_timeline_.begin(), event_timeline_.end());

        std::cerr << "Built event timeline with " << event_timeline_.size()
                  << " events\n";
    }

    /**
     * @brief Sets the Average Daily Volume for a symbol
     * @param symbol Trading symbol
     * @param adv Average daily volume
     */
    void set_symbol_adv(const std::string& symbol, uint64_t adv) {
        symbol_adv_[symbol] = adv;
    }

    /**
     * @brief Gets ADV for a symbol (uses default if not set)
     * @param symbol Trading symbol
     * @return ADV for the symbol
     */
    uint64_t get_symbol_adv(const std::string& symbol) const {
        auto it = symbol_adv_.find(symbol);
        if (it != symbol_adv_.end()) {
            return it->second;
        }
        return config_.assumed_adv;
    }

    /**
     * @brief Estimates market impact for a trade
     * @param volume Trade volume
     * @param symbol Trading symbol
     * @return Estimated impact in basis points
     */
    double estimate_impact(uint64_t volume, const std::string& symbol) const {
        uint64_t adv = get_symbol_adv(symbol);
        return impact_model_.estimate_impact(volume, adv);
    }

    /**
     * @brief Processes the event timeline and outputs analysis
     *
     * Iterates through the sorted event timeline and performs
     * microstructure analysis including impact estimation.
     */
    void process_timeline() {
        if (event_timeline_.empty()) {
            std::cerr << "No events in timeline. Call build_event_timeline first.\n";
            return;
        }

        // Print header
        std::cout << "timestamp_ns,symbol,price,volume";
        if (config_.output_impact) {
            std::cout << ",impact_bps";
        }
        std::cout << "\n";

        // Process each event
        for (const auto& event : event_timeline_) {
            std::cout << event.timestamp_ns << ","
                      << event.symbol << ","
                      << event.price << ","
                      << event.volume;

            if (config_.output_impact) {
                double impact = estimate_impact(event.volume, event.symbol);
                std::cout << "," << impact;
            }

            std::cout << "\n";
        }
    }

    /**
     * @brief Prints timeline statistics
     */
    void print_timeline_stats() const {
        if (event_timeline_.empty()) {
            std::cout << "Timeline is empty.\n";
            return;
        }

        // Calculate statistics
        uint64_t min_ts = event_timeline_.front().timestamp_ns;
        uint64_t max_ts = event_timeline_.back().timestamp_ns;
        uint64_t duration_ns = max_ts - min_ts;

        // Count events per symbol
        std::unordered_map<std::string, size_t> symbol_counts;
        std::unordered_map<std::string, uint64_t> symbol_volumes;
        std::unordered_map<std::string, double> symbol_min_price;
        std::unordered_map<std::string, double> symbol_max_price;

        for (const auto& event : event_timeline_) {
            symbol_counts[event.symbol]++;
            symbol_volumes[event.symbol] += event.volume;

            if (symbol_min_price.find(event.symbol) == symbol_min_price.end()) {
                symbol_min_price[event.symbol] = event.price;
                symbol_max_price[event.symbol] = event.price;
            } else {
                symbol_min_price[event.symbol] =
                    std::min(symbol_min_price[event.symbol], event.price);
                symbol_max_price[event.symbol] =
                    std::max(symbol_max_price[event.symbol], event.price);
            }
        }

        std::cout << "\n=== Event Timeline Statistics ===\n";
        std::cout << "Total events: " << event_timeline_.size() << "\n";
        std::cout << "Time span: " << (duration_ns / 1'000'000'000.0) << " seconds\n";
        std::cout << "Start timestamp: " << min_ts << " ns\n";
        std::cout << "End timestamp: " << max_ts << " ns\n";

        std::cout << "\n--- Per-Symbol Statistics ---\n";
        for (const auto& [symbol, count] : symbol_counts) {
            std::cout << symbol << ":\n";
            std::cout << "  Events: " << count << "\n";
            std::cout << "  Total Volume: " << symbol_volumes[symbol] << "\n";
            std::cout << "  Price Range: " << symbol_min_price[symbol]
                      << " - " << symbol_max_price[symbol] << "\n";

            // Calculate and show impact for total volume
            double total_impact = estimate_impact(symbol_volumes[symbol], symbol);
            std::cout << "  Est. Full Volume Impact: " << total_impact << " bps\n";
        }
    }

    /**
     * @brief Gets the event timeline (read-only)
     * @return Const reference to event vector
     */
    const std::vector<MarketEvent>& get_timeline() const {
        return event_timeline_;
    }

    /**
     * @brief Gets number of events in timeline
     * @return Event count
     */
    size_t timeline_size() const {
        return event_timeline_.size();
    }

    /**
     * @brief Gets the impact model (read-only)
     * @return Const reference to impact model
     */
    const SimpleImpactModel& get_impact_model() const {
        return impact_model_;
    }

    /**
     * @brief Gets mutable access to impact model
     * @return Reference to impact model
     */
    SimpleImpactModel& get_impact_model() {
        return impact_model_;
    }
};

/**
 * @brief Parses command-line arguments for the backtester
 * @param argc Argument count
 * @param argv Argument vector
 * @return BacktesterConfig with parsed values
 */
inline BacktesterConfig parse_backtester_args(int argc, char* argv[]) {
    BacktesterConfig config;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg.substr(0, 2) == "--") {
            auto eq_pos = arg.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = arg.substr(2, eq_pos - 2);
                std::string value = arg.substr(eq_pos + 1);

                if (key == "symbol") {
                    config.filter_symbol = value;
                } else if (key == "adv") {
                    config.assumed_adv = std::stoull(value);
                } else if (key == "impact-coeff") {
                    config.impact_coefficient = std::stod(value);
                } else {
                    throw std::invalid_argument("Unknown option: " + key);
                }
            } else if (arg == "--impact") {
                config.output_impact = true;
            } else if (arg == "--stats") {
                config.output_timeline = true;
            } else {
                throw std::invalid_argument("Invalid flag format: " + arg);
            }
        } else {
            config.input_filename = arg;
        }
    }

    return config;
}

#endif // MICROSTRUCTURE_BACKTESTER_HPP
