#ifndef ROLLING_STATISTICS_HPP
#define ROLLING_STATISTICS_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>

/**
 * @class RollingStatistics
 * @brief Fixed-size circular buffer for computing rolling statistics
 *
 * This template class maintains a fixed-size window of values and computes
 * various statistics (mean, variance, min, max, etc.) efficiently using
 * incremental updates where possible.
 *
 * @tparam T The numeric type to store (e.g., double, float)
 * @tparam N The maximum window size (compile-time constant for performance)
 *
 * Design considerations for HFT:
 * - Fixed-size array (no heap allocations after construction)
 * - O(1) insertion using circular buffer
 * - Incremental mean calculation to avoid full recomputation
 * - Cache-friendly contiguous memory layout
 */
template <typename T, size_t N>
class RollingStatistics {
private:
    std::array<T, N> buffer_;   ///< Circular buffer storage
    size_t head_ = 0;           ///< Next write position
    size_t count_ = 0;          ///< Current number of elements
    T sum_ = T{0};              ///< Running sum for O(1) mean
    T sum_sq_ = T{0};           ///< Running sum of squares for variance
    T min_val_ = std::numeric_limits<T>::max();
    T max_val_ = std::numeric_limits<T>::lowest();

public:
    /**
     * @brief Default constructor - initializes empty buffer
     */
    RollingStatistics() {
        buffer_.fill(T{0});
    }

    /**
     * @brief Adds a new value to the rolling window
     * @param value The value to add
     *
     * If the buffer is full, the oldest value is removed before adding.
     * Updates running sum and sum of squares for O(1) mean/variance.
     */
    void add(T value) {
        if (count_ == N) {
            // Buffer full - remove oldest value from running sums
            T old_value = buffer_[head_];
            sum_ -= old_value;
            sum_sq_ -= old_value * old_value;
        } else {
            count_++;
        }

        // Add new value
        buffer_[head_] = value;
        sum_ += value;
        sum_sq_ += value * value;

        // Update min/max (note: removal of old min/max requires recomputation)
        if (value < min_val_) min_val_ = value;
        if (value > max_val_) max_val_ = value;

        // Advance circular buffer pointer
        head_ = (head_ + 1) % N;
    }

    /**
     * @brief Returns the arithmetic mean of values in the window
     * @return Mean value, or 0 if empty
     */
    T mean() const {
        if (count_ == 0) return T{0};
        return sum_ / static_cast<T>(count_);
    }

    /**
     * @brief Returns the sample variance of values in the window
     * @return Variance using Bessel's correction (n-1 denominator)
     *
     * Uses the computational formula: Var = (sum_sq - sum^2/n) / (n-1)
     */
    T variance() const {
        if (count_ < 2) return T{0};
        T n = static_cast<T>(count_);
        T mean_val = sum_ / n;
        return (sum_sq_ - sum_ * mean_val) / (n - 1);
    }

    /**
     * @brief Returns the sample standard deviation
     * @return Square root of variance
     */
    T stddev() const {
        return std::sqrt(variance());
    }

    /**
     * @brief Returns the minimum value in the window
     * @return Minimum value (requires recomputation if old min was evicted)
     */
    T min() const {
        if (count_ == 0) return T{0};
        // For accurate min after eviction, recompute from buffer
        return *std::min_element(buffer_.begin(), buffer_.begin() + count_);
    }

    /**
     * @brief Returns the maximum value in the window
     * @return Maximum value (requires recomputation if old max was evicted)
     */
    T max() const {
        if (count_ == 0) return T{0};
        return *std::max_element(buffer_.begin(), buffer_.begin() + count_);
    }

    /**
     * @brief Returns the range (max - min) of values
     * @return Range of values in the window
     */
    T range() const {
        if (count_ == 0) return T{0};
        return max() - min();
    }

    /**
     * @brief Returns the number of values currently in the window
     * @return Current count (0 to N)
     */
    size_t count() const {
        return count_;
    }

    /**
     * @brief Returns the maximum capacity of the window
     * @return Template parameter N
     */
    constexpr size_t capacity() const {
        return N;
    }

    /**
     * @brief Checks if the window is full
     * @return true if count == N
     */
    bool is_full() const {
        return count_ == N;
    }

    /**
     * @brief Clears all values and resets statistics
     */
    void clear() {
        buffer_.fill(T{0});
        head_ = 0;
        count_ = 0;
        sum_ = T{0};
        sum_sq_ = T{0};
        min_val_ = std::numeric_limits<T>::max();
        max_val_ = std::numeric_limits<T>::lowest();
    }

    /**
     * @brief Returns the sum of all values in the window
     * @return Running sum
     */
    T sum() const {
        return sum_;
    }

    /**
     * @brief Returns the most recently added value
     * @return Last value added, or 0 if empty
     */
    T last() const {
        if (count_ == 0) return T{0};
        size_t last_idx = (head_ == 0) ? N - 1 : head_ - 1;
        return buffer_[last_idx];
    }

    /**
     * @brief Returns the oldest value in the window
     * @return First value that will be evicted, or 0 if empty
     */
    T oldest() const {
        if (count_ == 0) return T{0};
        if (count_ < N) {
            return buffer_[0];
        }
        return buffer_[head_];
    }

    /**
     * @brief Computes a specific percentile
     * @param p Percentile (0.0 to 1.0, e.g., 0.5 for median)
     * @return Value at the given percentile
     *
     * Note: This requires copying and sorting, so it's O(N log N).
     * Use sparingly in hot paths.
     */
    T percentile(double p) const {
        if (count_ == 0) return T{0};
        if (p <= 0.0) return min();
        if (p >= 1.0) return max();

        // Copy active elements and sort
        std::array<T, N> sorted;
        for (size_t i = 0; i < count_; ++i) {
            sorted[i] = buffer_[i];
        }
        std::sort(sorted.begin(), sorted.begin() + count_);

        // Linear interpolation for percentile
        double idx = p * (count_ - 1);
        size_t lower = static_cast<size_t>(idx);
        size_t upper = lower + 1;
        if (upper >= count_) upper = count_ - 1;

        double frac = idx - lower;
        return sorted[lower] * (1.0 - frac) + sorted[upper] * frac;
    }

    /**
     * @brief Returns the median (50th percentile)
     * @return Median value
     */
    T median() const {
        return percentile(0.5);
    }
};

#endif // ROLLING_STATISTICS_HPP
