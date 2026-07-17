#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

namespace plotcore {

struct GpsTime {
    std::int64_t nanoseconds_since_gps_epoch;

    auto operator<=>(const GpsTime&) const = default;
};

[[nodiscard]] constexpr std::int64_t operator-(GpsTime lhs, GpsTime rhs) noexcept
{
    return lhs.nanoseconds_since_gps_epoch - rhs.nanoseconds_since_gps_epoch;
}

struct Wgs84Llh {
    double latitude_rad;
    double longitude_rad;
    double ellipsoidal_height_m;
};

struct Ecef {
    double x_m;
    double y_m;
    double z_m;
};

struct Enu {
    double east_m;
    double north_m;
    double up_m;
};

enum class SolutionQuality : std::uint8_t {
    InvalidOrUnknown = 0,
    Fixed = 1,
    Float = 2,
    Sbas = 3,
    Dgps = 4,
    Single = 5,
    Ppp = 6,
};

inline constexpr std::size_t solution_quality_count = 7;

struct NormalizedSample {
    GpsTime time;
    Wgs84Llh llh;
    Ecef ecef;
    Enu enu;
    SolutionQuality quality;
    // Physical input line number, starting at one.
    std::size_t source_line_number;
    bool continuous_from_previous;
};

} // namespace plotcore
