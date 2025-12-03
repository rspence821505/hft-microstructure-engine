#include "../include/microstructure_backtester.hpp"
#include <iostream>

/**
 * @brief Main entry point for the microstructure backtester
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 *
 * Command-line usage:
 *   backtester [options] filename.csv
 *
 * Options:
 *   --symbol=SYM       Filter to specific symbol
 *   --impact           Include impact estimates in output
 *   --stats            Print timeline statistics
 *   --adv=N            Assumed ADV for impact calculation (default: 10000000)
 *   --impact-coeff=X   Impact coefficient (default: 0.01)
 *
 * Examples:
 *   ./backtester --impact --stats market_data.csv
 *   ./backtester --symbol=AAPL --impact data.csv
 *   ./backtester --adv=5000000 --impact-coeff=0.02 trades.csv
 *
 * The backtester reads CSV data with format:
 *   timestamp,symbol,price,volume
 *
 * Timestamps support nanosecond precision:
 *   2024-01-15 09:30:00.123456789
 *
 * Output includes nanosecond timestamps and optional impact estimates.
 */
int main(int argc, char* argv[]) {
    try {
        // Parse command-line arguments
        BacktesterConfig config = parse_backtester_args(argc, argv);

        // Validate input file was provided
        if (config.input_filename.empty()) {
            std::cerr << "Microstructure Backtester - Event Timeline Builder\n\n";
            std::cerr << "Usage: backtester [options] filename.csv\n\n";
            std::cerr << "Options:\n";
            std::cerr << "  --symbol=SYM       Filter to specific symbol\n";
            std::cerr << "  --impact           Include impact estimates in output\n";
            std::cerr << "  --stats            Print timeline statistics\n";
            std::cerr << "  --adv=N            Assumed ADV (default: 10000000)\n";
            std::cerr << "  --impact-coeff=X   Impact coefficient (default: 0.01)\n\n";
            std::cerr << "Example:\n";
            std::cerr << "  ./backtester --impact --stats market_data.csv\n";
            return 1;
        }

        // Create backtester with configuration
        MicrostructureBacktester backtester(config);

        // Build the event timeline from CSV
        backtester.build_event_timeline(config.input_filename);

        // Process and output the timeline
        backtester.process_timeline();

        // Print statistics if requested
        if (config.output_timeline) {
            backtester.print_timeline_stats();
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
