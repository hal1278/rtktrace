#include "plotcore/analysis/reference_matching.hpp"

#include <limits>

namespace plotcore {
namespace {

[[nodiscard]] bool valid_range(SampleRangeIndex range, std::size_t size) noexcept
{
    return range.begin <= range.end && range.end <= size;
}

[[nodiscard]] bool is_time_ordered(
    std::span<const NormalizedSample> samples, SampleRangeIndex range) noexcept
{
    for (std::size_t index = range.begin + (range.empty() ? 0 : 1); index < range.end; ++index) {
        if (samples[index].time < samples[index - 1].time) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::int64_t> nonnegative_difference(
    GpsTime later, GpsTime earlier) noexcept
{
    if (later < earlier) {
        return std::nullopt;
    }
    const std::int64_t later_value = later.nanoseconds_since_gps_epoch;
    const std::int64_t earlier_value = earlier.nanoseconds_since_gps_epoch;
    std::uint64_t difference = 0;
    if (earlier_value < 0 && later_value >= 0) {
        difference = static_cast<std::uint64_t>(later_value)
            + static_cast<std::uint64_t>(-(earlier_value + 1)) + 1;
    } else {
        difference = static_cast<std::uint64_t>(later_value - earlier_value);
    }
    if (difference > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(difference);
}

} // namespace

std::optional<std::vector<ReferenceMatch>> match_reference_epochs(
    std::span<const NormalizedSample> reference_samples, SampleRangeIndex reference_range,
    std::span<const NormalizedSample> comparison_samples, SampleRangeIndex comparison_range,
    ReferenceMatchConfiguration configuration)
{
    if (configuration.maximum_time_difference_ns < 0
        || !valid_range(reference_range, reference_samples.size())
        || !valid_range(comparison_range, comparison_samples.size())
        || !is_time_ordered(reference_samples, reference_range)
        || !is_time_ordered(comparison_samples, comparison_range)) {
        return std::nullopt;
    }

    std::vector<ReferenceMatch> matches;
    matches.reserve(comparison_range.size());
    std::size_t next_reference = reference_range.begin;
    std::optional<std::size_t> selected_reference;
    for (std::size_t comparison_index = comparison_range.begin;
        comparison_index < comparison_range.end; ++comparison_index) {
        const GpsTime comparison_time = comparison_samples[comparison_index].time;
        while (next_reference < reference_range.end
            && reference_samples[next_reference].time <= comparison_time) {
            selected_reference = next_reference;
            ++next_reference;
        }
        if (!selected_reference.has_value()) {
            continue;
        }
        const std::optional<std::int64_t> difference =
            nonnegative_difference(comparison_time, reference_samples[*selected_reference].time);
        if (!difference.has_value()) {
            return std::nullopt;
        }
        if (configuration.tolerance_check_enabled
            && *difference > configuration.maximum_time_difference_ns) {
            continue;
        }
        matches.push_back(ReferenceMatch{
            comparison_index,
            *selected_reference,
            *difference,
        });
    }
    return matches;
}

} // namespace plotcore
