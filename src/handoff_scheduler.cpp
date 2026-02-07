/**
 * Satellite Handoff Scheduler
 * ============================
 * Stuart Ray — Starlink Interview Prep Project
 *
 * Demonstrates:
 *   - Weighted interval scheduling (dynamic programming)
 *   - Overlap constraint satisfaction for seamless handoff
 *   - Signal quality optimization under time constraints
 *   - C++17: std::variant, structured bindings, algorithms
 *
 * Problem:
 *   A user terminal moves through multiple satellite coverage zones.
 *   Each satellite has a visibility window with varying signal quality.
 *   Schedule handoffs to:
 *     1. Maximize minimum signal quality (worst-case guarantee)
 *     2. Ensure ≥ OVERLAP_SECONDS of overlap for seamless transition
 *     3. Minimize total number of handoffs
 *
 * This is the exact problem Starlink ground software solves every
 * few seconds for every active user terminal.
 */

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <vector>

// ============================================================
// Types
// ============================================================

struct VisibilityWindow {
    int satellite_id;
    double start_time;       // seconds from epoch
    double end_time;         // seconds from epoch
    double peak_signal_quality;  // dB SNR at best point
    double start_signal_quality; // dB SNR at window start (rising)
    double end_signal_quality;   // dB SNR at window end (falling)

    double duration() const { return end_time - start_time; }

    // Signal quality at a given time (parabolic model)
    double signalAt(double t) const {
        if (t < start_time || t > end_time) return 0.0;
        double mid = (start_time + end_time) / 2.0;
        double half_dur = (end_time - start_time) / 2.0;
        double normalized = (t - mid) / half_dur;  // -1 to 1
        // Parabolic: peak at center, falls to start/end values
        return peak_signal_quality * (1.0 - 0.3 * normalized * normalized);
    }
};

struct HandoffDecision {
    int from_satellite;
    int to_satellite;
    double handoff_time;        // when to switch
    double overlap_duration;    // seconds of simultaneous coverage
    double signal_at_handoff;   // signal quality during transition
};

struct ScheduleResult {
    std::vector<HandoffDecision> handoffs;
    double min_signal_quality;    // worst signal during any transition
    double total_coverage_time;   // total time with service
    double total_gap_time;        // total time without service
    int num_handoffs;
};

// ============================================================
// Handoff Scheduler
// ============================================================

class HandoffScheduler {
public:
    static constexpr double MIN_OVERLAP_SEC = 2.0;   // Starlink target
    static constexpr double MIN_SIGNAL_DB = 5.0;      // Minimum usable signal
    static constexpr double HANDOFF_MARGIN_SEC = 1.0;  // Safety margin

    /**
     * Schedule handoffs for a set of visibility windows.
     *
     * Algorithm:
     *   1. Sort windows by start time
     *   2. For each window, find compatible next windows (sufficient overlap)
     *   3. Use DP to find schedule maximizing minimum signal quality
     *   4. Backtrack to extract the optimal schedule
     *
     * Time complexity: O(N² log N) where N = number of visibility windows
     */
    static ScheduleResult schedule(std::vector<VisibilityWindow> windows) {
        if (windows.empty()) return {{}, 0, 0, 0, 0};

        // Sort by start time
        std::sort(windows.begin(), windows.end(),
                  [](const auto& a, const auto& b) {
                      return a.start_time < b.start_time;
                  });

        int n = static_cast<int>(windows.size());

        // dp[i] = best minimum signal quality achievable ending at window i
        std::vector<double> dp(n, 0.0);
        std::vector<int> parent(n, -1);

        // Base case: each window alone has its peak signal quality
        for (int i = 0; i < n; i++) {
            dp[i] = windows[i].peak_signal_quality;
        }

        // DP transition: try extending from each previous window
        for (int i = 1; i < n; i++) {
            for (int j = 0; j < i; j++) {
                // Check if window j can hand off to window i
                double overlap = windows[j].end_time - windows[i].start_time;

                if (overlap < MIN_OVERLAP_SEC) continue;  // Not enough overlap
                if (windows[i].start_time >= windows[j].end_time) continue;  // No overlap

                // Compute signal quality at the handoff point
                // Optimal handoff time: maximize min(signal_j, signal_i)
                double best_handoff_time = findOptimalHandoffTime(
                    windows[j], windows[i]);

                double signal_at_handoff = std::min(
                    windows[j].signalAt(best_handoff_time),
                    windows[i].signalAt(best_handoff_time));

                if (signal_at_handoff < MIN_SIGNAL_DB) continue;  // Too weak

                // min_signal through this path
                double path_signal = std::min(dp[j], signal_at_handoff);

                if (path_signal > dp[i]) {
                    dp[i] = path_signal;
                    parent[i] = j;
                }
            }
        }

        // Find the best ending window
        int best_end = 0;
        for (int i = 1; i < n; i++) {
            if (dp[i] > dp[best_end]) best_end = i;
        }

        // Backtrack to find the schedule
        std::vector<int> selected;
        int cur = best_end;
        while (cur != -1) {
            selected.push_back(cur);
            cur = parent[cur];
        }
        std::reverse(selected.begin(), selected.end());

        // Build handoff decisions
        ScheduleResult result;
        result.min_signal_quality = dp[best_end];
        result.num_handoffs = static_cast<int>(selected.size()) - 1;
        result.total_coverage_time = 0;
        result.total_gap_time = 0;

        for (int k = 0; k + 1 < static_cast<int>(selected.size()); k++) {
            int j = selected[k];
            int i = selected[k + 1];

            double handoff_time = findOptimalHandoffTime(windows[j], windows[i]);
            double overlap = windows[j].end_time - windows[i].start_time;

            result.handoffs.push_back({
                windows[j].satellite_id,
                windows[i].satellite_id,
                handoff_time,
                overlap,
                std::min(windows[j].signalAt(handoff_time),
                         windows[i].signalAt(handoff_time))
            });

            // Coverage time for window j (up to handoff)
            if (k == 0) {
                result.total_coverage_time += handoff_time - windows[j].start_time;
            } else {
                double prev_handoff = result.handoffs[k - 1].handoff_time;
                result.total_coverage_time += handoff_time - prev_handoff;
            }
        }

        // Add coverage for last window
        if (!selected.empty()) {
            int last = selected.back();
            if (result.handoffs.empty()) {
                result.total_coverage_time = windows[last].duration();
            } else {
                result.total_coverage_time +=
                    windows[last].end_time - result.handoffs.back().handoff_time;
            }
        }

        // Gap time = total timeline - coverage time
        if (!selected.empty()) {
            double total_time = windows[selected.back()].end_time -
                               windows[selected.front()].start_time;
            result.total_gap_time = total_time - result.total_coverage_time;
        }

        return result;
    }

private:
    /**
     * Find the optimal handoff time between two overlapping windows.
     * The optimal point is where the weaker signal is maximized —
     * i.e., where signal_j(t) == signal_i(t) in the overlap region.
     *
     * Uses binary search on the overlap interval.
     */
    static double findOptimalHandoffTime(const VisibilityWindow& from,
                                          const VisibilityWindow& to) {
        double overlap_start = std::max(from.start_time, to.start_time);
        double overlap_end = std::min(from.end_time, to.end_time);

        if (overlap_start >= overlap_end) {
            return (from.end_time + to.start_time) / 2.0;
        }

        // Binary search for the crossover point
        // from.signalAt(t) is decreasing, to.signalAt(t) is increasing
        double lo = overlap_start, hi = overlap_end;

        for (int iter = 0; iter < 50; iter++) {  // ~15 digits of precision
            double mid = (lo + hi) / 2.0;
            if (from.signalAt(mid) > to.signalAt(mid)) {
                lo = mid;
            } else {
                hi = mid;
            }
        }

        return (lo + hi) / 2.0;
    }
};

// ============================================================
// Simulation: Generate realistic visibility windows
// ============================================================

std::vector<VisibilityWindow> generateWindows(int num_satellites,
                                                double total_time_sec,
                                                unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> duration_dist(180.0, 600.0);  // 3-10 min
    std::uniform_real_distribution<double> gap_dist(10.0, 120.0);        // 10s-2min gap
    std::uniform_real_distribution<double> signal_dist(8.0, 25.0);       // dB SNR
    std::uniform_real_distribution<double> jitter_dist(-30.0, 30.0);     // overlap jitter

    std::vector<VisibilityWindow> windows;
    double current_time = 0;
    int sat_id = 0;

    while (current_time < total_time_sec && sat_id < num_satellites) {
        double duration = duration_dist(rng);
        double peak_snr = signal_dist(rng);

        // Create window with overlap into next
        windows.push_back({
            sat_id++,
            current_time,
            current_time + duration,
            peak_snr,
            peak_snr * 0.6,   // 60% of peak at edges
            peak_snr * 0.5    // 50% of peak at end
        });

        // Advance time (with some overlap for next satellite)
        double gap = gap_dist(rng) + jitter_dist(rng);
        current_time += duration - std::max(30.0, gap);  // Ensure some overlap
    }

    return windows;
}

// ============================================================
// Main
// ============================================================

int main() {
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║  Satellite Handoff Scheduler                ║\n";
    std::cout << "║  Stuart Ray — Starlink Interview Prep       ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    // Simulate 1 hour of satellite passes for a user terminal
    constexpr double SIMULATION_TIME = 3600.0;  // 1 hour
    constexpr int MAX_SATELLITES = 30;

    auto windows = generateWindows(MAX_SATELLITES, SIMULATION_TIME);

    std::cout << "Generated " << windows.size() << " visibility windows "
              << "over " << SIMULATION_TIME / 60.0 << " minutes\n\n";

    // Print windows
    std::cout << "=== Visibility Windows ===\n";
    for (const auto& w : windows) {
        std::cout << "Sat " << w.satellite_id
                  << ": [" << w.start_time << "s - " << w.end_time << "s]"
                  << " duration=" << w.duration() << "s"
                  << " peak_SNR=" << w.peak_signal_quality << "dB\n";
    }

    // Run scheduler
    std::cout << "\n=== Running Handoff Scheduler ===\n";
    auto result = HandoffScheduler::schedule(windows);

    // Print results
    std::cout << "\nOptimal Schedule:\n";
    std::cout << "  Handoffs: " << result.num_handoffs << "\n";
    std::cout << "  Min signal quality: " << result.min_signal_quality << " dB\n";
    std::cout << "  Coverage time: " << result.total_coverage_time << "s ("
              << (result.total_coverage_time / SIMULATION_TIME * 100) << "%)\n";
    std::cout << "  Gap time: " << result.total_gap_time << "s\n\n";

    std::cout << "=== Handoff Details ===\n";
    for (const auto& h : result.handoffs) {
        std::cout << "  Sat " << h.from_satellite << " → Sat " << h.to_satellite
                  << " at t=" << h.handoff_time << "s"
                  << " overlap=" << h.overlap_duration << "s"
                  << " signal=" << h.signal_at_handoff << "dB\n";
    }

    // Verify constraints
    std::cout << "\n=== Constraint Verification ===\n";
    bool all_ok = true;
    for (const auto& h : result.handoffs) {
        if (h.overlap_duration < HandoffScheduler::MIN_OVERLAP_SEC) {
            std::cout << "  FAIL: Overlap " << h.overlap_duration
                      << "s < " << HandoffScheduler::MIN_OVERLAP_SEC << "s\n";
            all_ok = false;
        }
        if (h.signal_at_handoff < HandoffScheduler::MIN_SIGNAL_DB) {
            std::cout << "  FAIL: Signal " << h.signal_at_handoff
                      << "dB < " << HandoffScheduler::MIN_SIGNAL_DB << "dB\n";
            all_ok = false;
        }
    }
    if (all_ok) {
        std::cout << "  All constraints satisfied ✓\n";
    }

    return 0;
}
