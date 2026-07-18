#include "plotcore/analysis/common_time_range.hpp"

#include <algorithm>

namespace plotcore {

std::optional<TimeRange> file_time_range(const LoadedFile& file) noexcept
{
    if (file.samples.empty()) {
        return std::nullopt;
    }
    return TimeRange{file.samples.front().time, file.samples.back().time};
}

std::optional<TimeRange> union_time_range(const LoadedFiles& files) noexcept
{
    std::optional<TimeRange> result;
    for (const LoadedFile& file : files) {
        const std::optional<TimeRange> file_range = file_time_range(file);
        if (!file_range.has_value()) {
            continue;
        }
        if (!result.has_value()) {
            result = file_range;
            continue;
        }
        result->start = std::min(result->start, file_range->start);
        result->end = std::max(result->end, file_range->end);
    }
    return result;
}

std::optional<TimeRange> intersection_time_range(const LoadedFiles& files) noexcept
{
    std::optional<TimeRange> result;
    for (const LoadedFile& file : files) {
        const std::optional<TimeRange> file_range = file_time_range(file);
        if (!file_range.has_value()) {
            continue;
        }
        if (!result.has_value()) {
            result = file_range;
            continue;
        }
        result->start = std::max(result->start, file_range->start);
        result->end = std::min(result->end, file_range->end);
    }
    if (result.has_value() && result->start > result->end) {
        return std::nullopt;
    }
    return result;
}

std::optional<TimeRange> effective_time_range(
    const CommonTimeRange& configured_range, TimeRange union_range) noexcept
{
    if ((configured_range.start_enabled && !configured_range.entered_start.has_value())
        || (configured_range.end_enabled && !configured_range.entered_end.has_value())) {
        return std::nullopt;
    }

    const GpsTime effective_start =
        configured_range.start_enabled ? *configured_range.entered_start : union_range.start;
    const GpsTime effective_end =
        configured_range.end_enabled ? *configured_range.entered_end : union_range.end;
    if (effective_start > effective_end) {
        return std::nullopt;
    }
    return TimeRange{effective_start, effective_end};
}

bool apply_intersection(CommonTimeRange& configured_range, const LoadedFiles& files) noexcept
{
    const std::optional<TimeRange> intersection = intersection_time_range(files);
    if (!intersection.has_value()) {
        return false;
    }
    configured_range.entered_start = intersection->start;
    configured_range.entered_end = intersection->end;
    configured_range.start_enabled = true;
    configured_range.end_enabled = true;
    return true;
}

} // namespace plotcore
