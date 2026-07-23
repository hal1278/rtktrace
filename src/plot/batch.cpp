#include "rtktrace/plot/batch.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace rtktrace {
namespace {

constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
constexpr std::int64_t nanoseconds_per_gps_week = 7 * 24 * 60 * 60 * nanoseconds_per_second;

[[nodiscard]] bool includes_lines(DrawMode mode) noexcept
{
    return mode == DrawMode::Line || mode == DrawMode::LineAndPoint;
}

[[nodiscard]] bool includes_points(DrawMode mode) noexcept
{
    return mode == DrawMode::Point || mode == DrawMode::LineAndPoint;
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

[[nodiscard]] std::optional<GpsTime> first_visible_time(
    const PlotDataView& data, const QualityFilter& filter) noexcept
{
    std::optional<GpsTime> earliest;
    for (const PlotSeriesView& series : data.series) {
        if (!series.file_visible) {
            continue;
        }
        for (std::size_t index = 0; index < plot_series_size(series); ++index) {
            const std::optional<PlotSampleValue> sample = plot_sample_at(series, index);
            if (sample.has_value() && quality_is_visible(filter, sample->quality)
                && (!earliest.has_value() || sample->time < *earliest)) {
                earliest = sample->time;
            }
        }
    }
    return earliest;
}

[[nodiscard]] GpsTime gps_week_boundary(GpsTime time) noexcept
{
    std::int64_t week = time.nanoseconds_since_gps_epoch / nanoseconds_per_gps_week;
    if (time.nanoseconds_since_gps_epoch < 0
        && time.nanoseconds_since_gps_epoch % nanoseconds_per_gps_week != 0) {
        --week;
    }
    return GpsTime{week * nanoseconds_per_gps_week};
}

using ProjectSample = std::function<std::optional<PlotPoint>(const PlotSampleValue&)>;

[[nodiscard]] PlotSlotBatch build_slot_batch(const PlotSeriesView& series,
    const QualityFilter& filter, const PlotBatchOptions& options, const ProjectSample& project,
    std::size_t& visible_sample_count, std::optional<PlotBounds>& bounds)
{
    PlotSlotBatch result{series.slot_number, {}, {}};
    const auto qualities = quality_order_back_to_front(options.quality_order);
    if (includes_points(options.draw_mode)) {
        result.marker_batches.reserve(qualities.size());
        for (const SolutionQuality quality : qualities) {
            result.marker_batches.push_back(PlotMarkerBatch{quality, {}});
        }
    }

    PlotLineStrip current_line;
    const auto finish_line = [&result, &current_line]() {
        if (current_line.points.size() >= 2) {
            result.line_strips.push_back(std::move(current_line));
            current_line = PlotLineStrip{};
        } else {
            current_line.points.clear();
        }
    };

    for (std::size_t index = 0; index < plot_series_size(series); ++index) {
        const std::optional<PlotSampleValue> sample = plot_sample_at(series, index);
        const bool visible = sample.has_value() && quality_is_visible(filter, sample->quality);
        const std::optional<PlotPoint> point = visible ? project(*sample) : std::nullopt;
        if (!point.has_value()) {
            if (includes_lines(options.draw_mode) && !options.bridge_hidden_quality_samples) {
                finish_line();
            }
            continue;
        }

        ++visible_sample_count;
        if (!bounds.has_value()) {
            bounds = PlotBounds{point->x, point->x, point->y, point->y};
        } else {
            bounds->minimum_x = std::min(bounds->minimum_x, point->x);
            bounds->maximum_x = std::max(bounds->maximum_x, point->x);
            bounds->minimum_y = std::min(bounds->minimum_y, point->y);
            bounds->maximum_y = std::max(bounds->maximum_y, point->y);
        }
        if (includes_lines(options.draw_mode)) {
            current_line.points.push_back(*point);
        }
        if (includes_points(options.draw_mode)) {
            const auto marker = std::find_if(result.marker_batches.begin(),
                result.marker_batches.end(), [sample](const PlotMarkerBatch& batch) {
                    return batch.quality == sample->quality;
                });
            if (marker != result.marker_batches.end()) {
                marker->points.push_back(*point);
            }
        }
    }
    if (includes_lines(options.draw_mode)) {
        finish_line();
    }
    result.marker_batches.erase(
        std::remove_if(result.marker_batches.begin(), result.marker_batches.end(),
            [](const PlotMarkerBatch& batch) { return batch.points.empty(); }),
        result.marker_batches.end());
    return result;
}

[[nodiscard]] PlotBatch build_plot_batch(const PlotDataView& data, const QualityFilter& filter,
    const PlotBatchOptions& options, PlotProjection projection,
    std::optional<PositionComponent> component, std::optional<GpsTime> time_origin,
    const ProjectSample& project)
{
    PlotBatch result{projection, component, time_origin, {}, 0, std::nullopt};
    result.slots.reserve(data.series.size());

    std::vector<std::reference_wrapper<const PlotSeriesView>> series;
    series.reserve(data.series.size());
    for (const PlotSeriesView& item : data.series) {
        if (item.file_visible) {
            series.push_back(std::cref(item));
        }
    }
    std::sort(series.begin(), series.end(), [&options](const auto lhs, const auto rhs) {
        if (options.slot_order == SlotDrawingOrder::LargerSlotInFront) {
            return lhs.get().slot_number < rhs.get().slot_number;
        }
        return lhs.get().slot_number > rhs.get().slot_number;
    });
    for (const auto item : series) {
        PlotSlotBatch batch = build_slot_batch(
            item.get(), filter, options, project, result.visible_sample_count, result.bounds);
        if (!batch.line_strips.empty() || !batch.marker_batches.empty()) {
            result.slots.push_back(std::move(batch));
        }
    }
    return result;
}

} // namespace

std::array<SolutionQuality, solution_quality_count> quality_order_back_to_front(
    QualityDrawingOrder order) noexcept
{
    // RTKLIB treats smaller non-zero status codes as higher priority. Unknown is
    // kept behind all valid solutions.
    constexpr std::array best_to_worst{
        SolutionQuality::Fixed,
        SolutionQuality::Float,
        SolutionQuality::Sbas,
        SolutionQuality::Dgps,
        SolutionQuality::Single,
        SolutionQuality::Ppp,
        SolutionQuality::InvalidOrUnknown,
    };
    std::array result = best_to_worst;
    if (order == QualityDrawingOrder::BetterQualityInFront) {
        std::reverse(result.begin(), result.end());
    }
    return result;
}

PlotBatch build_trajectory_plot_batch(
    const PlotDataView& data, const QualityFilter& filter, const PlotBatchOptions& options)
{
    return build_plot_batch(data, filter, options, PlotProjection::Trajectory, std::nullopt,
        std::nullopt, [](const PlotSampleValue& sample) {
            return std::optional<PlotPoint>{PlotPoint{sample.east_m, sample.north_m}};
        });
}

PlotBatch build_time_series_plot_batch(const PlotDataView& data, const QualityFilter& filter,
    PositionComponent component, const PlotBatchOptions& options)
{
    const std::optional<GpsTime> first_time = first_visible_time(data, filter);
    const std::optional<GpsTime> origin = first_time.has_value()
        ? std::optional<GpsTime>{gps_week_boundary(*first_time)}
        : std::nullopt;
    return build_plot_batch(data, filter, options, PlotProjection::TimeSeries, component, origin,
        [component, origin](const PlotSampleValue& sample) {
            if (!origin.has_value()) {
                return std::optional<PlotPoint>{};
            }
            const std::optional<double> value = component_value(sample, component);
            if (!value.has_value()) {
                return std::optional<PlotPoint>{};
            }
            const double seconds = static_cast<double>(sample.time - *origin)
                / static_cast<double>(nanoseconds_per_second);
            return std::optional<PlotPoint>{PlotPoint{seconds, *value}};
        });
}

} // namespace rtktrace
