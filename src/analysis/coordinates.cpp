#include "rtktrace/analysis/coordinates.hpp"

#include <cmath>
#include <numbers>

namespace rtktrace {
namespace {

constexpr double wgs84_semi_major_axis_m = 6'378'137.0;
constexpr double wgs84_inverse_flattening = 298.257223563;
constexpr double wgs84_flattening = 1.0 / wgs84_inverse_flattening;
constexpr double wgs84_eccentricity_squared = wgs84_flattening * (2.0 - wgs84_flattening);
constexpr double wgs84_semi_minor_axis_m = wgs84_semi_major_axis_m * (1.0 - wgs84_flattening);
constexpr int inverse_max_iterations = 16;
constexpr double inverse_latitude_tolerance_rad = 1.0e-14;

} // namespace

Ecef wgs84_llh_to_ecef(Wgs84Llh llh) noexcept
{
    const double sin_latitude = std::sin(llh.latitude_rad);
    const double cos_latitude = std::cos(llh.latitude_rad);
    const double prime_vertical_radius_m = wgs84_semi_major_axis_m
        / std::sqrt(1.0 - wgs84_eccentricity_squared * sin_latitude * sin_latitude);

    return Ecef{
        .x_m = (prime_vertical_radius_m + llh.ellipsoidal_height_m) * cos_latitude
            * std::cos(llh.longitude_rad),
        .y_m = (prime_vertical_radius_m + llh.ellipsoidal_height_m) * cos_latitude
            * std::sin(llh.longitude_rad),
        .z_m = (prime_vertical_radius_m * (1.0 - wgs84_eccentricity_squared)
                   + llh.ellipsoidal_height_m)
            * sin_latitude,
    };
}

std::optional<Wgs84Llh> wgs84_ecef_to_llh(Ecef ecef) noexcept
{
    if (!std::isfinite(ecef.x_m) || !std::isfinite(ecef.y_m) || !std::isfinite(ecef.z_m)) {
        return std::nullopt;
    }

    const double distance_from_axis_m = std::hypot(ecef.x_m, ecef.y_m);
    if (distance_from_axis_m == 0.0) {
        if (ecef.z_m == 0.0) {
            return std::nullopt;
        }
        return Wgs84Llh{
            .latitude_rad = std::copysign(std::numbers::pi_v<double> / 2.0, ecef.z_m),
            .longitude_rad = 0.0,
            .ellipsoidal_height_m = std::abs(ecef.z_m) - wgs84_semi_minor_axis_m,
        };
    }

    double latitude_rad =
        std::atan2(ecef.z_m, distance_from_axis_m * (1.0 - wgs84_eccentricity_squared));
    bool converged = false;
    for (int iteration = 0; iteration < inverse_max_iterations; ++iteration) {
        const double sin_latitude = std::sin(latitude_rad);
        const double prime_vertical_radius_m = wgs84_semi_major_axis_m
            / std::sqrt(1.0 - wgs84_eccentricity_squared * sin_latitude * sin_latitude);
        const double next_latitude_rad = std::atan2(
            ecef.z_m + wgs84_eccentricity_squared * prime_vertical_radius_m * sin_latitude,
            distance_from_axis_m);
        if (std::abs(next_latitude_rad - latitude_rad) <= inverse_latitude_tolerance_rad) {
            latitude_rad = next_latitude_rad;
            converged = true;
            break;
        }
        latitude_rad = next_latitude_rad;
    }
    if (!converged) {
        return std::nullopt;
    }

    const double sin_latitude = std::sin(latitude_rad);
    const double cos_latitude = std::cos(latitude_rad);
    const double prime_vertical_radius_m = wgs84_semi_major_axis_m
        / std::sqrt(1.0 - wgs84_eccentricity_squared * sin_latitude * sin_latitude);
    const double height_m = std::abs(cos_latitude) >= std::abs(sin_latitude)
        ? distance_from_axis_m / cos_latitude - prime_vertical_radius_m
        : ecef.z_m / sin_latitude - prime_vertical_radius_m * (1.0 - wgs84_eccentricity_squared);

    if (!std::isfinite(height_m)) {
        return std::nullopt;
    }
    return Wgs84Llh{
        .latitude_rad = latitude_rad,
        .longitude_rad = std::atan2(ecef.y_m, ecef.x_m),
        .ellipsoidal_height_m = height_m,
    };
}

std::optional<EnuReference> make_enu_reference(Wgs84Llh llh) noexcept
{
    if (!std::isfinite(llh.latitude_rad) || !std::isfinite(llh.longitude_rad)
        || !std::isfinite(llh.ellipsoidal_height_m)) {
        return std::nullopt;
    }
    const Ecef origin = wgs84_llh_to_ecef(llh);
    if (!std::isfinite(origin.x_m) || !std::isfinite(origin.y_m) || !std::isfinite(origin.z_m)) {
        return std::nullopt;
    }
    return EnuReference{origin, llh.latitude_rad, llh.longitude_rad};
}

std::optional<EnuReference> make_enu_reference(Ecef ecef) noexcept
{
    const std::optional<Wgs84Llh> llh = wgs84_ecef_to_llh(ecef);
    if (!llh.has_value()) {
        return std::nullopt;
    }
    return EnuReference{ecef, llh->latitude_rad, llh->longitude_rad};
}

Enu ecef_to_enu(Ecef position, const EnuReference& reference) noexcept
{
    const double delta_x = position.x_m - reference.origin_ecef.x_m;
    const double delta_y = position.y_m - reference.origin_ecef.y_m;
    const double delta_z = position.z_m - reference.origin_ecef.z_m;
    const double sin_latitude = std::sin(reference.latitude_rad);
    const double cos_latitude = std::cos(reference.latitude_rad);
    const double sin_longitude = std::sin(reference.longitude_rad);
    const double cos_longitude = std::cos(reference.longitude_rad);

    return Enu{
        .east_m = -sin_longitude * delta_x + cos_longitude * delta_y,
        .north_m = -sin_latitude * cos_longitude * delta_x - sin_latitude * sin_longitude * delta_y
            + cos_latitude * delta_z,
        .up_m = cos_latitude * cos_longitude * delta_x + cos_latitude * sin_longitude * delta_y
            + sin_latitude * delta_z,
    };
}

Ecef enu_to_ecef(Enu position, const EnuReference& reference) noexcept
{
    const double sin_latitude = std::sin(reference.latitude_rad);
    const double cos_latitude = std::cos(reference.latitude_rad);
    const double sin_longitude = std::sin(reference.longitude_rad);
    const double cos_longitude = std::cos(reference.longitude_rad);

    return Ecef{
        .x_m = reference.origin_ecef.x_m - sin_longitude * position.east_m
            - sin_latitude * cos_longitude * position.north_m
            + cos_latitude * cos_longitude * position.up_m,
        .y_m = reference.origin_ecef.y_m + cos_longitude * position.east_m
            - sin_latitude * sin_longitude * position.north_m
            + cos_latitude * sin_longitude * position.up_m,
        .z_m = reference.origin_ecef.z_m + cos_latitude * position.north_m
            + sin_latitude * position.up_m,
    };
}

} // namespace rtktrace
