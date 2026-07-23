#include "rtktrace/analysis/statistics.hpp"

#include <cmath>
#include <limits>
#include <numeric>

namespace rtktrace {

RecordedStatistics calculate_recorded_statistics(
    std::span<const NormalizedSample> samples, TimeRange range) noexcept
{
    RecordedStatistics result;
    for (const NormalizedSample& sample : samples) {
        if (!contains(range, sample.time)) {
            continue;
        }

        const std::size_t quality_index = static_cast<std::size_t>(sample.quality);
        if (quality_index < result.quality_counts.size()) {
            ++result.quality_counts[quality_index];
        }
        if (!result.first_sample_time.has_value()) {
            result.first_sample_time = sample.time;
        }
        result.last_sample_time = sample.time;
    }
    return result;
}

std::size_t recorded_sample_count(const RecordedStatistics& statistics) noexcept
{
    return std::accumulate(
        statistics.quality_counts.begin(), statistics.quality_counts.end(), std::size_t{0});
}

std::optional<std::size_t> calculate_expected_sample_count(
    TimeRange range, std::optional<double> effective_hz) noexcept
{
    if (range.end < range.start || !effective_hz.has_value() || !std::isfinite(*effective_hz)
        || *effective_hz <= 0.0) {
        return std::nullopt;
    }

    constexpr long double nanoseconds_per_second = 1'000'000'000.0L;
    const long double duration_nanoseconds =
        static_cast<long double>(range.end.nanoseconds_since_gps_epoch)
        - static_cast<long double>(range.start.nanoseconds_since_gps_epoch);
    const long double rounded_intervals = std::floor(
        duration_nanoseconds / nanoseconds_per_second * static_cast<long double>(*effective_hz)
        + 0.5L);
    const long double maximum_intervals =
        static_cast<long double>(std::numeric_limits<std::size_t>::max() - 1);
    if (!std::isfinite(rounded_intervals) || rounded_intervals > maximum_intervals) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(rounded_intervals) + 1;
}

std::optional<double> quality_percentage(
    std::size_t quality_count, std::size_t denominator) noexcept
{
    if (denominator == 0) {
        return std::nullopt;
    }
    return static_cast<double>(quality_count) / static_cast<double>(denominator) * 100.0;
}

} // namespace rtktrace
