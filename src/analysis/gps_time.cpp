#include "plotcore/analysis/gps_time.hpp"

#include <array>
#include <chrono>
#include <limits>

namespace plotcore {
namespace {

struct LeapSecondTransition {
    std::chrono::year_month_day effective_date;
    int gps_utc_offset_after_seconds;
};

using namespace std::chrono_literals;

// Effective at 00:00:00 UTC. GPS-UTC equals TAI-UTC minus 19 seconds.
constexpr std::array leap_second_transitions{
    LeapSecondTransition{1981y / std::chrono::July / 1, 1},
    LeapSecondTransition{1982y / std::chrono::July / 1, 2},
    LeapSecondTransition{1983y / std::chrono::July / 1, 3},
    LeapSecondTransition{1985y / std::chrono::July / 1, 4},
    LeapSecondTransition{1988y / std::chrono::January / 1, 5},
    LeapSecondTransition{1990y / std::chrono::January / 1, 6},
    LeapSecondTransition{1991y / std::chrono::January / 1, 7},
    LeapSecondTransition{1992y / std::chrono::July / 1, 8},
    LeapSecondTransition{1993y / std::chrono::July / 1, 9},
    LeapSecondTransition{1994y / std::chrono::July / 1, 10},
    LeapSecondTransition{1996y / std::chrono::January / 1, 11},
    LeapSecondTransition{1997y / std::chrono::July / 1, 12},
    LeapSecondTransition{1999y / std::chrono::January / 1, 13},
    LeapSecondTransition{2006y / std::chrono::January / 1, 14},
    LeapSecondTransition{2009y / std::chrono::January / 1, 15},
    LeapSecondTransition{2012y / std::chrono::July / 1, 16},
    LeapSecondTransition{2015y / std::chrono::July / 1, 17},
    LeapSecondTransition{2017y / std::chrono::January / 1, 18},
};

constexpr std::chrono::year_month_day gps_epoch_date =
    1980y / std::chrono::January / 6;
constexpr std::int64_t seconds_per_day = 86'400;
constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;

[[nodiscard]] std::optional<int> gps_utc_offset_seconds(
    std::chrono::sys_days date, bool is_leap_second) noexcept
{
    if (is_leap_second) {
        const std::chrono::sys_days next_date = date + std::chrono::days{1};
        for (const LeapSecondTransition& transition : leap_second_transitions) {
            if (std::chrono::sys_days{transition.effective_date} == next_date) {
                return transition.gps_utc_offset_after_seconds - 1;
            }
        }
        return std::nullopt;
    }

    int offset_seconds = 0;
    for (const LeapSecondTransition& transition : leap_second_transitions) {
        if (date < std::chrono::sys_days{transition.effective_date}) {
            break;
        }
        offset_seconds = transition.gps_utc_offset_after_seconds;
    }
    return offset_seconds;
}

} // namespace

std::optional<std::int64_t> round_fractional_seconds_to_nanoseconds(
    std::string_view fractional_digits) noexcept
{
    std::int64_t nanoseconds = 0;
    const std::size_t retained_digits = fractional_digits.size() < 9
        ? fractional_digits.size()
        : 9;
    for (std::size_t index = 0; index < fractional_digits.size(); ++index) {
        const char digit = fractional_digits[index];
        if (digit < '0' || digit > '9') {
            return std::nullopt;
        }
        if (index < retained_digits) {
            nanoseconds = nanoseconds * 10 + (digit - '0');
        }
    }
    for (std::size_t index = retained_digits; index < 9; ++index) {
        nanoseconds *= 10;
    }
    if (fractional_digits.size() > 9 && fractional_digits[9] >= '5') {
        ++nanoseconds;
    }
    return nanoseconds;
}

std::optional<GpsTime> utc_civil_to_gps_time(UtcCivilTime utc) noexcept
{
    const std::chrono::year_month_day date{
        std::chrono::year{utc.year}, std::chrono::month{utc.month}, std::chrono::day{utc.day}};
    if (!date.ok() || utc.hour >= 24 || utc.minute >= 60 || utc.second > 60
        || utc.nanosecond >= nanoseconds_per_second) {
        return std::nullopt;
    }

    const std::chrono::sys_days day_point{date};
    const std::chrono::sys_days gps_epoch{gps_epoch_date};
    if (day_point < gps_epoch) {
        return std::nullopt;
    }

    const bool is_leap_second = utc.second == 60;
    if (is_leap_second && (utc.hour != 23 || utc.minute != 59)) {
        return std::nullopt;
    }
    const std::optional<int> offset_seconds = gps_utc_offset_seconds(day_point, is_leap_second);
    if (!offset_seconds.has_value()) {
        return std::nullopt;
    }

    const std::int64_t days_since_gps_epoch = (day_point - gps_epoch).count();
    const std::int64_t civil_seconds = days_since_gps_epoch * seconds_per_day
        + static_cast<std::int64_t>(utc.hour) * 3'600
        + static_cast<std::int64_t>(utc.minute) * 60 + static_cast<std::int64_t>(utc.second);
    const std::int64_t gps_seconds = civil_seconds + *offset_seconds;
    const std::int64_t maximum_seconds =
        (std::numeric_limits<std::int64_t>::max() - utc.nanosecond) / nanoseconds_per_second;
    if (gps_seconds > maximum_seconds) {
        return std::nullopt;
    }

    return GpsTime{
        gps_seconds * nanoseconds_per_second + static_cast<std::int64_t>(utc.nanosecond)};
}

} // namespace plotcore
