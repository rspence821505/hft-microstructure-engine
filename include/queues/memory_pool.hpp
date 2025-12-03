#pragma once

/**
 * @file memory_pool.hpp
 * @brief Performance Optimization - Cache-optimized memory pool allocator
 *
 * Provides high-performance memory allocation for the microstructure engine:
 * - Thread-local bump allocator for zero contention
 * - Object pools for frequently allocated types (Orders, Fills, Events)
 * - Cache-line aligned allocations to prevent false sharing
 * - O(1) allocation time
 *
 * Based on the ThreadLocalPool pattern from TCP-Socket project.
 */

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

/**
 * @class ArenaAllocator
 * @brief Fast bump allocator with cache-line alignment
 *
 * Features:
 * - O(1) allocation (pointer bump)
 * - No individual deallocation (arena-style)
 * - Cache-line aligned for optimal cache performance
 * - Suitable for temporary allocations in hot paths
 */
class ArenaAllocator {
public:
  static constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024; // 64KB chunks
  static constexpr size_t CACHE_LINE_SIZE = 64;
  static constexpr size_t MAX_ALIGNMENT = 64;

private:
  const size_t chunk_size_;
  std::vector<char *> chunks_;
  std::vector<void *> large_allocations_;
  char *current_chunk_ = nullptr;
  size_t current_offset_ = 0;
  size_t total_allocated_ = 0;
  size_t allocation_count_ = 0;

public:
  /**
   * @brief Constructs an arena with the specified chunk size
   * @param chunk_size Size of each memory chunk (default 64KB)
   */
  explicit ArenaAllocator(size_t chunk_size = DEFAULT_CHUNK_SIZE)
      : chunk_size_(chunk_size) {
    if (chunk_size < 1024) {
      throw std::invalid_argument("Chunk size must be at least 1KB");
    }
    allocate_new_chunk();
  }

  ~ArenaAllocator() {
    for (auto *chunk : chunks_) {
      std::free(chunk);
    }
    for (auto *ptr : large_allocations_) {
      std::free(ptr);
    }
  }

  // Non-copyable, non-movable
  ArenaAllocator(const ArenaAllocator &) = delete;
  ArenaAllocator &operator=(const ArenaAllocator &) = delete;
  ArenaAllocator(ArenaAllocator &&) = delete;
  ArenaAllocator &operator=(ArenaAllocator &&) = delete;

  /**
   * @brief Allocates memory from the arena
   * @param size Number of bytes to allocate
   * @param alignment Required alignment (default: max_align_t)
   * @return Pointer to allocated memory
   */
  void *allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
    if (size == 0)
      return nullptr;
    if (alignment > MAX_ALIGNMENT) {
      throw std::invalid_argument("Alignment too large");
    }

    // Align current offset
    const size_t aligned_offset = align_up(current_offset_, alignment);
    const size_t new_offset = aligned_offset + size;

    // Check if allocation fits in current chunk
    if (new_offset > chunk_size_) {
      if (size > chunk_size_) {
        return allocate_large(size, alignment);
      }
      allocate_new_chunk();
      return allocate(size, alignment);
    }

    void *ptr = current_chunk_ + aligned_offset;
    current_offset_ = new_offset;

    total_allocated_ += size;
    allocation_count_++;

    return ptr;
  }

  /**
   * @brief Allocates and constructs an object
   * @tparam T Object type
   * @tparam Args Constructor argument types
   * @param args Constructor arguments
   * @return Pointer to constructed object
   */
  template <typename T, typename... Args> T *construct(Args &&...args) {
    void *ptr = allocate(sizeof(T), alignof(T));
    return new (ptr) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Resets the arena, freeing all memory
   */
  void reset() {
    // Free all chunks except the first
    for (size_t i = 1; i < chunks_.size(); ++i) {
      std::free(chunks_[i]);
    }

    // Free large allocations
    for (auto *ptr : large_allocations_) {
      std::free(ptr);
    }
    large_allocations_.clear();

    if (!chunks_.empty()) {
      current_chunk_ = chunks_[0];
      current_offset_ = 0;
      chunks_.resize(1);
    }

    total_allocated_ = 0;
    allocation_count_ = 0;
  }

  // Statistics
  size_t total_allocated() const { return total_allocated_; }
  size_t allocation_count() const { return allocation_count_; }
  size_t chunk_count() const { return chunks_.size(); }
  size_t memory_usage() const {
    return chunks_.size() * chunk_size_ +
           large_allocations_.size() * chunk_size_;
  }

  double utilization() const {
    if (chunks_.empty())
      return 0.0;
    return 100.0 * total_allocated_ / memory_usage();
  }

private:
  void allocate_new_chunk() {
    void *chunk = std::aligned_alloc(CACHE_LINE_SIZE, chunk_size_);
    if (!chunk) {
      throw std::bad_alloc();
    }
    chunks_.push_back(static_cast<char *>(chunk));
    current_chunk_ = static_cast<char *>(chunk);
    current_offset_ = 0;
  }

  void *allocate_large(size_t size, size_t alignment) {
    void *ptr = std::aligned_alloc(alignment, size);
    if (!ptr) {
      throw std::bad_alloc();
    }
    large_allocations_.push_back(ptr);
    total_allocated_ += size;
    allocation_count_++;
    return ptr;
  }

  static constexpr size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
  }
};

/**
 * @class ObjectPool
 * @brief Fixed-size object pool for frequently allocated types
 *
 * Uses a free list for O(1) allocation and deallocation.
 * All objects are cache-line aligned.
 *
 * @tparam T Object type
 * @tparam N Initial pool capacity
 */
template <typename T, size_t N = 1024> class ObjectPool {
public:
  static constexpr size_t CACHE_LINE_SIZE = 64;

private:
  // Pad object size to cache line boundary
  static constexpr size_t PADDED_SIZE =
      ((sizeof(T) + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE) * CACHE_LINE_SIZE;

  struct alignas(CACHE_LINE_SIZE) Slot {
    union {
      char storage[PADDED_SIZE];
      Slot *next;
    };
    bool in_use = false;
  };

  std::unique_ptr<Slot[]> slots_;
  Slot *free_list_ = nullptr;
  size_t capacity_;
  std::atomic<size_t> allocated_{0};
  std::atomic<size_t> peak_allocated_{0};

public:
  /**
   * @brief Constructs a pool with the specified capacity
   * @param capacity Initial number of slots
   */
  explicit ObjectPool(size_t capacity = N)
      : slots_(std::make_unique<Slot[]>(capacity)), capacity_(capacity) {
    // Build free list
    for (size_t i = 0; i < capacity_; ++i) {
      slots_[i].next = free_list_;
      free_list_ = &slots_[i];
    }
  }

  // Non-copyable
  ObjectPool(const ObjectPool &) = delete;
  ObjectPool &operator=(const ObjectPool &) = delete;

  /**
   * @brief Allocates an object from the pool
   * @tparam Args Constructor argument types
   * @param args Constructor arguments
   * @return Pointer to constructed object, or nullptr if pool exhausted
   */
  template <typename... Args> T *allocate(Args &&...args) {
    if (!free_list_) {
      return nullptr; // Pool exhausted
    }

    Slot *slot = free_list_;
    free_list_ = slot->next;
    slot->in_use = true;

    size_t new_count = ++allocated_;
    size_t peak = peak_allocated_.load(std::memory_order_relaxed);
    while (new_count > peak) {
      if (peak_allocated_.compare_exchange_weak(peak, new_count,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
        break;
      }
    }

    return new (slot->storage) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Returns an object to the pool
   * @param ptr Pointer to object to deallocate
   */
  void deallocate(T *ptr) {
    if (!ptr)
      return;

    ptr->~T();

    // Find the slot containing this object
    Slot *slot = reinterpret_cast<Slot *>(ptr);
    slot->in_use = false;
    slot->next = free_list_;
    free_list_ = slot;

    --allocated_;
  }

  // Statistics
  size_t capacity() const { return capacity_; }
  size_t allocated() const {
    return allocated_.load(std::memory_order_relaxed);
  }
  size_t available() const { return capacity_ - allocated(); }
  size_t peak_allocated() const {
    return peak_allocated_.load(std::memory_order_relaxed);
  }

  double utilization() const { return 100.0 * allocated() / capacity_; }
};

/**
 * @class PooledAllocator
 * @brief STL-compatible allocator backed by an ObjectPool
 *
 * @tparam T Object type
 */
template <typename T> class PooledAllocator {
public:
  using value_type = T;

  PooledAllocator() noexcept = default;

  template <typename U> PooledAllocator(const PooledAllocator<U> &) noexcept {}

  T *allocate(size_t n) {
    if (n != 1) {
      // For arrays, fall back to standard allocator
      return static_cast<T *>(::operator new(n * sizeof(T)));
    }
    return get_pool().allocate();
  }

  void deallocate(T *ptr, size_t n) noexcept {
    if (n != 1) {
      ::operator delete(ptr);
      return;
    }
    get_pool().deallocate(ptr);
  }

  template <typename U>
  bool operator==(const PooledAllocator<U> &) const noexcept {
    return true;
  }

  template <typename U>
  bool operator!=(const PooledAllocator<U> &) const noexcept {
    return false;
  }

private:
  static ObjectPool<T> &get_pool() {
    static thread_local ObjectPool<T> pool;
    return pool;
  }
};

/**
 * @class RingBufferPool
 * @brief Lock-free ring buffer for pre-allocated objects
 *
 * Used for producer-consumer patterns with minimal allocation overhead.
 * Pre-allocates a fixed number of slots and provides O(1) get/release
 * operations.
 *
 * @tparam T Object type
 * @tparam N Buffer capacity (must be power of 2)
 */
template <typename T, size_t N = 1024> class RingBufferPool {
  static_assert((N & (N - 1)) == 0, "N must be a power of 2");

public:
  static constexpr size_t CACHE_LINE_SIZE = 64;

private:
  struct alignas(CACHE_LINE_SIZE) Slot {
    T data;
    std::atomic<bool> ready{false};
  };

  alignas(CACHE_LINE_SIZE) std::array<Slot, N> buffer_;
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0}; // Producer
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0}; // Consumer

  static constexpr size_t MASK = N - 1;

public:
  /**
   * @brief Gets a slot for writing (producer side)
   * @return Pointer to slot, or nullptr if buffer full
   */
  T *get_write_slot() {
    size_t head = head_.load(std::memory_order_relaxed);
    size_t next = (head + 1) & MASK;

    if (next == tail_.load(std::memory_order_acquire)) {
      return nullptr; // Buffer full
    }

    return &buffer_[head].data;
  }

  /**
   * @brief Commits a write slot (producer side)
   */
  void commit_write() {
    size_t head = head_.load(std::memory_order_relaxed);
    buffer_[head].ready.store(true, std::memory_order_release);
    head_.store((head + 1) & MASK, std::memory_order_release);
  }

  /**
   * @brief Gets a slot for reading (consumer side)
   * @return Pointer to slot, or nullptr if buffer empty
   */
  T *get_read_slot() {
    size_t tail = tail_.load(std::memory_order_relaxed);

    if (tail == head_.load(std::memory_order_acquire)) {
      return nullptr; // Buffer empty
    }

    if (!buffer_[tail].ready.load(std::memory_order_acquire)) {
      return nullptr; // Slot not ready
    }

    return &buffer_[tail].data;
  }

  /**
   * @brief Releases a read slot (consumer side)
   */
  void release_read() {
    size_t tail = tail_.load(std::memory_order_relaxed);
    buffer_[tail].ready.store(false, std::memory_order_release);
    tail_.store((tail + 1) & MASK, std::memory_order_release);
  }

  // Statistics
  size_t capacity() const { return N; }

  size_t size() const {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_acquire);
    return (head - tail) & MASK;
  }

  bool empty() const { return size() == 0; }
  bool full() const { return size() == N - 1; }
};

/**
 * @brief Thread-local arena instance for general allocations
 */
inline thread_local ArenaAllocator g_thread_arena;

/**
 * @brief Convenience function to allocate from thread-local arena
 * @tparam T Type to allocate
 * @tparam Args Constructor argument types
 * @param args Constructor arguments
 * @return Pointer to constructed object
 */
template <typename T, typename... Args> T *arena_new(Args &&...args) {
  return g_thread_arena.construct<T>(std::forward<Args>(args)...);
}

/**
 * @brief Resets the thread-local arena
 */
inline void arena_reset() { g_thread_arena.reset(); }

/**
 * @brief Gets thread-local arena statistics
 */
inline void print_arena_stats() {
  std::cout << "Thread-local Arena Statistics:\n";
  std::cout << "  Total allocated: " << g_thread_arena.total_allocated()
            << " bytes\n";
  std::cout << "  Allocation count: " << g_thread_arena.allocation_count()
            << "\n";
  std::cout << "  Chunk count: " << g_thread_arena.chunk_count() << "\n";
  std::cout << "  Memory usage: " << g_thread_arena.memory_usage()
            << " bytes\n";
  std::cout << "  Utilization: " << g_thread_arena.utilization() << "%\n";
}
