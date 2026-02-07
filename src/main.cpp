/**
 * Satellite Visibility Graph & Minimum Coverage Solver
 * =====================================================
 * Stuart Ray — Starlink Interview Prep Project
 *
 * Demonstrates:
 *   - Graph construction from orbital mechanics constraints
 *   - Greedy set cover approximation (NP-hard → O(N*M*log(N)) approx)
 *   - Multi-threaded visibility computation
 *   - C++17 idioms: structured bindings, std::optional, RAII
 *
 * Starlink relevance:
 *   - Directly models satellite-to-ground-station visibility
 *   - Same math used in beam assignment and handoff prediction
 *   - Concurrency pattern matches real ground station software
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <thread>
#include <unordered_set>
#include <vector>

// ============================================================
// Constants
// ============================================================
constexpr double EARTH_RADIUS_KM = 6371.0;
constexpr double MIN_ELEVATION_DEG = 25.0;
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / M_PI;

// ============================================================
// Data Structures
// ============================================================
struct GeoCoord {
    double lat_deg;  // degrees, -90 to 90
    double lon_deg;  // degrees, -180 to 180
};

struct Satellite {
    int id;
    GeoCoord position;
    double altitude_km;  // above Earth surface
    int orbital_plane;
    double capacity_mbps;
};

struct GroundStation {
    int id;
    GeoCoord position;
    double min_elevation_deg;
    double capacity_mbps;
};

struct VisibilityEdge {
    int satellite_id;
    int station_id;
    double elevation_deg;
    double distance_km;
    double estimated_latency_ms;
};

// ============================================================
// Orbital Mechanics Helpers
// ============================================================

/**
 * Compute great-circle distance between two points on Earth's surface.
 * Uses Haversine formula — accurate for all distances.
 */
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

/**
 * Compute elevation angle from a ground station to a satellite.
 *
 * This is the angle above the horizon as seen from the ground station.
 * Uses the geometric relationship between ground distance, Earth radius,
 * and satellite altitude.
 *
 * Elevation > 25° is typical minimum for reliable Starlink links
 * (atmospheric attenuation, multipath, and rain fade worsen at low angles).
 */
double computeElevationAngle(const GeoCoord& station, const GeoCoord& sat_pos,
                              double sat_altitude_km) {
    double ground_dist_km = haversineDistanceKm(station, sat_pos);

    // Central angle subtended at Earth's center
    double central_angle = ground_dist_km / EARTH_RADIUS_KM;

    // Satellite distance from Earth's center
    double r_sat = EARTH_RADIUS_KM + sat_altitude_km;

    // Slant range (distance from station to satellite)
    double slant_range = std::sqrt(
        EARTH_RADIUS_KM * EARTH_RADIUS_KM +
        r_sat * r_sat -
        2 * EARTH_RADIUS_KM * r_sat * std::cos(central_angle));

    if (slant_range < 1e-6) return 90.0;  // directly overhead

    // Elevation angle using law of cosines
    double cos_elevation = (slant_range * slant_range +
                            EARTH_RADIUS_KM * EARTH_RADIUS_KM -
                            r_sat * r_sat) /
                           (2 * slant_range * EARTH_RADIUS_KM);

    // Elevation is complement of the angle at the station
    double angle_at_station = std::acos(std::clamp(cos_elevation, -1.0, 1.0));
    return (angle_at_station * RAD_TO_DEG) - 90.0;
}

/**
 * Compute signal propagation delay (one-way) in milliseconds.
 * Speed of light: ~299,792 km/s
 */
double computeLatencyMs(double slant_range_km) {
    return slant_range_km / 299.792;  // ms
}

// ============================================================
// Visibility Graph Builder (Multi-threaded)
// ============================================================

class VisibilityGraph {
public:
    VisibilityGraph(const std::vector<Satellite>& sats,
                    const std::vector<GroundStation>& stations,
                    int num_threads = std::thread::hardware_concurrency())
        : satellites_(sats), stations_(stations) {
        buildGraph(num_threads);
    }

    /** Get all edges (satellite-station pairs with visibility) */
    const std::vector<VisibilityEdge>& edges() const { return edges_; }

    /** Get satellites visible from a specific station */
    std::vector<int> satellitesVisibleFrom(int station_id) const {
        std::vector<int> result;
        for (const auto& edge : edges_) {
            if (edge.station_id == station_id) {
                result.push_back(edge.satellite_id);
            }
        }
        return result;
    }

    /** Get stations covered by a specific satellite */
    std::vector<int> stationsCoveredBy(int satellite_id) const {
        std::vector<int> result;
        for (const auto& edge : edges_) {
            if (edge.satellite_id == satellite_id) {
                result.push_back(edge.station_id);
            }
        }
        return result;
    }

    /**
     * Greedy Set Cover: Find minimum satellites to cover all stations.
     *
     * This is NP-hard in general; greedy gives O(ln n) approximation.
     * At Starlink scale this runs in the constellation management software
     * to compute minimum active satellites for coverage guarantees.
     */
    std::vector<int> minimumCoverageSatellites() const {
        int M = static_cast<int>(stations_.size());
        std::unordered_set<int> uncovered;
        for (int j = 0; j < M; j++) uncovered.insert(j);

        // Build coverage map: satellite_id -> set of station_ids
        std::unordered_map<int, std::vector<int>> coverage;
        for (const auto& edge : edges_) {
            coverage[edge.satellite_id].push_back(edge.station_id);
        }

        std::vector<int> selected;

        while (!uncovered.empty()) {
            // Find satellite covering most uncovered stations
            int best_sat = -1;
            int best_count = 0;

            for (const auto& [sat_id, covered_stations] : coverage) {
                int count = 0;
                for (int s : covered_stations) {
                    if (uncovered.count(s)) count++;
                }
                if (count > best_count) {
                    best_count = count;
                    best_sat = sat_id;
                }
            }

            if (best_sat == -1) {
                std::cerr << "WARNING: Cannot cover all stations. "
                          << uncovered.size() << " stations unreachable.\n";
                break;
            }

            selected.push_back(best_sat);
            for (int s : coverage[best_sat]) {
                uncovered.erase(s);
            }
        }

        return selected;
    }

    /**
     * Find critical satellites — single points of failure.
     *
     * A satellite is critical if removing it leaves a station with
     * zero coverage. Uses articulation point detection on bipartite graph.
     */
    std::vector<int> findCriticalSatellites() const {
        // Count how many satellites cover each station
        std::unordered_map<int, int> station_coverage_count;
        std::unordered_map<int, std::vector<int>> station_to_sats;

        for (const auto& edge : edges_) {
            station_coverage_count[edge.station_id]++;
            station_to_sats[edge.station_id].push_back(edge.satellite_id);
        }

        // A satellite is critical if it's the ONLY satellite covering
        // at least one station
        std::unordered_set<int> critical;
        for (const auto& [station_id, count] : station_coverage_count) {
            if (count == 1) {
                critical.insert(station_to_sats[station_id][0]);
            }
        }

        return std::vector<int>(critical.begin(), critical.end());
    }

    void printStats() const {
        std::cout << "=== Visibility Graph Statistics ===\n";
        std::cout << "Satellites: " << satellites_.size() << "\n";
        std::cout << "Ground Stations: " << stations_.size() << "\n";
        std::cout << "Visibility Edges: " << edges_.size() << "\n";

        if (!edges_.empty()) {
            double avg_elev = 0, min_elev = 90, max_elev = 0;
            double avg_lat = 0, min_lat = 1e9, max_lat = 0;
            for (const auto& e : edges_) {
                avg_elev += e.elevation_deg;
                min_elev = std::min(min_elev, e.elevation_deg);
                max_elev = std::max(max_elev, e.elevation_deg);
                avg_lat += e.estimated_latency_ms;
                min_lat = std::min(min_lat, e.estimated_latency_ms);
                max_lat = std::max(max_lat, e.estimated_latency_ms);
            }
            avg_elev /= edges_.size();
            avg_lat /= edges_.size();

            std::cout << "Elevation: min=" << min_elev << "° avg=" << avg_elev
                      << "° max=" << max_elev << "°\n";
            std::cout << "Latency:   min=" << min_lat << "ms avg=" << avg_lat
                      << "ms max=" << max_lat << "ms\n";
        }

        // Coverage density
        std::unordered_map<int, int> sats_per_station;
        for (const auto& e : edges_) {
            sats_per_station[e.station_id]++;
        }
        if (!sats_per_station.empty()) {
            int min_cov = INT_MAX, max_cov = 0;
            double avg_cov = 0;
            for (const auto& [_, count] : sats_per_station) {
                min_cov = std::min(min_cov, count);
                max_cov = std::max(max_cov, count);
                avg_cov += count;
            }
            avg_cov /= sats_per_station.size();
            std::cout << "Satellites per station: min=" << min_cov
                      << " avg=" << avg_cov << " max=" << max_cov << "\n";
        }
    }

private:
    void buildGraph(int num_threads) {
        int N = static_cast<int>(satellites_.size());
        int chunk_size = (N + num_threads - 1) / num_threads;

        std::vector<std::thread> threads;
        std::vector<std::vector<VisibilityEdge>> thread_results(num_threads);

        auto start = std::chrono::high_resolution_clock::now();

        for (int t = 0; t < num_threads; t++) {
            int begin = t * chunk_size;
            int end = std::min(begin + chunk_size, N);

            threads.emplace_back([this, begin, end, t, &thread_results]() {
                for (int i = begin; i < end; i++) {
                    const auto& sat = satellites_[i];
                    for (int j = 0; j < static_cast<int>(stations_.size()); j++) {
                        const auto& gs = stations_[j];
                        double elev = computeElevationAngle(
                            gs.position, sat.position, sat.altitude_km);

                        if (elev >= gs.min_elevation_deg) {
                            // Compute slant range for latency
                            double ground_dist = haversineDistanceKm(
                                gs.position, sat.position);
                            double central_angle = ground_dist / EARTH_RADIUS_KM;
                            double r_sat = EARTH_RADIUS_KM + sat.altitude_km;
                            double slant = std::sqrt(
                                EARTH_RADIUS_KM * EARTH_RADIUS_KM +
                                r_sat * r_sat -
                                2 * EARTH_RADIUS_KM * r_sat *
                                    std::cos(central_angle));

                            thread_results[t].push_back({
                                sat.id, gs.id, elev, slant,
                                computeLatencyMs(slant)});
                        }
                    }
                }
            });
        }

        for (auto& t : threads) t.join();

        // Merge results
        for (const auto& results : thread_results) {
            edges_.insert(edges_.end(), results.begin(), results.end());
        }

        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        double ms = std::chrono::duration<double, std::milli>(elapsed).count();
        std::cout << "Visibility graph built in " << ms << " ms ("
                  << num_threads << " threads)\n";
    }

    std::vector<Satellite> satellites_;
    std::vector<GroundStation> stations_;
    std::vector<VisibilityEdge> edges_;
};

// ============================================================
// Constellation Generator (Starlink-like Walker constellation)
// ============================================================

std::vector<Satellite> generateStarlinkConstellation(
    int num_planes, int sats_per_plane, double altitude_km,
    double inclination_deg) {
    std::vector<Satellite> sats;
    int id = 0;

    for (int p = 0; p < num_planes; p++) {
        double raan = (360.0 / num_planes) * p;  // Right ascension of ascending node

        for (int s = 0; s < sats_per_plane; s++) {
            double true_anomaly = (360.0 / sats_per_plane) * s;

            // Convert orbital elements to lat/lon (simplified)
            double angle = (raan + true_anomaly) * DEG_TO_RAD;
            double lat = inclination_deg * std::sin(angle);
            double lon = std::fmod(raan + true_anomaly * std::cos(inclination_deg * DEG_TO_RAD), 360.0) - 180.0;

            sats.push_back({
                id++,
                {lat, lon},
                altitude_km,
                p,
                250.0  // Mbps capacity per satellite
            });
        }
    }

    return sats;
}

std::vector<GroundStation> generateGroundStations(int count) {
    // Major cities as ground station locations
    std::vector<GeoCoord> city_coords = {
        {47.67, -122.12},   // Redmond, WA
        {37.77, -122.42},   // San Francisco, CA
        {40.71, -74.01},    // New York, NY
        {51.51, -0.13},     // London, UK
        {35.68, 139.69},    // Tokyo, Japan
        {-33.87, 151.21},   // Sydney, Australia
        {48.86, 2.35},      // Paris, France
        {55.76, 37.62},     // Moscow, Russia
        {-23.55, -46.63},   // São Paulo, Brazil
        {28.61, 77.23},     // New Delhi, India
        {1.35, 103.82},     // Singapore
        {-1.29, 36.82},     // Nairobi, Kenya
        {45.42, -75.70},    // Ottawa, Canada
        {38.72, -9.14},     // Lisbon, Portugal
        {-34.60, -58.38},   // Buenos Aires, Argentina
        {25.20, 55.27},     // Dubai, UAE
        {35.69, 51.39},     // Tehran, Iran
        {59.33, 18.07},     // Stockholm, Sweden
        {64.14, -21.94},    // Reykjavik, Iceland
        {-41.29, 174.78},   // Wellington, NZ
    };

    std::vector<GroundStation> stations;
    for (int i = 0; i < std::min(count, static_cast<int>(city_coords.size())); i++) {
        stations.push_back({
            i,
            city_coords[i],
            MIN_ELEVATION_DEG,
            10000.0  // Mbps capacity
        });
    }
    return stations;
}

// ============================================================
// Main
// ============================================================

int main() {
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║  Starlink Constellation Visibility Solver    ║\n";
    std::cout << "║  Stuart Ray — Interview Prep Project         ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n\n";

    // Generate a Starlink-like constellation
    // Real Starlink Shell 1: 72 planes × 22 sats = 1,584 satellites at 550 km
    // Using smaller numbers for quick demo
    constexpr int NUM_PLANES = 36;
    constexpr int SATS_PER_PLANE = 20;
    constexpr double ALTITUDE_KM = 550.0;
    constexpr double INCLINATION_DEG = 53.0;
    constexpr int NUM_GROUND_STATIONS = 20;

    std::cout << "Generating constellation: " << NUM_PLANES << " planes × "
              << SATS_PER_PLANE << " sats = " << NUM_PLANES * SATS_PER_PLANE
              << " satellites at " << ALTITUDE_KM << " km\n\n";

    auto satellites = generateStarlinkConstellation(
        NUM_PLANES, SATS_PER_PLANE, ALTITUDE_KM, INCLINATION_DEG);
    auto stations = generateGroundStations(NUM_GROUND_STATIONS);

    // Build visibility graph
    VisibilityGraph graph(satellites, stations);
    graph.printStats();

    // Find minimum coverage set
    std::cout << "\n=== Minimum Coverage Analysis ===\n";
    auto min_sats = graph.minimumCoverageSatellites();
    std::cout << "Minimum satellites for full coverage: " << min_sats.size()
              << " (out of " << satellites.size() << ")\n";

    // Find critical satellites
    auto critical = graph.findCriticalSatellites();
    std::cout << "Critical satellites (single points of failure): "
              << critical.size() << "\n";

    if (!critical.empty()) {
        std::cout << "  IDs: ";
        for (int i = 0; i < std::min(10, static_cast<int>(critical.size())); i++) {
            std::cout << critical[i] << " ";
        }
        if (critical.size() > 10) std::cout << "...";
        std::cout << "\n";
    }

    // Per-station coverage report
    std::cout << "\n=== Per-Station Coverage ===\n";
    for (const auto& gs : stations) {
        auto visible = graph.satellitesVisibleFrom(gs.id);
        std::cout << "Station " << gs.id << " ("
                  << gs.position.lat_deg << "°, " << gs.position.lon_deg
                  << "°): " << visible.size() << " satellites visible\n";
    }

    return 0;
}
