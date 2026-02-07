/**
 * Tests for Satellite Visibility Graph
 * Simple test harness — no external dependencies needed.
 */

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

// Pull in declarations from main (in production you'd have headers)
constexpr double EARTH_RADIUS_KM = 6371.0;
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / M_PI;

struct GeoCoord {
    double lat_deg;
    double lon_deg;
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

// ============================================================
// Test Cases
// ============================================================

void test_haversine_same_point() {
    GeoCoord a{47.67, -122.12};
    double dist = haversineDistanceKm(a, a);
    assert(dist < 0.001);
    std::cout << "  PASS: haversine same point = " << dist << " km\n";
}

void test_haversine_known_distance() {
    // New York to London: ~5,570 km
    GeoCoord ny{40.71, -74.01};
    GeoCoord london{51.51, -0.13};
    double dist = haversineDistanceKm(ny, london);
    assert(dist > 5500 && dist < 5650);
    std::cout << "  PASS: NY to London = " << dist << " km\n";
}

void test_elevation_directly_overhead() {
    GeoCoord station{0.0, 0.0};
    GeoCoord sat{0.0, 0.0};  // directly above
    double elev = computeElevationAngle(station, sat, 550.0);
    assert(elev > 85.0);  // Should be ~90 degrees
    std::cout << "  PASS: directly overhead elevation = " << elev << "°\n";
}

void test_elevation_far_away() {
    GeoCoord station{0.0, 0.0};
    GeoCoord sat{45.0, 45.0};  // far away
    double elev = computeElevationAngle(station, sat, 550.0);
    assert(elev < 10.0);  // Should be very low
    std::cout << "  PASS: far satellite elevation = " << elev << "°\n";
}

void test_elevation_at_starlink_altitude() {
    // At 550 km altitude, a satellite ~1000 km ground distance should be
    // at roughly 25-30° elevation
    GeoCoord station{0.0, 0.0};
    GeoCoord sat{9.0, 0.0};  // ~1000 km away on surface
    double elev = computeElevationAngle(station, sat, 550.0);
    std::cout << "  INFO: 550km alt, ~1000km ground dist: elev = " << elev << "°\n";
    assert(elev > 10.0 && elev < 60.0);  // Reasonable range
    std::cout << "  PASS: reasonable elevation at Starlink altitude\n";
}

void test_elevation_symmetry() {
    GeoCoord station{30.0, -90.0};
    GeoCoord sat{35.0, -85.0};
    double elev1 = computeElevationAngle(station, sat, 550.0);

    // Swap roles (roughly — not exact because altitude matters)
    GeoCoord station2{35.0, -85.0};
    GeoCoord sat2{30.0, -90.0};
    double elev2 = computeElevationAngle(station2, sat2, 550.0);

    // Should be similar (not exact due to Earth curvature)
    assert(std::abs(elev1 - elev2) < 5.0);
    std::cout << "  PASS: elevation symmetry: " << elev1 << "° vs " << elev2 << "°\n";
}

int main() {
    std::cout << "=== Satellite Visibility Tests ===\n\n";

    std::cout << "Haversine Distance:\n";
    test_haversine_same_point();
    test_haversine_known_distance();

    std::cout << "\nElevation Angle:\n";
    test_elevation_directly_overhead();
    test_elevation_far_away();
    test_elevation_at_starlink_altitude();
    test_elevation_symmetry();

    std::cout << "\n=== All tests passed ===\n";
    return 0;
}
