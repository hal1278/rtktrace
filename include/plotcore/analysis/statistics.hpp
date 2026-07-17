#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>

#include "plotcore/analysis/common_time_range.hpp"

namespace plotcore {

using QualityCounts = std::array<std::size_t, solution_quality_count>;

struct RecordedStatistics {
    QualityCounts quality_counts{};
    std::optional<GpsTime> first_sample_time;
    std::optional<GpsTime> last_sample_time;
};

[[nodiscard]] RecordedStatistics calculate_recorded_statistics(
    std::span<const NormalizedSample> samples, TimeRange range) noexcept;
[[nodiscard]] std::size_t recorded_sample_count(
    const RecordedStatistics& statistics) noexcept;
[[nodiscard]] std::optional<std::size_t> calculate_expected_sample_count(
    TimeRange range, std::optional<double> effective_hz) noexcept;
[[nodiscard]] std::optional<double> quality_percentage(
    std::size_t quality_count, std::size_t denominator) noexcept;

} // namespace plotcore
