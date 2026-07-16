#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#include "plotcore/analysis/statistics.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] plotcore::NormalizedSample sample_at(
    std::int64_t time_ns, plotcore::SolutionQuality quality)
{
    return plotcore::NormalizedSample{
        .time = plotcore::GpsTime{time_ns},
        .llh = {},
        .ecef = {},
        .enu = {},
        .quality = quality,
        .source_line_number = 1,
        .continuous_from_previous = false,
    };
}

[[nodiscard]] bool near(double actual, double expected) noexcept
{
    return std::abs(actual - expected) <= 1.0e-12;
}

void test_recorded_statistics()
{
    using namespace plotcore;

    const std::vector samples{
        sample_at(0, SolutionQuality::Fixed),
        sample_at(10, SolutionQuality::Fixed),
        sample_at(20, SolutionQuality::Float),
        sample_at(30, SolutionQuality::Ppp),
        sample_at(40, SolutionQuality::Single),
    };
    const RecordedStatistics statistics = calculate_recorded_statistics(
        samples, TimeRange{GpsTime{10}, GpsTime{30}});

    check(statistics.quality_counts[static_cast<std::size_t>(SolutionQuality::Fixed)] == 1,
        "fixed samples are counted by quality");
    check(statistics.quality_counts[static_cast<std::size_t>(SolutionQuality::Float)] == 1,
        "float samples are counted by quality");
    check(statistics.quality_counts[static_cast<std::size_t>(SolutionQuality::Ppp)] == 1,
        "PPP samples are counted by quality");
    check(recorded_sample_count(statistics) == 3, "recorded denominator sums quality counts");
    check(statistics.first_sample_time == GpsTime{10}, "range start is inclusive");
    check(statistics.last_sample_time == GpsTime{30}, "range end is inclusive");

    const RecordedStatistics empty = calculate_recorded_statistics(
        samples, TimeRange{GpsTime{50}, GpsTime{60}});
    check(recorded_sample_count(empty) == 0, "range without samples has zero recorded count");
    check(!empty.first_sample_time.has_value() && !empty.last_sample_time.has_value(),
        "range without samples has no displayed file times");
}

void test_expected_count()
{
    using namespace plotcore;

    check(calculate_expected_sample_count(
              TimeRange{GpsTime{0}, GpsTime{3'000'000'000}}, 1.0)
            == 4,
        "expected count includes both endpoints");
    check(calculate_expected_sample_count(
              TimeRange{GpsTime{0}, GpsTime{1'000'000'000}}, 2.5)
            == 4,
        "expected count rounds non-negative half upward before adding endpoint");
    check(calculate_expected_sample_count(TimeRange{GpsTime{10}, GpsTime{10}}, 20.0) == 1,
        "zero-duration range has one expected sample");
    check(!calculate_expected_sample_count(TimeRange{GpsTime{20}, GpsTime{10}}, 1.0)
               .has_value(),
        "reversed range is invalid");
    check(!calculate_expected_sample_count(TimeRange{GpsTime{0}, GpsTime{10}}, std::nullopt)
               .has_value(),
        "missing effective Hz makes expected count unavailable");
    check(!calculate_expected_sample_count(TimeRange{GpsTime{0}, GpsTime{10}}, 0.0)
               .has_value(),
        "non-positive effective Hz is invalid");
    check(!calculate_expected_sample_count(
               TimeRange{GpsTime{0}, GpsTime{10}}, std::numeric_limits<double>::infinity())
               .has_value(),
        "non-finite effective Hz is invalid");
}

void test_percentages()
{
    const std::optional<double> recorded = plotcore::quality_percentage(1, 4);
    check(recorded.has_value() && near(*recorded, 25.0),
        "quality percentage uses the supplied denominator");

    const std::optional<double> over_expected = plotcore::quality_percentage(5, 4);
    check(over_expected.has_value() && near(*over_expected, 125.0),
        "quality percentage is not clamped to 100 percent");
    check(!plotcore::quality_percentage(0, 0).has_value(),
        "zero denominator has no percentage");
}

} // namespace

int main()
{
    test_recorded_statistics();
    test_expected_count();
    test_percentages();

    if (failures != 0) {
        std::cerr << failures << " statistics test(s) failed\n";
        return 1;
    }
    return 0;
}
