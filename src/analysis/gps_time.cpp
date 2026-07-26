#include "rtktrace/analysis/gps_time.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <limits>
#include <string>

namespace rtktrace {
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

constexpr std::chrono::year_month_day gps_epoch_date = 1980y / std::chrono::January / 6;
constexpr std::int64_t seconds_per_day = 86'400;
constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
constexpr std::int64_t nanoseconds_per_millisecond = 1'000'000;

[[nodiscard]] bool decimal_at(std::string_view text, std::size_t offset, std::size_t length,
    unsigned& value) noexcept
{
    value = 0;
    const char* first = text.data() + offset;
    const char* last = first + length;
    const std::from_chars_result result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

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
    const std::size_t retained_digits = fractional_digits.size() < 9 ? fractional_digits.size() : 9;
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
        + static_cast<std::int64_t>(utc.hour) * 3'600 + static_cast<std::int64_t>(utc.minute) * 60
        + static_cast<std::int64_t>(utc.second);
    const std::int64_t gps_seconds = civil_seconds + *offset_seconds;
    const std::int64_t maximum_seconds =
        (std::numeric_limits<std::int64_t>::max() - utc.nanosecond) / nanoseconds_per_second;
    if (gps_seconds > maximum_seconds) {
        return std::nullopt;
    }

    return GpsTime{
        gps_seconds * nanoseconds_per_second + static_cast<std::int64_t>(utc.nanosecond)};
}

std::optional<GpsTime> gps_civil_to_gps_time(GpsCivilTime gps) noexcept
{
    const std::chrono::year_month_day date{
        std::chrono::year{gps.year}, std::chrono::month{gps.month}, std::chrono::day{gps.day}};
    if (!date.ok() || gps.hour >= 24 || gps.minute >= 60 || gps.second >= 60
        || gps.nanosecond >= nanoseconds_per_second) {
        return std::nullopt;
    }

    const std::chrono::sys_days day_point{date};
    const std::chrono::sys_days gps_epoch{gps_epoch_date};
    if (day_point < gps_epoch) {
        return std::nullopt;
    }

    const std::int64_t days_since_gps_epoch = (day_point - gps_epoch).count();
    const std::int64_t gps_seconds = days_since_gps_epoch * seconds_per_day
        + static_cast<std::int64_t>(gps.hour) * 3'600 + static_cast<std::int64_t>(gps.minute) * 60
        + static_cast<std::int64_t>(gps.second);
    const std::int64_t maximum_seconds =
        (std::numeric_limits<std::int64_t>::max() - gps.nanosecond) / nanoseconds_per_second;
    if (gps_seconds > maximum_seconds) {
        return std::nullopt;
    }
    return GpsTime{
        gps_seconds * nanoseconds_per_second + static_cast<std::int64_t>(gps.nanosecond)};
}

std::optional<std::string> format_absolute_gps_time(GpsTime gps)
{
    std::int64_t seconds = gps.nanoseconds_since_gps_epoch / nanoseconds_per_second;
    std::int64_t nanosecond = gps.nanoseconds_since_gps_epoch % nanoseconds_per_second;
    if (nanosecond < 0) {
        nanosecond += nanoseconds_per_second;
        --seconds;
    }
    std::int64_t millisecond =
        (nanosecond + nanoseconds_per_millisecond / 2) / nanoseconds_per_millisecond;
    if (millisecond == 1'000) {
        millisecond = 0;
        ++seconds;
    }

    std::int64_t days_since_gps_epoch = seconds / seconds_per_day;
    std::int64_t seconds_of_day = seconds % seconds_per_day;
    if (seconds_of_day < 0) {
        seconds_of_day += seconds_per_day;
        --days_since_gps_epoch;
    }
    const std::chrono::year_month_day date{
        std::chrono::sys_days{gps_epoch_date} + std::chrono::days{days_since_gps_epoch}};
    const int year = static_cast<int>(date.year());
    if (!date.ok() || year < 0 || year > 9'999) {
        return std::nullopt;
    }
    const unsigned hour = static_cast<unsigned>(seconds_of_day / 3'600);
    const unsigned minute = static_cast<unsigned>((seconds_of_day % 3'600) / 60);
    const unsigned second = static_cast<unsigned>(seconds_of_day % 60);
    std::array<char, 24> text{};
    const int written = std::snprintf(text.data(), text.size(),
        "%04d-%02u-%02u %02u:%02u:%02u.%03lld", year, static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()), hour, minute, second,
        static_cast<long long>(millisecond));
    if (written != 23) {
        return std::nullopt;
    }
    return std::string{text.data(), static_cast<std::size_t>(written)};
}

std::optional<GpsTime> parse_absolute_gps_time(std::string_view text) noexcept
{
    if (text.size() != 23 || text[4] != '-' || text[7] != '-' || text[10] != ' '
        || text[13] != ':' || text[16] != ':' || text[19] != '.') {
        return std::nullopt;
    }
    unsigned year = 0;
    unsigned month = 0;
    unsigned day = 0;
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;
    unsigned millisecond = 0;
    if (!decimal_at(text, 0, 4, year) || !decimal_at(text, 5, 2, month)
        || !decimal_at(text, 8, 2, day) || !decimal_at(text, 11, 2, hour)
        || !decimal_at(text, 14, 2, minute) || !decimal_at(text, 17, 2, second)
        || !decimal_at(text, 20, 3, millisecond)) {
        return std::nullopt;
    }
    return gps_civil_to_gps_time(GpsCivilTime{static_cast<int>(year), month, day, hour, minute,
        second, millisecond * static_cast<std::uint32_t>(nanoseconds_per_millisecond)});
}

} // namespace rtktrace
