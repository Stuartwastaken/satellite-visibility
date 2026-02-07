/**
 * Starlink C++ Visualizer Data Generator
 * ======================================
 * Stuart Ray — Interview Prep Project
 *
 * Generates a single data.js file used by the HTML visualizer.
 * This keeps the GUI dependency-free: open index.html in a browser
 * and you get interactive visuals for all three C++ projects.
 */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef VISUALIZER_DATA_DIR
#define VISUALIZER_DATA_DIR "."
#endif

// ============================================================
// Constants
// ============================================================
constexpr double EARTH_RADIUS_KM = 6371.0;
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / M_PI;

// ============================================================
// Utility
// ============================================================
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

// ============================================================
// Visibility Graph Data
// ============================================================
struct GeoCoord {
    double lat_deg;
    double lon_deg;
};

struct Satellite {
    int id;
    GeoCoord position;
    double altitude_km;
    int orbital_plane;
    double capacity_mbps;
};

struct GroundStation {
    int id;
    GeoCoord position;
    std::string name;
    double min_elevation_deg;
    double capacity_mbps;
};

struct VisibilityEdge {
    int satellite_id;
    int station_id;
    double elevation_deg;
    double slant_km;
    double latency_ms;
};

struct VisibilityStats {
    int edge_count = 0;
    double min_elev = 90.0;
    double max_elev = 0.0;
    double avg_elev = 0.0;
    double min_latency = 1e9;
    double max_latency = 0.0;
    double avg_latency = 0.0;
    std::vector<int> coverage_counts;
};

double haversineDistanceKm(const GeoCoord& a, const GeoCoord& b) {
    double dlat = (b.lat_deg - a.lat_deg) * DEG_TO_RAD;
    double dlon = (b.lon_deg - a.lon_deg) * DEG_TO_RAD;
    double lat1 = a.lat_deg * DEG_TO_RAD;
    double lat2 = b.lat_deg * DEG_TO_RAD;
    double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1) * std::cos(lat2) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    double c = 2 * std::asin(std::sqrt(h));
    return EARTH_RADIUS_KM * c;
}

double computeElevationAngle(const GeoCoord& station, const GeoCoord& sat_pos,
                              double sat_altitude_km) {
    double ground_dist_km = haversineDistanceKm(station, sat_pos);
    double central_angle = ground_dist_km / EARTH_RADIUS_KM;
    double r_sat = EARTH_RADIUS_KM + sat_altitude_km;
    double slant_range = std::sqrt(
        EARTH_RADIUS_KM * EARTH_RADIUS_KM +
        r_sat * r_sat -
        2 * EARTH_RADIUS_KM * r_sat * std::cos(central_angle));
    if (slant_range < 1e-6) return 90.0;
    double cos_elevation = (slant_range * slant_range +
                            EARTH_RADIUS_KM * EARTH_RADIUS_KM -
                            r_sat * r_sat) /
                           (2 * slant_range * EARTH_RADIUS_KM);
    double angle_at_station = std::acos(std::clamp(cos_elevation, -1.0, 1.0));
    return (angle_at_station * RAD_TO_DEG) - 90.0;
}

double computeSlantRangeKm(const GeoCoord& station, const GeoCoord& sat_pos,
                           double sat_altitude_km) {
    double ground_dist_km = haversineDistanceKm(station, sat_pos);
    double central_angle = ground_dist_km / EARTH_RADIUS_KM;
    double r_sat = EARTH_RADIUS_KM + sat_altitude_km;
    return std::sqrt(
        EARTH_RADIUS_KM * EARTH_RADIUS_KM +
        r_sat * r_sat -
        2 * EARTH_RADIUS_KM * r_sat * std::cos(central_angle));
}

double computeLatencyMs(double slant_km) {
    return slant_km / 299.792;
}

std::vector<Satellite> generateStarlinkConstellation(int num_planes,
                                                     int sats_per_plane,
                                                     double altitude_km,
                                                     double inclination_deg) {
    std::vector<Satellite> sats;
    int id = 0;

    for (int p = 0; p < num_planes; p++) {
        double raan = (360.0 / num_planes) * p;
        for (int s = 0; s < sats_per_plane; s++) {
            double true_anomaly = (360.0 / sats_per_plane) * s;
            double angle = (raan + true_anomaly) * DEG_TO_RAD;
            double lat = inclination_deg * std::sin(angle);
            double lon = std::fmod(raan + true_anomaly *
                                          std::cos(inclination_deg * DEG_TO_RAD),
                                   360.0) -
                         180.0;
            sats.push_back({
                id++,
                {lat, lon},
                altitude_km,
                p,
                250.0
            });
        }
    }
    return sats;
}

std::vector<GroundStation> generateGroundStations(int count) {
    struct NamedCoord {
        const char* name;
        double lat;
        double lon;
    };
    std::vector<NamedCoord> city_coords = {
        {"Redmond, WA", 47.67, -122.12},
        {"San Francisco, CA", 37.77, -122.42},
        {"New York, NY", 40.71, -74.01},
        {"London, UK", 51.51, -0.13},
        {"Tokyo, JP", 35.68, 139.69},
        {"Sydney, AU", -33.87, 151.21},
        {"Paris, FR", 48.86, 2.35},
        {"Moscow, RU", 55.76, 37.62},
        {"Sao Paulo, BR", -23.55, -46.63},
        {"New Delhi, IN", 28.61, 77.23},
        {"Singapore", 1.35, 103.82},
        {"Nairobi, KE", -1.29, 36.82},
        {"Ottawa, CA", 45.42, -75.70},
        {"Lisbon, PT", 38.72, -9.14},
        {"Buenos Aires, AR", -34.60, -58.38},
        {"Dubai, AE", 25.20, 55.27},
        {"Tehran, IR", 35.69, 51.39},
        {"Stockholm, SE", 59.33, 18.07},
        {"Reykjavik, IS", 64.14, -21.94},
        {"Wellington, NZ", -41.29, 174.78},
    };

    std::vector<GroundStation> stations;
    for (int i = 0; i < std::min(count, static_cast<int>(city_coords.size())); i++) {
        stations.push_back({
            i,
            {city_coords[i].lat, city_coords[i].lon},
            city_coords[i].name,
            25.0,
            10000.0
        });
    }
    return stations;
}

std::vector<VisibilityEdge> buildVisibilityEdges(
    const std::vector<Satellite>& sats,
    const std::vector<GroundStation>& stations,
    double min_elevation_deg,
    VisibilityStats& stats_out) {
    std::vector<VisibilityEdge> edges;
    stats_out.coverage_counts.assign(stations.size(), 0);

    for (const auto& sat : sats) {
        for (const auto& gs : stations) {
            double elev = computeElevationAngle(gs.position, sat.position, sat.altitude_km);
            if (elev < min_elevation_deg) continue;

            double slant = computeSlantRangeKm(gs.position, sat.position, sat.altitude_km);
            double latency = computeLatencyMs(slant);
            edges.push_back({sat.id, gs.id, elev, slant, latency});
            stats_out.coverage_counts[gs.id]++;

            stats_out.min_elev = std::min(stats_out.min_elev, elev);
            stats_out.max_elev = std::max(stats_out.max_elev, elev);
            stats_out.avg_elev += elev;
            stats_out.min_latency = std::min(stats_out.min_latency, latency);
            stats_out.max_latency = std::max(stats_out.max_latency, latency);
            stats_out.avg_latency += latency;
        }
    }

    stats_out.edge_count = static_cast<int>(edges.size());
    if (stats_out.edge_count > 0) {
        stats_out.avg_elev /= stats_out.edge_count;
        stats_out.avg_latency /= stats_out.edge_count;
    } else {
        stats_out.min_elev = stats_out.max_elev = 0.0;
        stats_out.min_latency = stats_out.max_latency = 0.0;
    }
    return edges;
}

// ============================================================
// Packet Router Data
// ============================================================
struct PacketPoint {
    int seq;
    int arrival;
    int priority;
    int destination;
};

struct PacketStats {
    int num_packets = 0;
    int num_arrived = 0;
    int num_dropped = 0;
    int num_queues = 8;
    double reorder_prob = 0.0;
    double drop_prob = 0.0;
    std::vector<int> queue_counts;
    std::vector<int> priority_counts;
    std::vector<int> gaps;
    std::vector<PacketPoint> points;
};

PacketStats simulatePacketStream(int num_packets,
                                 int num_queues,
                                 double reorder_prob,
                                 double drop_prob,
                                 unsigned seed) {
    PacketStats stats;
    stats.num_packets = num_packets;
    stats.num_queues = num_queues;
    stats.reorder_prob = reorder_prob;
    stats.drop_prob = drop_prob;
    stats.queue_counts.assign(num_queues, 0);
    stats.priority_counts.assign(4, 0);

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int> pri_dist(0, 3);
    std::uniform_int_distribution<int> dst_dist(0, num_queues - 1);
    std::uniform_int_distribution<int> swap_dist(1, 8);

    // Generate sequence numbers
    std::vector<int> seqs(num_packets);
    std::iota(seqs.begin(), seqs.end(), 0);

    // Apply local reordering
    for (int i = 0; i < num_packets; i++) {
        if (prob(rng) < reorder_prob && i + 1 < num_packets) {
            int offset = std::min(swap_dist(rng), num_packets - i - 1);
            std::swap(seqs[i], seqs[i + offset]);
        }
    }

    int arrival_index = 0;
    for (int i = 0; i < num_packets; i++) {
        int seq = seqs[i];
        if (prob(rng) < drop_prob) {
            stats.num_dropped++;
            stats.gaps.push_back(seq);
            continue;
        }

        int priority = pri_dist(rng);
        int destination = dst_dist(rng);
        stats.priority_counts[priority]++;
        stats.queue_counts[destination]++;

        stats.points.push_back({seq, arrival_index++, priority, destination});
    }

    stats.num_arrived = arrival_index;
    return stats;
}

// ============================================================
// Handoff Scheduler Data
// ============================================================
struct VisibilityWindow {
    int satellite_id;
    double start_time;
    double end_time;
    double peak_signal_quality;
    double start_signal_quality;
    double end_signal_quality;

    double duration() const { return end_time - start_time; }

    double signalAt(double t) const {
        if (t < start_time || t > end_time) return 0.0;
        double mid = (start_time + end_time) / 2.0;
        double half = (end_time - start_time) / 2.0;
        if (half < 1e-6) return peak_signal_quality;
        double normalized = (t - mid) / half;
        return peak_signal_quality * (1.0 - 0.3 * normalized * normalized);
    }
};

struct HandoffDecision {
    int from_satellite;
    int to_satellite;
    double handoff_time;
    double overlap_duration;
    double signal_at_handoff;
};

struct HandoffResult {
    std::vector<HandoffDecision> handoffs;
    double min_signal_quality = 0.0;
    double total_coverage_time = 0.0;
    double total_gap_time = 0.0;
    int num_handoffs = 0;
};

class HandoffScheduler {
public:
    static constexpr double MIN_OVERLAP_SEC = 2.0;
    static constexpr double MIN_SIGNAL_DB = 5.0;

    static HandoffResult schedule(std::vector<VisibilityWindow> windows) {
        HandoffResult result;
        if (windows.empty()) return result;

        std::sort(windows.begin(), windows.end(),
                  [](const auto& a, const auto& b) { return a.start_time < b.start_time; });

        int n = static_cast<int>(windows.size());
        std::vector<double> dp(n, 0.0);
        std::vector<int> parent(n, -1);

        for (int i = 0; i < n; i++) dp[i] = windows[i].peak_signal_quality;

        for (int i = 1; i < n; i++) {
            for (int j = 0; j < i; j++) {
                double overlap = windows[j].end_time - windows[i].start_time;
                if (overlap < MIN_OVERLAP_SEC) continue;
                if (windows[i].start_time >= windows[j].end_time) continue;

                double t = findOptimalHandoffTime(windows[j], windows[i]);
                double signal = std::min(windows[j].signalAt(t), windows[i].signalAt(t));
                if (signal < MIN_SIGNAL_DB) continue;

                double path_signal = std::min(dp[j], signal);
                if (path_signal > dp[i]) {
                    dp[i] = path_signal;
                    parent[i] = j;
                }
            }
        }

        int best_end = 0;
        for (int i = 1; i < n; i++) {
            if (dp[i] > dp[best_end]) best_end = i;
        }

        std::vector<int> selected;
        int cur = best_end;
        while (cur != -1) {
            selected.push_back(cur);
            cur = parent[cur];
        }
        std::reverse(selected.begin(), selected.end());

        result.min_signal_quality = dp[best_end];
        result.num_handoffs = static_cast<int>(selected.size()) - 1;

        for (int k = 0; k + 1 < static_cast<int>(selected.size()); k++) {
            int a = selected[k];
            int b = selected[k + 1];
            double t = findOptimalHandoffTime(windows[a], windows[b]);
            double overlap = windows[a].end_time - windows[b].start_time;
            double signal = std::min(windows[a].signalAt(t), windows[b].signalAt(t));
            result.handoffs.push_back({
                windows[a].satellite_id,
                windows[b].satellite_id,
                t,
                overlap,
                signal
            });
        }

        if (!selected.empty()) {
            int last = selected.back();
            if (result.handoffs.empty()) {
                result.total_coverage_time = windows[last].duration();
            } else {
                result.total_coverage_time +=
                    windows[last].end_time - result.handoffs.back().handoff_time;
            }

            double total_time =
                windows[selected.back()].end_time - windows[selected.front()].start_time;
            result.total_gap_time = total_time - result.total_coverage_time;
        }

        return result;
    }

private:
    static double findOptimalHandoffTime(const VisibilityWindow& from,
                                         const VisibilityWindow& to) {
        double overlap_start = std::max(from.start_time, to.start_time);
        double overlap_end = std::min(from.end_time, to.end_time);
        if (overlap_start >= overlap_end) {
            return (from.end_time + to.start_time) / 2.0;
        }

        double lo = overlap_start, hi = overlap_end;
        for (int iter = 0; iter < 50; iter++) {
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

std::vector<VisibilityWindow> generateWindows(int num_satellites,
                                              double total_time_sec,
                                              unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> duration_dist(180.0, 600.0);
    std::uniform_real_distribution<double> gap_dist(10.0, 120.0);
    std::uniform_real_distribution<double> signal_dist(8.0, 25.0);
    std::uniform_real_distribution<double> jitter_dist(-30.0, 30.0);

    std::vector<VisibilityWindow> windows;
    double current_time = 0.0;
    int sat_id = 0;

    while (current_time < total_time_sec && sat_id < num_satellites) {
        double duration = duration_dist(rng);
        double peak_snr = signal_dist(rng);

        windows.push_back({
            sat_id++,
            current_time,
            current_time + duration,
            peak_snr,
            peak_snr * 0.6,
            peak_snr * 0.5
        });

        double gap = gap_dist(rng) + jitter_dist(rng);
        current_time += duration - std::max(30.0, gap);
    }
    return windows;
}

// ============================================================
// Arguments
// ============================================================
struct Args {
    int num_planes = 36;
    int sats_per_plane = 20;
    int num_stations = 20;
    double altitude_km = 550.0;
    double inclination_deg = 53.0;
    double min_elevation_deg = 25.0;
    int num_packets = 400;
    double reorder_prob = 0.18;
    double drop_prob = 0.03;
    int num_queues = 8;
    int num_handoff_sats = 18;
    double handoff_time_sec = 3600.0;
    unsigned seed = 42;
};

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --planes N           Number of orbital planes (default 36)\n"
              << "  --sats N             Satellites per plane (default 20)\n"
              << "  --stations N         Ground stations (default 20)\n"
              << "  --altitude KM        Altitude in km (default 550)\n"
              << "  --inclination DEG    Inclination in degrees (default 53)\n"
              << "  --min-elev DEG       Min elevation (default 25)\n"
              << "  --packets N          Packet count (default 400)\n"
              << "  --reorder P          Reorder probability (default 0.18)\n"
              << "  --drop P             Drop probability (default 0.03)\n"
              << "  --queues N           Router output queues (default 8)\n"
              << "  --handoff-sats N     Handoff windows (default 18)\n"
              << "  --handoff-time SEC   Handoff timeline seconds (default 3600)\n"
              << "  --seed N             RNG seed (default 42)\n"
              << "  --help               Show this help\n";
}

bool parseArgs(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto needValue = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                return "";
            }
            return argv[++i];
        };

        if (arg == "--planes") {
            args.num_planes = std::stoi(needValue("--planes"));
        } else if (arg == "--sats") {
            args.sats_per_plane = std::stoi(needValue("--sats"));
        } else if (arg == "--stations") {
            args.num_stations = std::stoi(needValue("--stations"));
        } else if (arg == "--altitude") {
            args.altitude_km = std::stod(needValue("--altitude"));
        } else if (arg == "--inclination") {
            args.inclination_deg = std::stod(needValue("--inclination"));
        } else if (arg == "--min-elev") {
            args.min_elevation_deg = std::stod(needValue("--min-elev"));
        } else if (arg == "--packets") {
            args.num_packets = std::stoi(needValue("--packets"));
        } else if (arg == "--reorder") {
            args.reorder_prob = std::stod(needValue("--reorder"));
        } else if (arg == "--drop") {
            args.drop_prob = std::stod(needValue("--drop"));
        } else if (arg == "--queues") {
            args.num_queues = std::stoi(needValue("--queues"));
        } else if (arg == "--handoff-sats") {
            args.num_handoff_sats = std::stoi(needValue("--handoff-sats"));
        } else if (arg == "--handoff-time") {
            args.handoff_time_sec = std::stod(needValue("--handoff-time"));
        } else if (arg == "--seed") {
            args.seed = static_cast<unsigned>(std::stoul(needValue("--seed")));
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return false;
        }
    }
    return true;
}

// ============================================================
// JSON Builders
// ============================================================
std::string buildVisibilityJson(const Args& args,
                                const std::vector<Satellite>& sats,
                                const std::vector<GroundStation>& stations,
                                const std::vector<VisibilityEdge>& edges,
                                const VisibilityStats& stats) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);

    os << "{";
    os << "\"meta\":{"
       << "\"num_planes\":" << args.num_planes << ","
       << "\"sats_per_plane\":" << args.sats_per_plane << ","
       << "\"altitude_km\":" << args.altitude_km << ","
       << "\"inclination_deg\":" << args.inclination_deg << ","
       << "\"min_elevation_deg\":" << args.min_elevation_deg
       << "},";

    os << "\"satellites\":[";
    for (size_t i = 0; i < sats.size(); i++) {
        const auto& s = sats[i];
        if (i) os << ",";
        os << "{"
           << "\"id\":" << s.id << ","
           << "\"lat\":" << s.position.lat_deg << ","
           << "\"lon\":" << s.position.lon_deg << ","
           << "\"alt\":" << s.altitude_km << ","
           << "\"plane\":" << s.orbital_plane
           << "}";
    }
    os << "],";

    os << "\"stations\":[";
    for (size_t i = 0; i < stations.size(); i++) {
        const auto& gs = stations[i];
        if (i) os << ",";
        os << "{"
           << "\"id\":" << gs.id << ","
           << "\"lat\":" << gs.position.lat_deg << ","
           << "\"lon\":" << gs.position.lon_deg << ","
           << "\"name\":\"" << jsonEscape(gs.name) << "\","
           << "\"min_elev\":" << gs.min_elevation_deg
           << "}";
    }
    os << "],";

    os << "\"edges\":[";
    for (size_t i = 0; i < edges.size(); i++) {
        const auto& e = edges[i];
        if (i) os << ",";
        os << "{"
           << "\"sat\":" << e.satellite_id << ","
           << "\"station\":" << e.station_id << ","
           << "\"elev\":" << e.elevation_deg << ","
           << "\"latency_ms\":" << e.latency_ms
           << "}";
    }
    os << "],";

    os << "\"stats\":{"
       << "\"edge_count\":" << stats.edge_count << ","
       << "\"min_elev\":" << stats.min_elev << ","
       << "\"max_elev\":" << stats.max_elev << ","
       << "\"avg_elev\":" << stats.avg_elev << ","
       << "\"min_latency\":" << stats.min_latency << ","
       << "\"max_latency\":" << stats.max_latency << ","
       << "\"avg_latency\":" << stats.avg_latency << ","
       << "\"coverage_counts\":[";
    for (size_t i = 0; i < stats.coverage_counts.size(); i++) {
        if (i) os << ",";
        os << stats.coverage_counts[i];
    }
    os << "]}";

    os << "}";
    return os.str();
}

std::string buildPacketJson(const PacketStats& stats) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);

    os << "{";
    os << "\"meta\":{"
       << "\"num_packets\":" << stats.num_packets << ","
       << "\"num_arrived\":" << stats.num_arrived << ","
       << "\"num_dropped\":" << stats.num_dropped << ","
       << "\"num_queues\":" << stats.num_queues << ","
       << "\"reorder_prob\":" << stats.reorder_prob << ","
       << "\"drop_prob\":" << stats.drop_prob
       << "},";

    os << "\"queue_counts\":[";
    for (size_t i = 0; i < stats.queue_counts.size(); i++) {
        if (i) os << ",";
        os << stats.queue_counts[i];
    }
    os << "],";

    os << "\"priority_counts\":[";
    for (size_t i = 0; i < stats.priority_counts.size(); i++) {
        if (i) os << ",";
        os << stats.priority_counts[i];
    }
    os << "],";

    os << "\"gaps\":[";
    for (size_t i = 0; i < stats.gaps.size(); i++) {
        if (i) os << ",";
        os << stats.gaps[i];
    }
    os << "],";

    os << "\"points\":[";
    for (size_t i = 0; i < stats.points.size(); i++) {
        const auto& p = stats.points[i];
        if (i) os << ",";
        os << "{"
           << "\"seq\":" << p.seq << ","
           << "\"arrival\":" << p.arrival << ","
           << "\"priority\":" << p.priority << ","
           << "\"destination\":" << p.destination
           << "}";
    }
    os << "]";
    os << "}";
    return os.str();
}

std::string buildHandoffJson(const Args& args,
                             const std::vector<VisibilityWindow>& windows,
                             const HandoffResult& result) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);

    os << "{";
    os << "\"meta\":{"
       << "\"min_overlap_sec\":" << HandoffScheduler::MIN_OVERLAP_SEC << ","
       << "\"min_signal_db\":" << HandoffScheduler::MIN_SIGNAL_DB << ","
       << "\"timeline_sec\":" << args.handoff_time_sec
       << "},";

    os << "\"windows\":[";
    for (size_t i = 0; i < windows.size(); i++) {
        const auto& w = windows[i];
        if (i) os << ",";
        os << "{"
           << "\"sat\":" << w.satellite_id << ","
           << "\"start\":" << w.start_time << ","
           << "\"end\":" << w.end_time << ","
           << "\"peak\":" << w.peak_signal_quality << ","
           << "\"start_signal\":" << w.start_signal_quality << ","
           << "\"end_signal\":" << w.end_signal_quality
           << "}";
    }
    os << "],";

    os << "\"handoffs\":[";
    for (size_t i = 0; i < result.handoffs.size(); i++) {
        const auto& h = result.handoffs[i];
        if (i) os << ",";
        os << "{"
           << "\"from\":" << h.from_satellite << ","
           << "\"to\":" << h.to_satellite << ","
           << "\"time\":" << h.handoff_time << ","
           << "\"overlap\":" << h.overlap_duration << ","
           << "\"signal\":" << h.signal_at_handoff
           << "}";
    }
    os << "],";

    os << "\"stats\":{"
       << "\"min_signal\":" << result.min_signal_quality << ","
       << "\"coverage_time\":" << result.total_coverage_time << ","
       << "\"gap_time\":" << result.total_gap_time << ","
       << "\"num_handoffs\":" << result.num_handoffs
       << "}";

    os << "}";
    return os.str();
}

// ============================================================
// Main
// ============================================================
int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        return 0;
    }

    std::cout << "Generating visualizer data...\n";

    // Visibility graph
    auto satellites = generateStarlinkConstellation(
        args.num_planes, args.sats_per_plane, args.altitude_km,
        args.inclination_deg);
    auto stations = generateGroundStations(args.num_stations);
    VisibilityStats vis_stats;
    auto edges = buildVisibilityEdges(
        satellites, stations, args.min_elevation_deg, vis_stats);
    auto vis_json = buildVisibilityJson(args, satellites, stations, edges, vis_stats);

    // Packet router
    auto packet_stats = simulatePacketStream(
        args.num_packets, args.num_queues, args.reorder_prob,
        args.drop_prob, args.seed);
    auto packet_json = buildPacketJson(packet_stats);

    // Handoff scheduler
    auto windows = generateWindows(
        args.num_handoff_sats, args.handoff_time_sec, args.seed + 1);
    auto handoff_result = HandoffScheduler::schedule(windows);
    auto handoff_json = buildHandoffJson(args, windows, handoff_result);

    // Output
    std::filesystem::path out_dir = VISUALIZER_DATA_DIR;
    std::filesystem::create_directories(out_dir);
    std::filesystem::path out_path = out_dir / "data.js";

    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "Failed to write data to " << out_path << "\n";
        return 1;
    }

    out << "window.VIS_DATA=" << vis_json << ";\n";
    out << "window.PACKET_DATA=" << packet_json << ";\n";
    out << "window.HANDOFF_DATA=" << handoff_json << ";\n";
    out.close();

    std::cout << "Wrote " << out_path << "\n";
    return 0;
}
