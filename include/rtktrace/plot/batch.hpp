#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "rtktrace/plot/axis.hpp"

namespace rtktrace {

enum class DrawMode : std::uint8_t {
    Line,
    Point,
    LineAndPoint,
};

enum class SlotDrawingOrder : std::uint8_t {
    LargerSlotInFront,
    SmallerSlotInFront,
};

enum class QualityDrawingOrder : std::uint8_t {
    BetterQualityInFront,
    LowerQualityInFront,
};

struct Rgba8 {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
    std::uint8_t alpha;

    auto operator<=>(const Rgba8&) const = default;
};

// RTKPLOT File 1 defaults, indexed by normalized solution quality.
inline constexpr std::array<Rgba8, solution_quality_count> rtkplot_file1_quality_colors{
    Rgba8{192, 192, 192, 255},
    Rgba8{0, 128, 0, 255},
    Rgba8{255, 170, 0, 255},
    Rgba8{255, 0, 255, 255},
    Rgba8{0, 0, 255, 255},
    Rgba8{255, 0, 0, 255},
    Rgba8{0, 128, 128, 255},
};

inline constexpr Rgba8 rtkplot_connection_line_color{192, 192, 192, 255};

struct PlotPoint {
    double x;
    double y;
};

struct PlotBounds {
    double minimum_x;
    double maximum_x;
    double minimum_y;
    double maximum_y;
};

struct PlotLineStrip {
    std::vector<PlotPoint> points;
};

struct PlotMarkerBatch {
    SolutionQuality quality;
    std::vector<PlotPoint> points;
};

struct PlotSlotBatch {
    std::size_t slot_number;
    std::vector<PlotLineStrip> line_strips;
    std::vector<PlotMarkerBatch> marker_batches;
};

enum class PlotProjection : std::uint8_t {
    Trajectory,
    TimeSeries,
};

struct PlotBatchOptions {
    DrawMode draw_mode{DrawMode::LineAndPoint};
    bool bridge_hidden_quality_samples{true};
    SlotDrawingOrder slot_order{SlotDrawingOrder::LargerSlotInFront};
    QualityDrawingOrder quality_order{QualityDrawingOrder::BetterQualityInFront};
};

struct PlotBatch {
    PlotProjection projection;
    std::optional<PositionComponent> component;
    // Time-series x coordinates are seconds relative to this GPST week boundary.
    std::optional<GpsTime> time_origin;
    // Slots and marker batches are stored from back to front.
    std::vector<PlotSlotBatch> slots;
    std::size_t visible_sample_count;
    std::optional<PlotBounds> bounds;
};

[[nodiscard]] std::array<SolutionQuality, solution_quality_count> quality_order_back_to_front(
    QualityDrawingOrder order) noexcept;

[[nodiscard]] PlotBatch build_trajectory_plot_batch(
    const PlotDataView& data, const QualityFilter& filter, const PlotBatchOptions& options);
[[nodiscard]] PlotBatch build_time_series_plot_batch(const PlotDataView& data,
    const QualityFilter& filter, PositionComponent component, const PlotBatchOptions& options);

} // namespace rtktrace
