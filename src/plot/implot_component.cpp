#include "plotcore/plot/implot_component.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "implot.h"

namespace plotcore {
namespace {

constexpr double trajectory_data_fraction = 0.9;
constexpr double nanoseconds_per_second = 1'000'000'000.0;

[[nodiscard]] ImVec2 widget_size(PlotAreaSize size) noexcept
{
    return ImVec2{static_cast<float>(size.width_px), static_cast<float>(size.height_px)};
}

[[nodiscard]] ImVec4 color(Rgba8 value) noexcept
{
    constexpr float scale = 1.0F / 255.0F;
    return ImVec4{value.red * scale, value.green * scale, value.blue * scale,
        value.alpha * scale};
}

[[nodiscard]] ImU32 packed_color(Rgba8 value) noexcept
{
    return IM_COL32(value.red, value.green, value.blue, value.alpha);
}

[[nodiscard]] NumericRange expand_position_range(double minimum, double maximum) noexcept
{
    double length = maximum - minimum;
    if (length == 0.0) {
        return NumericRange{minimum - 0.5, maximum + 0.5};
    }
    if (length < minimum_position_axis_range_m) {
        const double center = (minimum + maximum) * 0.5;
        length = minimum_position_axis_range_m;
        return NumericRange{center - length * 0.5, center + length * 0.5};
    }
    return NumericRange{minimum, maximum};
}

[[nodiscard]] NumericRange expand_time_range(double minimum, double maximum) noexcept
{
    double length = maximum - minimum;
    if (length == 0.0) {
        constexpr double minute = 60.0;
        return NumericRange{minimum - minute * 0.5, maximum + minute * 0.5};
    }
    constexpr double minimum_seconds = 0.001;
    if (length < minimum_seconds) {
        const double center = (minimum + maximum) * 0.5;
        return NumericRange{center - minimum_seconds * 0.5,
            center + minimum_seconds * 0.5};
    }
    return NumericRange{minimum, maximum};
}

[[nodiscard]] std::optional<TrajectoryPlotMetrics> fit_trajectory(
    const PlotBatch& batch, ImVec2 plot_size) noexcept
{
    if (!batch.bounds.has_value() || plot_size.x <= 0.0F || plot_size.y <= 0.0F) {
        return std::nullopt;
    }
    const PlotBounds bounds = *batch.bounds;
    double east_span = bounds.maximum_x - bounds.minimum_x;
    double north_span = bounds.maximum_y - bounds.minimum_y;
    if (east_span == 0.0 && north_span == 0.0) {
        east_span = 1.0;
    }
    const double data_width = static_cast<double>(plot_size.x) * trajectory_data_fraction;
    const double data_height = static_cast<double>(plot_size.y) * trajectory_data_fraction;
    double meters_per_pixel = std::max(east_span / data_width, north_span / data_height);
    const double minimum_shorter_axis_pixels = std::min(plot_size.x, plot_size.y);
    meters_per_pixel = std::max(meters_per_pixel,
        minimum_position_axis_range_m / minimum_shorter_axis_pixels);
    if (bounds.minimum_x == bounds.maximum_x && bounds.minimum_y == bounds.maximum_y) {
        meters_per_pixel = std::max(meters_per_pixel,
            1.0 / minimum_shorter_axis_pixels);
    }
    const double east_range = meters_per_pixel * plot_size.x;
    const double north_range = meters_per_pixel * plot_size.y;
    const double east_center = (bounds.minimum_x + bounds.maximum_x) * 0.5;
    const double north_center = (bounds.minimum_y + bounds.maximum_y) * 0.5;
    return TrajectoryPlotMetrics{
        NumericRange{east_center - east_range * 0.5, east_center + east_range * 0.5},
        NumericRange{north_center - north_range * 0.5, north_center + north_range * 0.5},
        meters_per_pixel,
        plot_size.x,
        plot_size.y,
    };
}

void draw_line_strips(const PlotSlotBatch& slot)
{
    ImPlot::SetNextLineStyle(color(rtkplot_connection_line_color), 1.0F);
    for (std::size_t index = 0; index < slot.line_strips.size(); ++index) {
        const std::vector<PlotPoint>& points = slot.line_strips[index].points;
        if (points.size() < 2) {
            continue;
        }
        const std::string label = "##slot" + std::to_string(slot.slot_number)
            + "-line" + std::to_string(index);
        ImPlot::SetNextLineStyle(color(rtkplot_connection_line_color), 1.0F);
        ImPlot::PlotLine(label.c_str(), &points.front().x, &points.front().y,
            static_cast<int>(points.size()), ImPlotLineFlags_None, 0,
            static_cast<int>(sizeof(PlotPoint)));
    }
}

void draw_marker(ImDrawList& draw_list, PlotPoint point, std::size_t slot_number,
    float size, ImU32 marker_color)
{
    const ImVec2 center = ImPlot::PlotToPixels(point.x, point.y);
    if (slot_number == 1) {
        draw_list.AddCircleFilled(center, size, marker_color);
        return;
    }
    if (slot_number == 2) {
        draw_list.AddLine(ImVec2{center.x - size, center.y - size},
            ImVec2{center.x + size, center.y + size}, marker_color, 1.5F);
        draw_list.AddLine(ImVec2{center.x - size, center.y + size},
            ImVec2{center.x + size, center.y - size}, marker_color, 1.5F);
        return;
    }

    const std::size_t vertex_count = slot_number;
    draw_list.PathClear();
    for (std::size_t index = 0; index < vertex_count; ++index) {
        const double angle = -std::numbers::pi / 2.0
            + 2.0 * std::numbers::pi * static_cast<double>(index)
                / static_cast<double>(vertex_count);
        draw_list.PathLineTo(ImVec2{
            center.x + size * static_cast<float>(std::cos(angle)),
            center.y + size * static_cast<float>(std::sin(angle))});
    }
    draw_list.PathFillConvex(marker_color);
}

void draw_markers(const PlotSlotBatch& slot, float marker_size)
{
    ImPlotMarker marker = ImPlotMarker_None;
    switch (slot.slot_number) {
    case 1:
        marker = ImPlotMarker_Circle;
        break;
    case 2:
        marker = ImPlotMarker_Cross;
        break;
    case 3:
        marker = ImPlotMarker_Up;
        break;
    case 4:
        marker = ImPlotMarker_Square;
        break;
    default:
        break;
    }
    if (marker != ImPlotMarker_None) {
        for (const PlotMarkerBatch& batch : slot.marker_batches) {
            const std::size_t quality = static_cast<std::size_t>(batch.quality);
            if (quality >= rtkplot_file1_quality_colors.size()
                || batch.points.empty()) {
                continue;
            }
            const ImVec4 marker_color = color(rtkplot_file1_quality_colors[quality]);
            ImPlot::SetNextMarkerStyle(
                marker, marker_size, marker_color, 1.0F, marker_color);
            const std::string label = "##slot" + std::to_string(slot.slot_number)
                + "-quality" + std::to_string(quality);
            ImPlot::PlotScatter(label.c_str(), &batch.points.front().x,
                &batch.points.front().y, static_cast<int>(batch.points.size()),
                ImPlotScatterFlags_None, 0, static_cast<int>(sizeof(PlotPoint)));
        }
        return;
    }

    ImDrawList* draw_list = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    for (const PlotMarkerBatch& batch : slot.marker_batches) {
        const std::size_t quality = static_cast<std::size_t>(batch.quality);
        if (quality >= rtkplot_file1_quality_colors.size()) {
            continue;
        }
        const ImU32 marker_color = packed_color(rtkplot_file1_quality_colors[quality]);
        for (const PlotPoint point : batch.points) {
            draw_marker(*draw_list, point, slot.slot_number, marker_size, marker_color);
        }
    }
    ImPlot::PopPlotClipRect();
}

void draw_batch(const PlotBatch& batch, float marker_size)
{
    for (const PlotSlotBatch& slot : batch.slots) {
        draw_line_strips(slot);
        draw_markers(slot, marker_size);
    }
}

[[nodiscard]] const char* component_label(PositionComponent component) noexcept
{
    switch (component) {
    case PositionComponent::East:
        return "E-W (m)";
    case PositionComponent::North:
        return "N-S (m)";
    case PositionComponent::Up:
        return "U-D (m)";
    case PositionComponent::EllipsoidalHeight:
        return "Height (m)";
    case PositionComponent::ReferenceRelativeDistance3d:
        return "Distance (m)";
    }
    return "Position (m)";
}

[[nodiscard]] double rtkplot_time_tick(double seconds_per_pixel) noexcept
{
    constexpr std::array candidates{
        0.1, 0.2, 0.5, 1.0, 3.0, 6.0, 12.0, 30.0, 60.0, 300.0, 900.0,
        1800.0, 3600.0, 7200.0, 10800.0, 21600.0, 43200.0, 86400.0,
        172800.0, 604800.0, 1209600.0, 3024000.0, 6048000.0,
    };
    const double target = 60.0 * seconds_per_pixel;
    const auto candidate = std::find_if(candidates.begin(), candidates.end(),
        [target](double value) { return target <= value; });
    return candidate == candidates.end() ? 12096000.0 : *candidate;
}

[[nodiscard]] std::string gpst_tick_label(
    GpsTime origin, double seconds, double tick_seconds)
{
    const long double total_nanoseconds =
        static_cast<long double>(origin.nanoseconds_since_gps_epoch)
        + static_cast<long double>(seconds) * nanoseconds_per_second;
    if (total_nanoseconds < static_cast<long double>(std::numeric_limits<std::int64_t>::min())
        || total_nanoseconds > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return {};
    }
    constexpr std::int64_t nanoseconds_per_day = 86'400'000'000'000;
    std::int64_t nanoseconds = static_cast<std::int64_t>(std::llround(total_nanoseconds));
    std::int64_t days = nanoseconds / nanoseconds_per_day;
    std::int64_t day_nanoseconds = nanoseconds % nanoseconds_per_day;
    if (day_nanoseconds < 0) {
        day_nanoseconds += nanoseconds_per_day;
        --days;
    }
    using namespace std::chrono_literals;
    constexpr std::chrono::year_month_day gps_epoch =
        1980y / std::chrono::January / 6;
    const std::chrono::year_month_day date{
        std::chrono::sys_days{gps_epoch} + std::chrono::days{days}};
    const std::int64_t whole_seconds = day_nanoseconds / 1'000'000'000;
    const int hour = static_cast<int>(whole_seconds / 3600);
    const int minute = static_cast<int>((whole_seconds % 3600) / 60);
    const int second = static_cast<int>(whole_seconds % 60);
    const int tenth = static_cast<int>((day_nanoseconds % 1'000'000'000) / 100'000'000);
    char buffer[32];
    if (tick_seconds < 1.0) {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%d",
            hour, minute, second, tenth);
    } else if (tick_seconds < 60.0) {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hour, minute, second);
    } else if (tick_seconds < 86400.0) {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
    } else if (tick_seconds < 2592000.0) {
        std::snprintf(buffer, sizeof(buffer), "%02u/%02u",
            static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
    } else {
        const int year = static_cast<int>(date.year());
        std::snprintf(buffer, sizeof(buffer), "%02d/%02u", year % 100,
            static_cast<unsigned>(date.month()));
    }
    return buffer;
}

struct TimeTicks {
    std::vector<double> values;
    std::vector<std::string> labels;
    std::vector<const char*> label_pointers;
};

[[nodiscard]] std::vector<double> make_numeric_ticks(
    NumericRange range, double axis_pixels)
{
    if (range.length() <= 0.0 || axis_pixels <= 0.0) {
        return {};
    }
    const double target = 30.0 * range.length() / axis_pixels;
    const double order = std::pow(10.0, std::floor(std::log10(target)));
    constexpr std::array multipliers{1.0, 2.0, 5.0, 10.0};
    double tick = 10.0 * order;
    for (const double multiplier : multipliers) {
        if (target <= multiplier * order) {
            tick = multiplier * order;
            break;
        }
    }
    const double first = std::ceil(range.minimum / tick) * tick;
    std::vector<double> result;
    if (first > range.maximum) {
        return result;
    }
    const std::size_t count =
        static_cast<std::size_t>(std::floor((range.maximum - first) / tick)) + 1;
    result.reserve(std::min<std::size_t>(count, 1024));
    for (std::size_t index = 0; index < count && index < 1024; ++index) {
        result.push_back(first + static_cast<double>(index) * tick);
    }
    return result;
}

[[nodiscard]] TimeTicks make_time_ticks(
    NumericRange range, double axis_pixels, GpsTime origin)
{
    const double tick = rtkplot_time_tick(range.length() / std::max(axis_pixels, 1.0));
    const double first = std::ceil(range.minimum / tick) * tick;
    TimeTicks result;
    const std::size_t count = first <= range.maximum
        ? static_cast<std::size_t>(std::floor((range.maximum - first) / tick)) + 1
        : 0;
    result.values.reserve(std::min<std::size_t>(count, 1024));
    result.labels.reserve(std::min<std::size_t>(count, 1024));
    for (std::size_t index = 0; index < count && index < 1024; ++index) {
        const double value = first + static_cast<double>(index) * tick;
        result.values.push_back(value);
        result.labels.push_back(gpst_tick_label(origin, value, tick));
    }
    result.label_pointers.reserve(result.labels.size());
    for (const std::string& label : result.labels) {
        result.label_pointers.push_back(label.c_str());
    }
    return result;
}

void draw_dotted_vertical(ImDrawList& draw_list, float x, ImVec2 plot_position,
    ImVec2 plot_size, ImU32 grid_color)
{
    constexpr float dash = 1.0F;
    constexpr float step = 4.0F;
    for (float y = plot_position.y; y < plot_position.y + plot_size.y; y += step) {
        draw_list.AddLine(ImVec2{x, y},
            ImVec2{x, std::min(y + dash, plot_position.y + plot_size.y)}, grid_color);
    }
}

void draw_dotted_horizontal(ImDrawList& draw_list, float y, ImVec2 plot_position,
    ImVec2 plot_size, ImU32 grid_color)
{
    constexpr float dash = 1.0F;
    constexpr float step = 4.0F;
    for (float x = plot_position.x; x < plot_position.x + plot_size.x; x += step) {
        draw_list.AddLine(ImVec2{x, y},
            ImVec2{std::min(x + dash, plot_position.x + plot_size.x), y}, grid_color);
    }
}

void draw_rtkplot_grid(
    const std::vector<double>& x_ticks, const std::vector<double>& y_ticks)
{
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    const ImVec2 plot_position = ImPlot::GetPlotPos();
    const ImVec2 plot_size = ImPlot::GetPlotSize();
    ImDrawList* draw_list = ImPlot::GetPlotDrawList();
    const ImU32 grid_color = packed_color(rtkplot_connection_line_color);
    ImPlot::PushPlotClipRect();
    for (const double value : x_ticks) {
        if (value < limits.X.Min || value > limits.X.Max) {
            continue;
        }
        const float x = ImPlot::PlotToPixels(value, limits.Y.Min).x;
        if (value == 0.0) {
            draw_list->AddLine(ImVec2{x, plot_position.y},
                ImVec2{x, plot_position.y + plot_size.y}, grid_color);
        } else {
            draw_dotted_vertical(*draw_list, x, plot_position, plot_size, grid_color);
        }
    }
    for (const double value : y_ticks) {
        if (value < limits.Y.Min || value > limits.Y.Max) {
            continue;
        }
        const float y = ImPlot::PlotToPixels(limits.X.Min, value).y;
        if (value == 0.0) {
            draw_list->AddLine(ImVec2{plot_position.x, y},
                ImVec2{plot_position.x + plot_size.x, y}, grid_color);
        } else {
            draw_dotted_horizontal(*draw_list, y, plot_position, plot_size, grid_color);
        }
    }
    ImPlot::PopPlotClipRect();
}

} // namespace

void ImPlotComponent::prepare(const PlotDataView& data, const QualityFilter& filter,
    const ImPlotComponentOptions& options, bool fit_axes)
{
    data_kind_ = data.kind;
    options_ = options;
    options_.marker_size_px = std::max(options_.marker_size_px, 1.0F);
    trajectory_ = build_trajectory_plot_batch(data, filter, options_.batch);
    time_series_.clear();
    time_series_.push_back(build_time_series_plot_batch(
        data, filter, PositionComponent::East, options_.batch));
    time_series_.push_back(build_time_series_plot_batch(
        data, filter, PositionComponent::North, options_.batch));
    time_series_.push_back(build_time_series_plot_batch(
        data, filter, options_.vertical_component, options_.batch));
    if (data.kind == PlotDataKind::Relative
        && options_.show_reference_relative_distance) {
        time_series_.push_back(build_time_series_plot_batch(data, filter,
            PositionComponent::ReferenceRelativeDistance3d, options_.batch));
    }
    if (fit_axes) {
        request_fit();
        trajectory_metrics_.reset();
        time_series_metrics_.clear();
    }
}

void ImPlotComponent::clear() noexcept
{
    trajectory_.slots.clear();
    trajectory_.visible_sample_count = 0;
    trajectory_.bounds.reset();
    time_series_.clear();
    trajectory_metrics_.reset();
    time_series_metrics_.clear();
    requested_trajectory_limits_.reset();
    requested_time_limits_seconds_.reset();
    for (std::optional<NumericRange>& range : requested_position_limits_) {
        range.reset();
    }
}

void ImPlotComponent::request_fit() noexcept
{
    request_trajectory_fit();
    request_time_series_fit();
}

void ImPlotComponent::request_trajectory_fit() noexcept
{
    fit_trajectory_pending_ = true;
}

void ImPlotComponent::request_time_series_fit() noexcept
{
    fit_time_pending_ = true;
}

bool ImPlotComponent::set_trajectory_ranges(
    NumericRange east, NumericRange north) noexcept
{
    if (!trajectory_metrics_.has_value() || !std::isfinite(east.minimum)
        || !std::isfinite(east.maximum) || !std::isfinite(north.minimum)
        || !std::isfinite(north.maximum) || east.minimum >= east.maximum
        || north.minimum >= north.maximum) {
        return false;
    }
    const double east_pixels = std::max(trajectory_metrics_->east_axis_length_px, 1.0);
    const double north_pixels = std::max(trajectory_metrics_->north_axis_length_px, 1.0);
    double meters_per_pixel = std::max(
        east.length() / east_pixels, north.length() / north_pixels);
    meters_per_pixel = std::max(meters_per_pixel,
        minimum_position_axis_range_m / std::min(east_pixels, north_pixels));
    const double east_center = (east.minimum + east.maximum) * 0.5;
    const double north_center = (north.minimum + north.maximum) * 0.5;
    requested_trajectory_limits_ = TrajectoryPlotMetrics{
        NumericRange{east_center - meters_per_pixel * east_pixels * 0.5,
            east_center + meters_per_pixel * east_pixels * 0.5},
        NumericRange{north_center - meters_per_pixel * north_pixels * 0.5,
            north_center + meters_per_pixel * north_pixels * 0.5},
        meters_per_pixel,
        east_pixels,
        north_pixels,
    };
    fit_trajectory_pending_ = false;
    return true;
}

bool ImPlotComponent::set_trajectory_meters_per_pixel(double value) noexcept
{
    if (!trajectory_metrics_.has_value() || !std::isfinite(value) || value <= 0.0) {
        return false;
    }
    const double east_pixels = std::max(trajectory_metrics_->east_axis_length_px, 1.0);
    const double north_pixels = std::max(trajectory_metrics_->north_axis_length_px, 1.0);
    value = std::max(value,
        minimum_position_axis_range_m / std::min(east_pixels, north_pixels));
    const double east_center =
        (trajectory_metrics_->east.minimum + trajectory_metrics_->east.maximum) * 0.5;
    const double north_center =
        (trajectory_metrics_->north.minimum + trajectory_metrics_->north.maximum) * 0.5;
    return set_trajectory_ranges(
        NumericRange{east_center - value * east_pixels * 0.5,
            east_center + value * east_pixels * 0.5},
        NumericRange{north_center - value * north_pixels * 0.5,
            north_center + value * north_pixels * 0.5});
}

bool ImPlotComponent::pan_trajectory_by_fraction(
    double east_fraction, double north_fraction) noexcept
{
    if (!trajectory_metrics_.has_value() || !std::isfinite(east_fraction)
        || !std::isfinite(north_fraction)) {
        return false;
    }
    const double east_offset = trajectory_metrics_->east.length() * east_fraction;
    const double north_offset = trajectory_metrics_->north.length() * north_fraction;
    requested_trajectory_limits_ = *trajectory_metrics_;
    requested_trajectory_limits_->east.minimum += east_offset;
    requested_trajectory_limits_->east.maximum += east_offset;
    requested_trajectory_limits_->north.minimum += north_offset;
    requested_trajectory_limits_->north.maximum += north_offset;
    fit_trajectory_pending_ = false;
    return true;
}

bool ImPlotComponent::set_time_series_time_range(TimeRange range) noexcept
{
    if (time_series_.empty() || !time_series_.front().time_origin.has_value()
        || range.start > range.end) {
        return false;
    }
    const GpsTime origin = *time_series_.front().time_origin;
    NumericRange seconds{
        static_cast<double>(range.start - origin) / nanoseconds_per_second,
        static_cast<double>(range.end - origin) / nanoseconds_per_second,
    };
    if (seconds.length() < 0.001) {
        const double center = (seconds.minimum + seconds.maximum) * 0.5;
        seconds = NumericRange{center - 0.0005, center + 0.0005};
    }
    requested_time_limits_seconds_ = seconds;
    fit_time_pending_ = false;
    return true;
}

bool ImPlotComponent::set_time_series_position_range(
    PositionComponent component, NumericRange range) noexcept
{
    const std::size_t index = static_cast<std::size_t>(component);
    if (index >= requested_position_limits_.size() || !std::isfinite(range.minimum)
        || !std::isfinite(range.maximum) || range.minimum >= range.maximum) {
        return false;
    }
    if (range.length() < minimum_position_axis_range_m) {
        const double center = (range.minimum + range.maximum) * 0.5;
        range = NumericRange{center - minimum_position_axis_range_m * 0.5,
            center + minimum_position_axis_range_m * 0.5};
    }
    requested_position_limits_[index] = range;
    fit_time_pending_ = false;
    return true;
}

void ImPlotComponent::render_trajectory(std::string_view id, PlotAreaSize requested_size)
{
    const std::string plot_id{id};
    ImVec2 frame_size = widget_size(requested_size);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (frame_size.x <= 0.0F) {
        frame_size.x = available.x;
    }
    if (frame_size.y <= 0.0F) {
        frame_size.y = available.y;
    }
    std::optional<TrajectoryPlotMetrics> requested_fit;
    if (fit_trajectory_pending_) {
        requested_fit = fit_trajectory(trajectory_, frame_size);
    }
    const std::optional<TrajectoryPlotMetrics> requested_limits =
        requested_trajectory_limits_.has_value()
        ? requested_trajectory_limits_
        : requested_fit;
    NumericRange east_ticks = requested_limits.has_value() ? requested_limits->east
        : trajectory_metrics_.has_value() ? trajectory_metrics_->east
                                          : NumericRange{-1.0, 1.0};
    NumericRange north_ticks = requested_limits.has_value() ? requested_limits->north
        : trajectory_metrics_.has_value() ? trajectory_metrics_->north
                                          : NumericRange{-1.0, 1.0};
    const std::vector<double> x_ticks = make_numeric_ticks(east_ticks, frame_size.x);
    const std::vector<double> y_ticks = make_numeric_ticks(north_ticks, frame_size.y);
    if (!ImPlot::BeginPlot(plot_id.c_str(), frame_size,
            ImPlotFlags_NoLegend | ImPlotFlags_Equal)) {
        return;
    }
    ImPlot::SetupAxes("E-W (m)", "N-S (m)",
        ImPlotAxisFlags_NoGridLines, ImPlotAxisFlags_NoGridLines);
    if (!x_ticks.empty()) {
        ImPlot::SetupAxisTicks(
            ImAxis_X1, x_ticks.data(), static_cast<int>(x_ticks.size()));
    }
    if (!y_ticks.empty()) {
        ImPlot::SetupAxisTicks(
            ImAxis_Y1, y_ticks.data(), static_cast<int>(y_ticks.size()));
    }
    if (fit_trajectory_pending_ || requested_trajectory_limits_.has_value()) {
        if (requested_limits.has_value()) {
            ImPlot::SetupAxesLimits(requested_limits->east.minimum,
                requested_limits->east.maximum, requested_limits->north.minimum,
                requested_limits->north.maximum,
                ImPlotCond_Always);
        }
        fit_trajectory_pending_ = false;
        requested_trajectory_limits_.reset();
    }
    ImPlot::SetupFinish();
    draw_rtkplot_grid(x_ticks, y_ticks);
    draw_batch(trajectory_, options_.marker_size_px);
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    const ImVec2 actual_size = ImPlot::GetPlotSize();
    trajectory_metrics_ = TrajectoryPlotMetrics{
        NumericRange{limits.X.Min, limits.X.Max},
        NumericRange{limits.Y.Min, limits.Y.Max},
        (limits.X.Max - limits.X.Min) / std::max(static_cast<double>(actual_size.x), 1.0),
        actual_size.x,
        actual_size.y,
    };
    if (ImPlot::IsPlotHovered()) {
        double east_fraction = 0.0;
        double north_fraction = 0.0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            east_fraction -= 0.05;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            east_fraction += 0.05;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            north_fraction -= 0.05;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            north_fraction += 0.05;
        }
        if (east_fraction != 0.0 || north_fraction != 0.0) {
            static_cast<void>(pan_trajectory_by_fraction(
                east_fraction, north_fraction));
        }
    }
    ImPlot::EndPlot();
}

void ImPlotComponent::render_time_series(std::string_view id, PlotAreaSize requested_size)
{
    if (time_series_.empty()) {
        return;
    }
    std::optional<PlotBounds> combined;
    for (const PlotBatch& batch : time_series_) {
        if (!batch.bounds.has_value()) {
            continue;
        }
        if (!combined.has_value()) {
            combined = batch.bounds;
        } else {
            combined->minimum_x = std::min(combined->minimum_x, batch.bounds->minimum_x);
            combined->maximum_x = std::max(combined->maximum_x, batch.bounds->maximum_x);
        }
    }
    if (fit_time_pending_ && combined.has_value()) {
        last_time_range_seconds_ = expand_time_range(
            combined->minimum_x, combined->maximum_x);
    }
    if (requested_time_limits_seconds_.has_value()) {
        last_time_range_seconds_ = *requested_time_limits_seconds_;
    }

    ImVec2 frame_size = widget_size(requested_size);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (frame_size.x <= 0.0F) {
        frame_size.x = available.x;
    }
    if (frame_size.y <= 0.0F) {
        frame_size.y = available.y;
    }
    const std::string subplot_id{id};
    if (!ImPlot::BeginSubplots(subplot_id.c_str(),
            static_cast<int>(time_series_.size()), 1, frame_size,
            ImPlotSubplotFlags_LinkCols | ImPlotSubplotFlags_NoLegend)) {
        return;
    }
    const std::vector<TimeSeriesPanelMetrics> previous_metrics = time_series_metrics_;
    time_series_metrics_.clear();
    for (std::size_t index = 0; index < time_series_.size(); ++index) {
        PlotBatch& batch = time_series_[index];
        const PositionComponent component = batch.component.value_or(PositionComponent::Up);
        const std::string plot_id = "##panel" + std::to_string(index);
        if (!ImPlot::BeginPlot(plot_id.c_str(), ImVec2{-1.0F, 0.0F}, ImPlotFlags_NoLegend)) {
            continue;
        }
        const bool bottom = index + 1 == time_series_.size();
        ImPlot::SetupAxis(ImAxis_X1, bottom ? "TIME (GPST)" : nullptr,
            ImPlotAxisFlags_NoGridLines
                | (bottom ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoTickLabels));
        ImPlot::SetupAxis(ImAxis_Y1, component_label(component),
            ImPlotAxisFlags_NoGridLines);
        NumericRange y_tick_range = batch.bounds.has_value()
            ? expand_position_range(batch.bounds->minimum_y, batch.bounds->maximum_y)
            : NumericRange{-1.0, 1.0};
        if (!fit_time_pending_ && index < previous_metrics.size()) {
            y_tick_range = previous_metrics[index].position;
        }
        const std::size_t component_index = static_cast<std::size_t>(component);
        const std::optional<NumericRange> requested_y =
            component_index < requested_position_limits_.size()
            ? requested_position_limits_[component_index]
            : std::nullopt;
        if ((fit_time_pending_ && combined.has_value())
            || requested_time_limits_seconds_.has_value()) {
            ImPlot::SetupAxisLimits(ImAxis_X1, last_time_range_seconds_.minimum,
                last_time_range_seconds_.maximum, ImPlotCond_Always);
            if (fit_time_pending_ && batch.bounds.has_value()) {
                const NumericRange y = expand_position_range(
                    batch.bounds->minimum_y, batch.bounds->maximum_y);
                ImPlot::SetupAxisLimits(
                    ImAxis_Y1, y.minimum, y.maximum, ImPlotCond_Always);
            }
        }
        if (requested_y.has_value()) {
            ImPlot::SetupAxisLimits(ImAxis_Y1,
                requested_y->minimum, requested_y->maximum, ImPlotCond_Always);
            y_tick_range = *requested_y;
        }
        const GpsTime origin = batch.time_origin.value_or(GpsTime{0});
        TimeTicks ticks = make_time_ticks(last_time_range_seconds_,
            frame_size.x, origin);
        const std::vector<double> y_ticks = make_numeric_ticks(y_tick_range,
            frame_size.y / static_cast<double>(time_series_.size()));
        if (!ticks.values.empty()) {
            ImPlot::SetupAxisTicks(ImAxis_X1, ticks.values.data(),
                static_cast<int>(ticks.values.size()), ticks.label_pointers.data(), false);
        }
        if (!y_ticks.empty()) {
            ImPlot::SetupAxisTicks(
                ImAxis_Y1, y_ticks.data(), static_cast<int>(y_ticks.size()));
        }
        ImPlot::SetupFinish();
        draw_rtkplot_grid(ticks.values, y_ticks);
        draw_batch(batch, options_.marker_size_px);
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        const ImVec2 actual_size = ImPlot::GetPlotSize();
        last_time_range_seconds_ = NumericRange{limits.X.Min, limits.X.Max};
        time_series_metrics_.push_back(TimeSeriesPanelMetrics{
            component,
            NumericRange{limits.Y.Min, limits.Y.Max},
            actual_size.y,
        });
        ImPlot::EndPlot();
    }
    fit_time_pending_ = false;
    requested_time_limits_seconds_.reset();
    for (std::optional<NumericRange>& range : requested_position_limits_) {
        range.reset();
    }
    ImPlot::EndSubplots();
}

bool ImPlotComponent::empty() const noexcept
{
    return !trajectory_.bounds.has_value();
}

PlotDataKind ImPlotComponent::data_kind() const noexcept
{
    return data_kind_;
}

std::size_t ImPlotComponent::visible_sample_count() const noexcept
{
    return trajectory_.visible_sample_count;
}

const std::optional<TrajectoryPlotMetrics>& ImPlotComponent::trajectory_metrics()
    const noexcept
{
    return trajectory_metrics_;
}

const std::vector<TimeSeriesPanelMetrics>& ImPlotComponent::time_series_metrics()
    const noexcept
{
    return time_series_metrics_;
}

std::optional<TimeRange> ImPlotComponent::time_series_time_range() const noexcept
{
    if (time_series_.empty() || !time_series_.front().time_origin.has_value()) {
        return std::nullopt;
    }
    const long double origin = static_cast<long double>(
        time_series_.front().time_origin->nanoseconds_since_gps_epoch);
    const long double minimum = origin
        + static_cast<long double>(last_time_range_seconds_.minimum)
            * nanoseconds_per_second;
    const long double maximum = origin
        + static_cast<long double>(last_time_range_seconds_.maximum)
            * nanoseconds_per_second;
    if (minimum < static_cast<long double>(std::numeric_limits<std::int64_t>::min())
        || maximum > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return TimeRange{
        GpsTime{static_cast<std::int64_t>(std::llround(minimum))},
        GpsTime{static_cast<std::int64_t>(std::llround(maximum))},
    };
}

} // namespace plotcore
