#include "plotcore/plot/axis.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace plotcore {
namespace {

struct Bounds {
    double minimum;
    double maximum;
};

template <typename Value>
void for_each_visible_sample(const PlotDataView& data, const QualityFilter& filter, Value&& value)
{
    for (const PlotSeriesView& series : data.series) {
        if (!series.file_visible) {
            continue;
        }
        for (std::size_t index = 0; index < plot_series_size(series); ++index) {
            const std::optional<PlotSampleValue> sample = plot_sample_at(series, index);
            if (sample.has_value() && quality_is_visible(filter, sample->quality)) {
                value(*sample);
            }
        }
    }
}

void include_value(std::optional<Bounds>& bounds, double value) noexcept
{
    if (!std::isfinite(value)) {
        return;
    }
    if (!bounds.has_value()) {
        bounds = Bounds{value, value};
        return;
    }
    bounds->minimum = std::min(bounds->minimum, value);
    bounds->maximum = std::max(bounds->maximum, value);
}

[[nodiscard]] std::optional<double> component_value(
    const PlotSampleValue& sample, PositionComponent component) noexcept
{
    switch (component) {
    case PositionComponent::East:
        return sample.east_m;
    case PositionComponent::North:
        return sample.north_m;
    case PositionComponent::Up:
        return sample.up_m;
    case PositionComponent::EllipsoidalHeight:
        return sample.ellipsoidal_height_m;
    case PositionComponent::ReferenceRelativeDistance3d:
        return sample.reference_relative_distance_3d_m;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<NumericRange> centered_numeric_range(
    double minimum, double maximum, double target_length) noexcept
{
    const long double center =
        static_cast<long double>(minimum) + (static_cast<long double>(maximum) - minimum) / 2.0L;
    const long double half = static_cast<long double>(target_length) / 2.0L;
    const NumericRange result{
        static_cast<double>(center - half),
        static_cast<double>(center + half),
    };
    if (!std::isfinite(result.minimum) || !std::isfinite(result.maximum)
        || result.minimum >= result.maximum) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] TimeRange centered_time_range(
    GpsTime minimum, GpsTime maximum, std::int64_t target_length_ns) noexcept
{
    const long double center = static_cast<long double>(minimum.nanoseconds_since_gps_epoch)
        + (static_cast<long double>(maximum.nanoseconds_since_gps_epoch)
              - minimum.nanoseconds_since_gps_epoch)
            / 2.0L;
    const long double int64_min =
        static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    const long double int64_max =
        static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    long double start = center - static_cast<long double>(target_length_ns) / 2.0L;
    start = std::max(start, int64_min);
    start = std::min(start, int64_max - target_length_ns);
    const std::int64_t start_ns = static_cast<std::int64_t>(start);
    return TimeRange{GpsTime{start_ns}, GpsTime{start_ns + target_length_ns}};
}

} // namespace

std::optional<TrajectoryAxisRanges> auto_fit_trajectory(
    const PlotDataView& data, const QualityFilter& filter, PlotAreaSize area) noexcept
{
    if (!std::isfinite(area.width_px) || !std::isfinite(area.height_px) || area.width_px <= 0.0
        || area.height_px <= 0.0) {
        return std::nullopt;
    }
    std::optional<Bounds> east;
    std::optional<Bounds> north;
    for_each_visible_sample(data, filter, [&](const PlotSampleValue& sample) {
        if (!std::isfinite(sample.east_m) || !std::isfinite(sample.north_m)) {
            return;
        }
        include_value(east, sample.east_m);
        include_value(north, sample.north_m);
    });
    if (!east.has_value() || !north.has_value()) {
        return std::nullopt;
    }

    const double east_data_range = east->maximum - east->minimum;
    const double north_data_range = north->maximum - north->minimum;
    const double shorter_pixels = std::min(area.width_px, area.height_px);
    double meters_per_pixel = 0.0;
    if (east_data_range == 0.0 && north_data_range == 0.0) {
        meters_per_pixel = 1.0 / shorter_pixels;
    } else {
        meters_per_pixel = std::max(
            east_data_range / (0.9 * area.width_px), north_data_range / (0.9 * area.height_px));
        meters_per_pixel =
            std::max(meters_per_pixel, minimum_position_axis_range_m / shorter_pixels);
    }
    if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0) {
        return std::nullopt;
    }
    const std::optional<NumericRange> east_range =
        centered_numeric_range(east->minimum, east->maximum, area.width_px * meters_per_pixel);
    const std::optional<NumericRange> north_range =
        centered_numeric_range(north->minimum, north->maximum, area.height_px * meters_per_pixel);
    if (!east_range.has_value() || !north_range.has_value()) {
        return std::nullopt;
    }
    return TrajectoryAxisRanges{
        *east_range,
        *north_range,
        meters_per_pixel,
    };
}

std::optional<NumericRange> auto_fit_position_component(
    const PlotDataView& data, const QualityFilter& filter, PositionComponent component) noexcept
{
    std::optional<Bounds> bounds;
    for_each_visible_sample(data, filter, [&](const PlotSampleValue& sample) {
        const std::optional<double> value = component_value(sample, component);
        if (value.has_value()) {
            include_value(bounds, *value);
        }
    });
    if (!bounds.has_value()) {
        return std::nullopt;
    }
    const double length = bounds->maximum - bounds->minimum;
    if (length >= minimum_position_axis_range_m) {
        return NumericRange{bounds->minimum, bounds->maximum};
    }
    const double target_length = length == 0.0 ? 1.0 : minimum_position_axis_range_m;
    return centered_numeric_range(bounds->minimum, bounds->maximum, target_length);
}

std::optional<TimeRange> auto_fit_time_axis(
    const PlotDataView& data, const QualityFilter& filter) noexcept
{
    std::optional<GpsTime> minimum;
    std::optional<GpsTime> maximum;
    for_each_visible_sample(data, filter, [&](const PlotSampleValue& sample) {
        minimum = minimum.has_value() ? std::min(*minimum, sample.time) : sample.time;
        maximum = maximum.has_value() ? std::max(*maximum, sample.time) : sample.time;
    });
    if (!minimum.has_value() || !maximum.has_value()) {
        return std::nullopt;
    }
    const long double length = static_cast<long double>(maximum->nanoseconds_since_gps_epoch)
        - minimum->nanoseconds_since_gps_epoch;
    if (length == 0.0L) {
        return centered_time_range(*minimum, *maximum, degenerate_time_axis_range_ns);
    }
    if (length < minimum_time_axis_range_ns) {
        return centered_time_range(*minimum, *maximum, minimum_time_axis_range_ns);
    }
    return TimeRange{*minimum, *maximum};
}

} // namespace plotcore
