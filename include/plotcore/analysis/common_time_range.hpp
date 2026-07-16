#pragma once

#include <compare>
#include <optional>

#include "plotcore/model/loaded_file.hpp"

namespace plotcore {

struct TimeRange {
    GpsTime start;
    GpsTime end;

    auto operator<=>(const TimeRange&) const = default;
};

struct CommonTimeRange {
    std::optional<GpsTime> entered_start;
    std::optional<GpsTime> entered_end;
    bool start_enabled{false};
    bool end_enabled{false};
};

[[nodiscard]] std::optional<TimeRange> file_time_range(const LoadedFile& file) noexcept;
[[nodiscard]] std::optional<TimeRange> union_time_range(const LoadedFiles& files) noexcept;
[[nodiscard]] std::optional<TimeRange> intersection_time_range(const LoadedFiles& files) noexcept;
[[nodiscard]] std::optional<TimeRange> effective_time_range(
    const CommonTimeRange& configured_range, TimeRange union_range) noexcept;
[[nodiscard]] bool apply_intersection(
    CommonTimeRange& configured_range, const LoadedFiles& files) noexcept;
[[nodiscard]] constexpr bool contains(TimeRange range, GpsTime time) noexcept
{
    return range.start <= time && time <= range.end;
}

} // namespace plotcore
