#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "plotcore/model/sample.hpp"

namespace plotcore {

struct UtcCivilTime {
    int year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned second;
    std::uint32_t nanosecond;
};

// Returns 1,000,000,000 when rounding carries into the next whole second.
[[nodiscard]] std::optional<std::int64_t> round_fractional_seconds_to_nanoseconds(
    std::string_view fractional_digits) noexcept;

[[nodiscard]] std::optional<GpsTime> utc_civil_to_gps_time(UtcCivilTime utc) noexcept;

} // namespace plotcore
