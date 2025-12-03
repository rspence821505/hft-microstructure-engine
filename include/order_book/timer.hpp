#pragma once

#include <chrono>

class Timer {
private:
  using Clock = std::chrono::high_resolution_clock;
  using TimePoint = std::chrono::time_point<Clock>;

  TimePoint start_time_;
  TimePoint end_time_;
  bool is_running_;

public:
  Timer() : is_running_(false) {}

  void start() {
    start_time_ = Clock::now();
    is_running_ = true;
  }

  void stop() {
    end_time_ = Clock::now();
    is_running_ = false;
  }

  long long elapsed_microseconds() const {
    auto duration = end_time_ - start_time_;
    return std::chrono::duration_cast<std::chrono::microseconds>(duration)
        .count();
  }

  long long elapsed_nanoseconds() const {
    auto duration = end_time_ - start_time_;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration)
        .count();
  }

  double elapsed_milliseconds() const {
    return elapsed_microseconds() / 1000.0;
  }
};
