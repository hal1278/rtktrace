#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "rtktrace/analysis/sample_rate.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] rtktrace::NormalizedSample sample_at(std::int64_t time_ns)
{
    return rtktrace::NormalizedSample{
        .time = rtktrace::GpsTime{time_ns},
        .llh = {},
        .ecef = {},
        .enu = {},
        .quality = rtktrace::SolutionQuality::InvalidOrUnknown,
        .source_line_number = 1,
        .continuous_from_previous = false,
    };
}

[[nodiscard]] bool near(double actual, double expected, double tolerance) noexcept
{
    return std::abs(actual - expected) <= tolerance;
}

void test_missing_estimate()
{
    const std::vector<rtktrace::NormalizedSample> empty;
    check(!rtktrace::estimate_sample_rate_hz(empty).has_value(), "empty sample list has no rate");

    const std::vector single{sample_at(0)};
    check(!rtktrace::estimate_sample_rate_hz(single).has_value(), "single sample has no rate");

    const std::vector no_candidate{
        sample_at(10'000'000),
        sample_at(10'000'000),
        sample_at(5'000'000),
        sample_at(10'000'000),
    };
    check(!rtktrace::estimate_sample_rate_hz(no_candidate).has_value(),
        "duplicates, reversals, and the exact 5 ms threshold are not candidates");
}

void test_minimum_positive_interval()
{
    const std::vector samples{
        sample_at(0),
        sample_at(1'000'000'000),
        sample_at(1'200'000'000),
        sample_at(1'700'000'000),
    };
    const auto estimated_hz = rtktrace::estimate_sample_rate_hz(samples);
    check(estimated_hz.has_value(), "rate candidate produces an estimate");
    if (estimated_hz.has_value()) {
        check(near(*estimated_hz, 5.0, 1.0e-12), "minimum interval determines rate");
    }
}

void test_configurable_minimum_interval()
{
    const std::vector samples{
        sample_at(0),
        sample_at(200'000'000),
        sample_at(500'000'000),
    };
    const auto default_estimate = rtktrace::estimate_sample_rate_hz(samples);
    check(default_estimate.has_value() && near(*default_estimate, 5.0, 1.0e-12),
        "default threshold accepts 200 ms interval");

    const auto configured_estimate = rtktrace::estimate_sample_rate_hz(samples, 250'000'000);
    check(configured_estimate.has_value(), "configured threshold leaves a candidate");
    if (configured_estimate.has_value()) {
        check(near(*configured_estimate, 10.0 / 3.0, 1.0e-12),
            "configured threshold selects the 300 ms interval");
    }
}

} // namespace

int main()
{
    test_missing_estimate();
    test_minimum_positive_interval();
    test_configurable_minimum_interval();

    if (failures != 0) {
        std::cerr << failures << " sample-rate test(s) failed\n";
        return 1;
    }
    return 0;
}
