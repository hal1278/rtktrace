#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <string_view>

#include "plotcore/analysis/coordinates.hpp"

namespace {

constexpr double wgs84_semi_major_axis_m = 6'378'137.0;
constexpr double wgs84_semi_minor_axis_m = 6'356'752.314245179;
constexpr double pi = std::numbers::pi_v<double>;
int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(double actual, double expected, double tolerance) noexcept
{
    return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] double longitude_difference(double lhs, double rhs) noexcept
{
    return std::remainder(lhs - rhs, 2.0 * pi);
}

void test_known_wgs84_points()
{
    using namespace plotcore;

    const Ecef equator = wgs84_llh_to_ecef(Wgs84Llh{0.0, 0.0, 0.0});
    check(near(equator.x_m, wgs84_semi_major_axis_m, 1.0e-9), "equator ECEF x");
    check(near(equator.y_m, 0.0, 1.0e-9), "equator ECEF y");
    check(near(equator.z_m, 0.0, 1.0e-9), "equator ECEF z");

    const Ecef east_equator = wgs84_llh_to_ecef(Wgs84Llh{0.0, pi / 2.0, 0.0});
    check(near(east_equator.x_m, 0.0, 1.0e-8), "90 degree longitude ECEF x");
    check(near(east_equator.y_m, wgs84_semi_major_axis_m, 1.0e-9), "90 degree longitude ECEF y");
    check(near(east_equator.z_m, 0.0, 1.0e-9), "90 degree longitude ECEF z");

    const Ecef north_pole = wgs84_llh_to_ecef(Wgs84Llh{pi / 2.0, 0.0, 0.0});
    check(near(north_pole.x_m, 0.0, 1.0e-8), "north pole ECEF x");
    check(near(north_pole.y_m, 0.0, 1.0e-9), "north pole ECEF y");
    check(near(north_pole.z_m, wgs84_semi_minor_axis_m, 1.0e-8), "north pole ECEF z");

    const auto pole_llh = wgs84_ecef_to_llh(Ecef{0.0, 0.0, wgs84_semi_minor_axis_m + 25.0});
    check(pole_llh.has_value(), "polar ECEF converts to LLH");
    if (pole_llh.has_value()) {
        check(near(pole_llh->latitude_rad, pi / 2.0, 1.0e-15), "polar latitude");
        check(near(pole_llh->longitude_rad, 0.0, 1.0e-15), "polar longitude convention");
        check(near(pole_llh->ellipsoidal_height_m, 25.0, 1.0e-9), "polar height");
    }
}

void test_round_trips()
{
    using plotcore::Wgs84Llh;

    constexpr std::array positions{
        Wgs84Llh{35.681236 * pi / 180.0, 139.767125 * pi / 180.0, 42.25},
        Wgs84Llh{-33.8688 * pi / 180.0, 151.2093 * pi / 180.0, 125.5},
        Wgs84Llh{64.1466 * pi / 180.0, -21.9426 * pi / 180.0, -12.0},
        Wgs84Llh{0.001 * pi / 180.0, -179.999 * pi / 180.0, 20'000.0},
    };

    for (const Wgs84Llh original : positions) {
        const auto converted = plotcore::wgs84_ecef_to_llh(plotcore::wgs84_llh_to_ecef(original));
        check(converted.has_value(), "LLH/ECEF round trip produces a result");
        if (!converted.has_value()) {
            continue;
        }
        check(near(converted->latitude_rad, original.latitude_rad, 1.0e-12),
            "LLH/ECEF round trip latitude");
        check(std::abs(longitude_difference(converted->longitude_rad, original.longitude_rad))
                <= 1.0e-12,
            "LLH/ECEF round trip longitude");
        check(near(converted->ellipsoidal_height_m, original.ellipsoidal_height_m, 1.0e-5),
            "LLH/ECEF round trip height");
    }
}

void test_undefined_inverse_inputs()
{
    using plotcore::Ecef;
    using plotcore::wgs84_ecef_to_llh;

    check(!wgs84_ecef_to_llh(Ecef{0.0, 0.0, 0.0}).has_value(), "Earth centre has no unique LLH");
    check(!wgs84_ecef_to_llh(Ecef{std::numeric_limits<double>::infinity(), 0.0, 0.0}).has_value(),
        "non-finite ECEF is rejected");
}

void test_ecef_enu_transform()
{
    using namespace plotcore;

    const std::optional<EnuReference> reference = make_enu_reference(Wgs84Llh{0.0, 0.0, 0.0});
    check(reference.has_value(), "finite LLH creates an ENU reference");
    if (!reference.has_value()) {
        return;
    }
    const Ecef origin = reference->origin_ecef;
    const Enu east = ecef_to_enu(Ecef{origin.x_m, origin.y_m + 2.0, origin.z_m}, *reference);
    check(near(east.east_m, 2.0, 1.0e-12), "equatorial ECEF y maps to east");
    check(near(east.north_m, 0.0, 1.0e-12), "east displacement has no north component");
    check(near(east.up_m, 0.0, 1.0e-12), "east displacement has no up component");

    const Enu north = ecef_to_enu(Ecef{origin.x_m, origin.y_m, origin.z_m + 3.0}, *reference);
    check(near(north.north_m, 3.0, 1.0e-12), "equatorial ECEF z maps to north");
    const Enu up = ecef_to_enu(Ecef{origin.x_m + 4.0, origin.y_m, origin.z_m}, *reference);
    check(near(up.up_m, 4.0, 1.0e-12), "equatorial ECEF x maps to up");

    const Enu arbitrary{12.5, -3.25, 8.75};
    const Ecef converted = enu_to_ecef(arbitrary, *reference);
    const Enu round_trip = ecef_to_enu(converted, *reference);
    check(near(round_trip.east_m, arbitrary.east_m, 1.0e-12), "ENU/ECEF east round trip");
    check(near(round_trip.north_m, arbitrary.north_m, 1.0e-12), "ENU/ECEF north round trip");
    check(near(round_trip.up_m, arbitrary.up_m, 1.0e-9), "ENU/ECEF up round trip");

    check(!make_enu_reference(Ecef{}).has_value(), "Earth centre cannot define ENU rotation");
    check(!make_enu_reference(Wgs84Llh{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0})
              .has_value(),
        "non-finite LLH cannot define an ENU reference");
}

} // namespace

int main()
{
    test_known_wgs84_points();
    test_round_trips();
    test_undefined_inverse_inputs();
    test_ecef_enu_transform();

    if (failures != 0) {
        std::cerr << failures << " coordinate test(s) failed\n";
        return 1;
    }
    return 0;
}
