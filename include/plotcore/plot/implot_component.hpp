#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "plotcore/plot/batch.hpp"

namespace plotcore {

enum class TrajectoryRangePriority : std::uint8_t {
    AxisRange,
    DisplayScale,
};

enum class TrajectoryScaleFixedTarget : std::uint8_t {
    DrawingArea,
    AxisRange,
};

enum class TrajectoryAxis : std::uint8_t {
    East,
    North,
};

struct ImPlotComponentOptions {
    PlotBatchOptions batch;
    PositionComponent vertical_component{PositionComponent::Up};
    bool show_east{true};
    bool show_north{true};
    bool show_vertical{true};
    bool show_reference_relative_distance{true};
    TrajectoryRangePriority trajectory_range_priority{TrajectoryRangePriority::DisplayScale};
    TrajectoryScaleFixedTarget trajectory_scale_fixed_target{
        TrajectoryScaleFixedTarget::DrawingArea};
    float marker_size_px{3.0F};
};

struct TrajectoryPlotMetrics {
    NumericRange east;
    NumericRange north;
    double meters_per_pixel;
    double east_axis_length_px;
    double north_axis_length_px;
};

struct TimeSeriesPanelMetrics {
    PositionComponent component;
    NumericRange position;
    double position_axis_length_px;
};

enum class TrajectoryResizeFixedTarget : std::uint8_t {
    DisplayScale,
    AxisRange,
};

struct TrajectoryResizeRequest {
    NumericRange east;
    NumericRange north;
    double desired_east_axis_length_px;
    double desired_north_axis_length_px;
    double meters_per_pixel;
    TrajectoryResizeFixedTarget fixed_target;
};

class ImPlotComponent {
public:
    void prepare(const PlotDataView& data, const QualityFilter& filter,
        const ImPlotComponentOptions& options = {}, bool fit_axes = true);
    void clear() noexcept;
    void request_fit() noexcept;
    void request_trajectory_fit() noexcept;
    void request_time_series_fit() noexcept;

    [[nodiscard]] bool set_trajectory_ranges(NumericRange east, NumericRange north) noexcept;
    [[nodiscard]] bool set_trajectory_meters_per_pixel(double value) noexcept;
    [[nodiscard]] bool apply_trajectory_ranges(NumericRange east, NumericRange north) noexcept;
    [[nodiscard]] bool apply_trajectory_axis_range(
        TrajectoryAxis axis, NumericRange range) noexcept;
    [[nodiscard]] bool apply_trajectory_meters_per_pixel(double value) noexcept;
    [[nodiscard]] bool pan_trajectory_by_fraction(
        double east_fraction, double north_fraction) noexcept;
    [[nodiscard]] bool zoom_trajectory_by_factor(double factor,
        std::optional<double> fixed_east = std::nullopt,
        std::optional<double> fixed_north = std::nullopt) noexcept;
    [[nodiscard]] bool set_time_series_time_range(TimeRange range) noexcept;
    [[nodiscard]] bool set_time_series_position_range(
        PositionComponent component, NumericRange range) noexcept;
    [[nodiscard]] bool zoom_time_series_time_by_factor(
        double factor, std::optional<GpsTime> fixed_time = std::nullopt) noexcept;
    [[nodiscard]] bool zoom_time_series_position_by_factor(PositionComponent component,
        double factor, std::optional<double> fixed_position = std::nullopt) noexcept;

    void render_trajectory(std::string_view id, PlotAreaSize widget_size);
    void render_time_series(std::string_view id, PlotAreaSize widget_size);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] PlotDataKind data_kind() const noexcept;
    [[nodiscard]] std::size_t visible_sample_count() const noexcept;
    [[nodiscard]] std::size_t time_series_panel_count() const noexcept;
    [[nodiscard]] const std::optional<TrajectoryPlotMetrics>& trajectory_metrics() const noexcept;
    [[nodiscard]] const std::vector<TimeSeriesPanelMetrics>& time_series_metrics() const noexcept;
    [[nodiscard]] std::optional<TimeRange> time_series_time_range() const noexcept;
    [[nodiscard]] std::optional<double> consume_window_resize_factor() noexcept;
    [[nodiscard]] std::optional<TrajectoryResizeRequest>
    consume_trajectory_resize_request() noexcept;

private:
    void render_trajectory_axis_controls(std::string_view id);
    void render_time_series_axis_controls(std::string_view id);

    PlotDataKind data_kind_{PlotDataKind::Normal};
    ImPlotComponentOptions options_;
    PlotBatch trajectory_{
        PlotProjection::Trajectory, std::nullopt, std::nullopt, {}, 0, std::nullopt};
    std::vector<PlotBatch> time_series_;
    bool fit_trajectory_pending_{true};
    bool fit_time_pending_{true};
    std::optional<TrajectoryPlotMetrics> trajectory_metrics_;
    std::vector<TimeSeriesPanelMetrics> time_series_metrics_;
    NumericRange last_time_range_seconds_{0.0, 1.0};
    double last_time_axis_length_px_{0.0};
    std::optional<TrajectoryPlotMetrics> requested_trajectory_limits_;
    std::optional<TrajectoryResizeRequest> requested_trajectory_resize_;
    std::optional<PlotAreaSize> last_trajectory_widget_size_;
    TrajectoryAxis range_priority_axis_{TrajectoryAxis::East};
    std::optional<NumericRange> requested_time_limits_seconds_;
    std::array<std::optional<NumericRange>, 5> requested_position_limits_{};
    double pending_window_resize_wheel_{0.0};
};

} // namespace plotcore
