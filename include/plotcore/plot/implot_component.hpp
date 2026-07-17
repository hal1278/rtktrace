#pragma once

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
        const ImPlotComponentOptions& options = {});
    void clear() noexcept;
    void request_fit() noexcept;

    void render_trajectory(std::string_view id, PlotAreaSize widget_size);
    void render_time_series(std::string_view id, PlotAreaSize widget_size);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] PlotDataKind data_kind() const noexcept;
    [[nodiscard]] std::size_t visible_sample_count() const noexcept;
    [[nodiscard]] const std::optional<TrajectoryPlotMetrics>& trajectory_metrics()
        const noexcept;
    [[nodiscard]] const std::vector<TimeSeriesPanelMetrics>& time_series_metrics()
        const noexcept;

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
};

} // namespace plotcore
