#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "plotcore/plot/batch.hpp"

namespace plotcore {

struct ImPlotComponentOptions {
    PlotBatchOptions batch;
    PositionComponent vertical_component{PositionComponent::Up};
    bool show_reference_relative_distance{true};
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

class ImPlotComponent {
public:
    void prepare(const PlotDataView& data, const QualityFilter& filter,
        const ImPlotComponentOptions& options = {}, bool fit_axes = true);
    void clear() noexcept;
    void request_fit() noexcept;
    void request_trajectory_fit() noexcept;
    void request_time_series_fit() noexcept;

    [[nodiscard]] bool set_trajectory_ranges(
        NumericRange east, NumericRange north) noexcept;
    [[nodiscard]] bool set_trajectory_meters_per_pixel(double value) noexcept;
    [[nodiscard]] bool pan_trajectory_by_fraction(
        double east_fraction, double north_fraction) noexcept;
    [[nodiscard]] bool zoom_trajectory_by_factor(double factor,
        std::optional<double> fixed_east = std::nullopt,
        std::optional<double> fixed_north = std::nullopt) noexcept;
    [[nodiscard]] bool set_time_series_time_range(TimeRange range) noexcept;
    [[nodiscard]] bool set_time_series_position_range(
        PositionComponent component, NumericRange range) noexcept;
    [[nodiscard]] bool zoom_time_series_time_by_factor(double factor,
        std::optional<GpsTime> fixed_time = std::nullopt) noexcept;
    [[nodiscard]] bool zoom_time_series_position_by_factor(
        PositionComponent component, double factor,
        std::optional<double> fixed_position = std::nullopt) noexcept;

    void render_trajectory(std::string_view id, PlotAreaSize widget_size);
    void render_time_series(std::string_view id, PlotAreaSize widget_size);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] PlotDataKind data_kind() const noexcept;
    [[nodiscard]] std::size_t visible_sample_count() const noexcept;
    [[nodiscard]] const std::optional<TrajectoryPlotMetrics>& trajectory_metrics()
        const noexcept;
    [[nodiscard]] const std::vector<TimeSeriesPanelMetrics>& time_series_metrics()
        const noexcept;
    [[nodiscard]] std::optional<TimeRange> time_series_time_range() const noexcept;
    [[nodiscard]] std::optional<double> consume_window_resize_factor() noexcept;

private:
    PlotDataKind data_kind_{PlotDataKind::Normal};
    ImPlotComponentOptions options_;
    PlotBatch trajectory_{PlotProjection::Trajectory, std::nullopt, std::nullopt,
        {}, 0, std::nullopt};
    std::vector<PlotBatch> time_series_;
    bool fit_trajectory_pending_{true};
    bool fit_time_pending_{true};
    std::optional<TrajectoryPlotMetrics> trajectory_metrics_;
    std::vector<TimeSeriesPanelMetrics> time_series_metrics_;
    NumericRange last_time_range_seconds_{0.0, 1.0};
    std::optional<TrajectoryPlotMetrics> requested_trajectory_limits_;
    std::optional<NumericRange> requested_time_limits_seconds_;
    std::array<std::optional<NumericRange>, 5> requested_position_limits_{};
    double pending_window_resize_wheel_{0.0};
};

} // namespace plotcore
