#pragma once

#include <cstdint>
#include <optional>

#include "rtktrace/plot/data_view.hpp"

namespace rtktrace {

inline constexpr double minimum_position_axis_range_m = 0.001;
inline constexpr std::int64_t minimum_time_axis_range_ns = 1'000'000;
inline constexpr std::int64_t degenerate_time_axis_range_ns = 60'000'000'000;

struct NumericRange {
    double minimum;
    double maximum;

    [[nodiscard]] constexpr double length() const noexcept
    {
        return maximum - minimum;
    }
};

struct PlotAreaSize {
    double width_px;
    double height_px;
};

struct TrajectoryAxisRanges {
    NumericRange east;
    NumericRange north;
    double meters_per_pixel;
};

enum class PositionComponent : std::uint8_t {
    East,
    North,
    Up,
    EllipsoidalHeight,
    ReferenceRelativeDistance3d,
};

[[nodiscard]] std::optional<TrajectoryAxisRanges> auto_fit_trajectory(
    const PlotDataView& data, const QualityFilter& filter, PlotAreaSize area) noexcept;
[[nodiscard]] std::optional<NumericRange> auto_fit_position_component(
    const PlotDataView& data, const QualityFilter& filter, PositionComponent component) noexcept;
[[nodiscard]] std::optional<TimeRange> auto_fit_time_axis(
    const PlotDataView& data, const QualityFilter& filter) noexcept;

} // namespace rtktrace
