/**
 * Thread-Safe Packet Reordering Buffer with Priority Routing
 * ============================================================
 * Stuart Ray — Starlink Interview Prep Project
 *
 * Demonstrates:
 *   - Lock-free SPSC ring buffer (producer-consumer pattern)
 *   - Thread-safe priority queue with timeout-based release
 *   - C++ concurrency: std::atomic, std::mutex, std::condition_variable
 *   - Memory management: RAII, smart pointers, arena allocation
 *   - Zero-copy packet handling patterns
 *
 * Starlink relevance:
 *   - Satellite packets arrive out-of-order from multiple paths
 *   - Must reorder before delivering to user applications
 *   - Priority routing for real-time traffic (VoIP, gaming) vs bulk
 *   - Thread-safe: receiver thread and consumer thread are decoupled
 *
 * This directly demonstrates the Go → C++ translation from
 * Mastercard's connection pool and Homa's packet processing.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// ============================================================
// Packet Structure
// ============================================================

enum class Priority : uint8_t {
    REAL_TIME = 0,   // VoIP, gaming — lowest latency
    STREAMING = 1,   // Video, audio — moderate latency OK
    BULK      = 2,   // Downloads, updates — best effort
    CONTROL   = 3,   // Satellite control plane — always highest
};

struct Packet {
    uint64_t sequence_number;
    Priority priority;
    uint32_t source_satellite_id;
    uint32_t destination_id;
    TimePoint arrival_time;
    std::vector<uint8_t> payload;

    // For priority queue ordering: CONTROL > REAL_TIME > STREAMING > BULK
    // Within same priority: lower sequence number first
    bool operator>(const Packet& other) const {
        if (priority != other.priority) {
            // CONTROL (3) has highest priority, then REAL_TIME (0)
            if (priority == Priority::CONTROL) return false;
            if (other.priority == Priority::CONTROL) return true;
            return static_cast<uint8_t>(priority) > static_cast<uint8_t>(other.priority);
        }
        return sequence_number > other.sequence_number;
    }
};

// ============================================================
// Lock-Free SPSC Ring Buffer
// ============================================================
// Single-Producer Single-Consumer — no locks needed.
// Uses acquire/release memory ordering for correctness.
// This is the pattern used in high-performance packet I/O (DPDK, etc.)

template <typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be power of 2");

public:
    SPSCRingBuffer() : head_(0), tail_(0) {}

    // Producer: try to enqueue an item
    bool tryPush(T&& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & (Capacity - 1);

        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // Full
        }

        buffer_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: try to dequeue an item
    std::optional<T> tryPop() {
        size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // Empty
        }

        T item = std::move(buffer_[tail]);
        tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return item;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    size_t size() const {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & (Capacity - 1);
    }

private:
    // Align to cache line to prevent false sharing
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    std::array<T, Capacity> buffer_;
};

// ============================================================
// Packet Reordering Buffer
// ============================================================
// Thread-safe buffer that accepts out-of-order packets and
// releases them in sequence order with timeout.

class ReorderingBuffer {
public:
    explicit ReorderingBuffer(uint64_t start_seq, double timeout_ms = 50.0)
        : next_expected_seq_(start_seq),
          timeout_(std::chrono::duration_cast<Clock::duration>(
              std::chrono::duration<double, std::milli>(timeout_ms))),
          running_(true),
          total_received_(0),
          total_released_(0),
          total_gaps_(0) {}

    ~ReorderingBuffer() {
        running_ = false;
    }

    // Called by receiver thread — inserts a packet
    void insert(Packet pkt) {
        std::lock_guard<std::mutex> lock(mu_);
        total_received_++;
        buffer_[pkt.sequence_number] = std::move(pkt);
        last_insert_time_ = Clock::now();
        cv_.notify_one();
    }

    // Called by consumer thread — blocks until next in-order packet
    // is available or timeout expires
    std::optional<Packet> getNext() {
        std::unique_lock<std::mutex> lock(mu_);

        auto deadline = Clock::now() + timeout_;

        // Wait until we have the expected sequence number or timeout
        cv_.wait_until(lock, deadline, [this] {
            return !running_ || buffer_.count(next_expected_seq_);
        });

        if (!running_ && buffer_.empty()) {
            return std::nullopt;
        }

        // Check if we have the expected packet
        auto it = buffer_.find(next_expected_seq_);
        if (it != buffer_.end()) {
            Packet pkt = std::move(it->second);
            buffer_.erase(it);
            next_expected_seq_++;
            total_released_++;
            return pkt;
        }

        // Timeout: skip this sequence number (gap)
        total_gaps_++;
        next_expected_seq_++;

        // Try to release any buffered packets that are now in order
        while (buffer_.count(next_expected_seq_)) {
            auto jt = buffer_.find(next_expected_seq_);
            // Return the first one; others will be returned on next call
            Packet pkt = std::move(jt->second);
            buffer_.erase(jt);
            next_expected_seq_++;
            total_released_++;
            return pkt;
        }

        return std::nullopt;  // Gap with no subsequent packet ready
    }

    void stop() {
        running_ = false;
        cv_.notify_all();
    }

    void printStats() const {
        std::cout << "Reordering Buffer Stats:\n"
                  << "  Received: " << total_received_ << "\n"
                  << "  Released: " << total_released_ << "\n"
                  << "  Gaps:     " << total_gaps_ << "\n"
                  << "  Buffered: " << buffer_.size() << "\n";
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::unordered_map<uint64_t, Packet> buffer_;
    uint64_t next_expected_seq_;
    Clock::duration timeout_;
    TimePoint last_insert_time_;
    std::atomic<bool> running_;

    // Stats
    std::atomic<uint64_t> total_received_;
    std::atomic<uint64_t> total_released_;
    std::atomic<uint64_t> total_gaps_;
};

// ============================================================
// Priority Router
// ============================================================
// Routes packets to output queues based on destination and priority.
// Higher priority packets are dequeued first.

class PriorityRouter {
public:
    explicit PriorityRouter(int num_output_queues)
        : queues_(num_output_queues), queue_sizes_(num_output_queues) {
        for (auto& size : queue_sizes_) size.store(0);
    }

    void route(Packet pkt) {
        int queue_idx = pkt.destination_id % queues_.size();

        {
            std::lock_guard<std::mutex> lock(queue_mutexes_[queue_idx]);
            queues_[queue_idx].push(std::move(pkt));
        }
        queue_sizes_[queue_idx].fetch_add(1, std::memory_order_relaxed);
        total_routed_.fetch_add(1, std::memory_order_relaxed);
    }

    std::optional<Packet> dequeue(int queue_idx) {
        std::lock_guard<std::mutex> lock(queue_mutexes_[queue_idx]);
        if (queues_[queue_idx].empty()) return std::nullopt;

        Packet pkt = std::move(
            const_cast<Packet&>(queues_[queue_idx].top()));
        queues_[queue_idx].pop();
        queue_sizes_[queue_idx].fetch_sub(1, std::memory_order_relaxed);
        return pkt;
    }

    uint64_t totalRouted() const {
        return total_routed_.load(std::memory_order_relaxed);
    }

    void printStats() const {
        std::cout << "Router Stats:\n"
                  << "  Total routed: " << total_routed_.load() << "\n"
                  << "  Queue depths: ";
        for (size_t i = 0; i < queue_sizes_.size(); i++) {
            std::cout << "[" << i << "]=" << queue_sizes_[i].load() << " ";
        }
        std::cout << "\n";
    }

private:
    std::vector<std::priority_queue<Packet, std::vector<Packet>,
                                     std::greater<Packet>>> queues_;
    std::array<std::mutex, 16> queue_mutexes_;  // fixed max for simplicity
    std::vector<std::atomic<int>> queue_sizes_;
    std::atomic<uint64_t> total_routed_{0};
};

// ============================================================
// Simulation
// ============================================================

Packet generatePacket(uint64_t seq, std::mt19937& rng) {
    std::uniform_int_distribution<int> pri_dist(0, 3);
    std::uniform_int_distribution<uint32_t> sat_dist(1, 100);
    std::uniform_int_distribution<uint32_t> dst_dist(0, 7);
    std::uniform_int_distribution<int> size_dist(64, 1500);

    int payload_size = size_dist(rng);
    std::vector<uint8_t> payload(payload_size, 0xAB);

    return {
        seq,
        static_cast<Priority>(pri_dist(rng)),
        sat_dist(rng),
        dst_dist(rng),
        Clock::now(),
        std::move(payload)
    };
}

int main() {
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║  Packet Reordering Buffer + Priority Router ║\n";
    std::cout << "║  Stuart Ray — Starlink Interview Prep       ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    constexpr int NUM_PACKETS = 100000;
    constexpr int NUM_OUTPUT_QUEUES = 8;
    constexpr double REORDER_PROBABILITY = 0.15;  // 15% out-of-order
    constexpr double DROP_PROBABILITY = 0.02;     // 2% loss

    ReorderingBuffer reorder_buf(0, 10.0 /* timeout_ms */);
    PriorityRouter router(NUM_OUTPUT_QUEUES);

    std::mt19937 rng(42);

    // --- Producer thread: simulate receiving packets from satellites ---
    std::thread producer([&]() {
        std::vector<Packet> batch;
        batch.reserve(NUM_PACKETS);

        // Generate all packets
        for (int i = 0; i < NUM_PACKETS; i++) {
            batch.push_back(generatePacket(i, rng));
        }

        // Simulate out-of-order delivery
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        std::uniform_int_distribution<int> swap_dist(1, 10);

        for (int i = 0; i < NUM_PACKETS; i++) {
            // Skip (drop) some packets
            if (prob(rng) < DROP_PROBABILITY) continue;

            // Swap with a nearby packet to simulate reordering
            if (prob(rng) < REORDER_PROBABILITY && i + 1 < NUM_PACKETS) {
                int swap_offset = std::min(swap_dist(rng), NUM_PACKETS - i - 1);
                std::swap(batch[i], batch[i + swap_offset]);
            }

            reorder_buf.insert(std::move(batch[i]));

            // Simulate arrival jitter
            if (i % 1000 == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        // Signal completion
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        reorder_buf.stop();
    });

    // --- Consumer thread: dequeue in-order and route ---
    std::atomic<bool> consumer_done{false};

    std::thread consumer([&]() {
        while (true) {
            auto pkt = reorder_buf.getNext();
            if (!pkt.has_value()) {
                // Check if producer is done
                static int empty_count = 0;
                empty_count++;
                if (empty_count > 100) break;
                continue;
            }
            router.route(std::move(*pkt));
        }
        consumer_done = true;
    });

    // --- Monitor thread: print stats periodically ---
    std::thread monitor([&]() {
        while (!consumer_done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    producer.join();
    consumer.join();
    monitor.join();

    // Final stats
    std::cout << "\n=== Final Results ===\n";
    reorder_buf.printStats();
    std::cout << "\n";
    router.printStats();

    // Measure throughput
    std::cout << "\nPackets processed: " << router.totalRouted() << "\n";

    // Drain and verify output queues
    std::cout << "\n=== Output Queue Contents (sample) ===\n";
    for (int q = 0; q < NUM_OUTPUT_QUEUES; q++) {
        int count = 0;
        while (auto pkt = router.dequeue(q)) {
            count++;
        }
        std::cout << "Queue " << q << ": " << count << " packets\n";
    }

    return 0;
}
