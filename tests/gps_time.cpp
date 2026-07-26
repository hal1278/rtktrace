#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "rtktrace/analysis/gps_time.hpp"

namespace {

constexpr std::int64_t second_ns = 1'000'000'000;
int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void test_fraction_rounding()
{
    using rtktrace::round_fractional_seconds_to_nanoseconds;

    check(round_fractional_seconds_to_nanoseconds("") == 0, "omitted fraction is zero");
    check(round_fractional_seconds_to_nanoseconds("1") == 100'000'000,
        "short fraction is scaled to nanoseconds");
    check(round_fractional_seconds_to_nanoseconds("123456789") == 123'456'789,
        "nine fraction digits are exact");
    check(round_fractional_seconds_to_nanoseconds("1234567894") == 123'456'789,
        "sub-nanosecond value below half rounds down");
    check(round_fractional_seconds_to_nanoseconds("1234567895") == 123'456'790,
        "sub-nanosecond half rounds up");
    check(round_fractional_seconds_to_nanoseconds("9999999995") == second_ns,
        "fraction rounding reports whole-second carry");
    check(!round_fractional_seconds_to_nanoseconds("12x").has_value(),
        "non-digit fraction is rejected");
}

void test_gps_epoch_and_current_offset()
{
    using rtktrace::utc_civil_to_gps_time;
    using rtktrace::UtcCivilTime;

    const auto epoch = utc_civil_to_gps_time(UtcCivilTime{1980, 1, 6, 0, 0, 0, 0});
    check(epoch == rtktrace::GpsTime{0}, "GPS epoch UTC maps to zero");

    const auto current = utc_civil_to_gps_time(UtcCivilTime{2017, 1, 1, 0, 0, 0, 250});
    check(current.has_value(), "post-2017 UTC converts to GPST");
    if (current.has_value()) {
        using namespace std::chrono_literals;
        const std::chrono::sys_days date = 2017y / std::chrono::January / 1;
        const std::chrono::sys_days gps_epoch = 1980y / std::chrono::January / 6;
        const std::int64_t utc_elapsed_seconds = (date - gps_epoch).count() * 86'400;
        check(current->nanoseconds_since_gps_epoch == (utc_elapsed_seconds + 18) * second_ns + 250,
            "post-2017 conversion uses 18 second GPS-UTC offset");
    }
}

void test_leap_second_continuity()
{
    using rtktrace::utc_civil_to_gps_time;
    using rtktrace::UtcCivilTime;

    const auto before = utc_civil_to_gps_time(UtcCivilTime{2016, 12, 31, 23, 59, 59, 0});
    const auto leap = utc_civil_to_gps_time(UtcCivilTime{2016, 12, 31, 23, 59, 60, 0});
    const auto after = utc_civil_to_gps_time(UtcCivilTime{2017, 1, 1, 0, 0, 0, 0});
    check(before.has_value() && leap.has_value() && after.has_value(),
        "2016 leap-second sequence converts");
    if (before.has_value() && leap.has_value() && after.has_value()) {
        check(*leap - *before == second_ns, "leap second follows 23:59:59 by one second");
        check(*after - *leap == second_ns, "midnight follows leap second by one second");
    }

    constexpr std::array leap_days{
        UtcCivilTime{1981, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{1982, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{1983, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{1985, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{1987, 12, 31, 23, 59, 60, 0},
        UtcCivilTime{1989, 12, 31, 23, 59, 60, 0},
        UtcCivilTime{1990, 12, 31, 23, 59, 60, 0},
        UtcCivilTime{1992, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{1993, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{1994, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{1995, 12, 31, 23, 59, 60, 0},
        UtcCivilTime{1997, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{1998, 12, 31, 23, 59, 60, 0},
        UtcCivilTime{2005, 12, 31, 23, 59, 60, 0},
        UtcCivilTime{2008, 12, 31, 23, 59, 60, 0},
        UtcCivilTime{2012, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{2015, 6, 30, 23, 59, 60, 0},
        UtcCivilTime{2016, 12, 31, 23, 59, 60, 0},
    };
    for (const UtcCivilTime leap_day : leap_days) {
        const auto leap_time = utc_civil_to_gps_time(leap_day);
        const std::chrono::year_month_day next_date{
            std::chrono::sys_days{std::chrono::year{leap_day.year}
                / std::chrono::month{leap_day.month} / std::chrono::day{leap_day.day}}
            + std::chrono::days{1}};
        const auto next_time = utc_civil_to_gps_time(UtcCivilTime{
            static_cast<int>(next_date.year()),
            static_cast<unsigned>(next_date.month()),
            static_cast<unsigned>(next_date.day()),
            0,
            0,
            0,
            0,
        });
        check(leap_time.has_value() && next_time.has_value(), "listed leap second converts");
        if (leap_time.has_value() && next_time.has_value()) {
            check(*next_time - *leap_time == second_ns, "listed leap transition is continuous");
        }
    }
}

void test_invalid_utc()
{
    using rtktrace::utc_civil_to_gps_time;
    using rtktrace::UtcCivilTime;

    check(!utc_civil_to_gps_time(UtcCivilTime{1979, 12, 31, 23, 59, 59, 0}).has_value(),
        "UTC before GPS epoch is rejected");
    check(!utc_civil_to_gps_time(UtcCivilTime{2023, 2, 29, 0, 0, 0, 0}).has_value(),
        "invalid civil date is rejected");
    check(!utc_civil_to_gps_time(UtcCivilTime{2016, 12, 30, 23, 59, 60, 0}).has_value(),
        "second 60 on a non-leap day is rejected");
    check(!utc_civil_to_gps_time(UtcCivilTime{2016, 12, 31, 22, 59, 60, 0}).has_value(),
        "second 60 outside 23:59 is rejected");
    check(!utc_civil_to_gps_time(UtcCivilTime{2026, 12, 31, 23, 59, 60, 0}).has_value(),
        "unannounced 2026 leap second is rejected");
    check(!utc_civil_to_gps_time(UtcCivilTime{2026, 1, 1, 0, 0, 0, 1'000'000'000}).has_value(),
        "nanosecond outside one second is rejected");
}

void test_gps_civil_time()
{
    using rtktrace::gps_civil_to_gps_time;
    using rtktrace::GpsCivilTime;

    check(gps_civil_to_gps_time(GpsCivilTime{1980, 1, 6, 0, 0, 0, 0}) == rtktrace::GpsTime{0},
        "GPS civil epoch maps to zero without a UTC offset");
    check(gps_civil_to_gps_time(GpsCivilTime{1980, 1, 7, 0, 0, 0, 25})
            == rtktrace::GpsTime{86'400 * second_ns + 25},
        "GPS civil time is interpreted directly as GPST");
    check(!gps_civil_to_gps_time(GpsCivilTime{2023, 2, 29, 0, 0, 0, 0}).has_value(),
        "invalid GPS civil date is rejected");
    check(!gps_civil_to_gps_time(GpsCivilTime{2016, 12, 31, 23, 59, 60, 0}).has_value(),
        "GPST civil time does not contain UTC leap seconds");
}

void test_absolute_gps_time_format()
{
    using rtktrace::format_absolute_gps_time;
    using rtktrace::gps_civil_to_gps_time;
    using rtktrace::GpsCivilTime;

    check(format_absolute_gps_time(rtktrace::GpsTime{0})
            == std::optional<std::string>{"1980-01-06 00:00:00.000"},
        "absolute GPST format represents the GPS epoch without an offset suffix");

    const auto civil = gps_civil_to_gps_time(GpsCivilTime{2017, 1, 1, 0, 0, 0, 0});
    check(civil.has_value()
            && format_absolute_gps_time(*civil)
                == std::optional<std::string>{"2017-01-01 00:00:00.000"},
        "absolute GPST format uses the GPST calendar directly");

    const auto utc = rtktrace::utc_civil_to_gps_time(
        rtktrace::UtcCivilTime{2017, 1, 1, 0, 0, 0, 0});
    check(utc.has_value()
            && format_absolute_gps_time(*utc)
                == std::optional<std::string>{"2017-01-01 00:00:18.000"},
        "absolute GPST format does not subtract the GPS-UTC leap-second offset");

    const auto before_midnight =
        gps_civil_to_gps_time(GpsCivilTime{2024, 2, 29, 23, 59, 59, 999'499'999});
    const auto carry_midnight =
        gps_civil_to_gps_time(GpsCivilTime{2024, 2, 29, 23, 59, 59, 999'500'000});
    check(before_midnight.has_value()
            && format_absolute_gps_time(*before_midnight)
                == std::optional<std::string>{"2024-02-29 23:59:59.999"},
        "absolute GPST format rounds below half a millisecond down");
    check(carry_midnight.has_value()
            && format_absolute_gps_time(*carry_midnight)
                == std::optional<std::string>{"2024-03-01 00:00:00.000"},
        "absolute GPST format carries nearest-millisecond rounding across a day");
}

void test_absolute_gps_time_parse()
{
    using rtktrace::gps_civil_to_gps_time;
    using rtktrace::GpsCivilTime;
    using rtktrace::parse_absolute_gps_time;

    const auto expected =
        gps_civil_to_gps_time(GpsCivilTime{2024, 2, 29, 12, 34, 56, 789'000'000});
    check(parse_absolute_gps_time("2024-02-29 12:34:56.789") == expected,
        "absolute GPST parser accepts the exact millisecond grammar");
    check(!parse_absolute_gps_time("2023-02-29 12:34:56.789").has_value(),
        "absolute GPST parser rejects an invalid civil date");
    check(!parse_absolute_gps_time("2024-02-29T12:34:56.789").has_value()
            && !parse_absolute_gps_time("2024-02-29 12:34:56.78").has_value(),
        "absolute GPST parser rejects alternate separators and fractional precision");
    check(!parse_absolute_gps_time("2024-02-29 12:34:56.789Z").has_value()
            && !parse_absolute_gps_time("2024-02-29 12:34:56.789+00:00").has_value(),
        "absolute GPST parser rejects UTC and numeric offset suffixes");
    check(!parse_absolute_gps_time("2024-02-29 12:34:60.000").has_value(),
        "absolute GPST parser rejects UTC leap-second notation");
    check(!parse_absolute_gps_time("1979-12-31 23:59:59.999").has_value(),
        "absolute GPST parser rejects civil values before the GPS epoch");
}

} // namespace

int main()
{
    test_fraction_rounding();
    test_gps_epoch_and_current_offset();
    test_leap_second_continuity();
    test_invalid_utc();
    test_gps_civil_time();
    test_absolute_gps_time_format();
    test_absolute_gps_time_parse();

    if (failures != 0) {
        std::cerr << failures << " GPS-time test(s) failed\n";
        return 1;
    }
    return 0;
}
