#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "implot.h"
#include "plotcore/plot/implot_component.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] plotcore::NormalizedSample sample_at(
    std::int64_t time_ns, std::size_t slot, std::size_t index)
{
    const double offset = static_cast<double>(slot) * 0.25;
    const double position = static_cast<double>(index) * 0.001;
    return plotcore::NormalizedSample{
        .time = plotcore::GpsTime{time_ns},
        .llh = plotcore::Wgs84Llh{0.0, 0.0, 100.0 + offset + position},
        .ecef = {},
        .enu = plotcore::Enu{position, offset + position * 0.5, position * 0.1},
        .quality = static_cast<plotcore::SolutionQuality>(index % 6 + 1),
        .source_line_number = index + 1,
        .continuous_from_previous = index != 0,
    };
}

} // namespace

int main()
{
    using namespace plotcore;
    constexpr std::size_t slot_count = 4;
    constexpr std::size_t samples_per_slot = 10'000;
    std::vector<std::vector<NormalizedSample>> samples(slot_count);
    PlotDataView data{PlotDataKind::Normal, {}};
    data.series.reserve(slot_count);
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
        samples[slot].reserve(samples_per_slot);
        for (std::size_t index = 0; index < samples_per_slot; ++index) {
            samples[slot].push_back(sample_at(
                1'400'000'000'000'000'000LL + static_cast<std::int64_t>(index) * 100'000'000,
                slot + 1, index));
        }
        data.series.push_back(
            PlotSeriesView{slot + 1, true, std::span<const NormalizedSample>{samples[slot]}});
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{1600.0F, 1200.0F};
    io.DeltaTime = 1.0F / 60.0F;
    // The production OpenGL3 backend advertises this before rendering. Mirror
    // that capability in the backend-free harness so large draw lists split by
    // VtxOffset exactly as they do in the application.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    ImPlotComponent component;
    const auto prepare_start = std::chrono::steady_clock::now();
    component.prepare(data, QualityFilter{});
    const auto prepare_end = std::chrono::steady_clock::now();
    check(component.visible_sample_count() == slot_count * samples_per_slot,
        "component prepares every visible sample from multiple large files");

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin(
        "plot performance", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    const auto render_start = std::chrono::steady_clock::now();
    const bool trajectory_rendered =
        component.render_trajectory("Trajectory", PlotAreaSize{1500.0, 400.0});
    component.render_time_series("Time series", PlotAreaSize{1500.0, 700.0});
    const auto render_end = std::chrono::steady_clock::now();
    ImGui::End();
    ImGui::Render();

    check(trajectory_rendered && component.trajectory_metrics().has_value()
            && component.trajectory_metrics()->meters_per_pixel > 0.0,
        "successful trajectory rendering reports refreshed range, axis length, and scale");
    check(component.time_series_metrics().size() == 3,
        "normal time-series rendering produces linked East, North, and vertical panels");
    check(component.time_series_panel_count() == 3,
        "default time-series selection prepares three position panels");

    const double requested_scale = component.trajectory_metrics()->meters_per_pixel * 2.0;
    const std::optional<TimeRange> rendered_time = component.time_series_time_range();
    check(component.set_trajectory_meters_per_pixel(requested_scale)
            && !component.set_trajectory_ranges(NumericRange{1.0, 0.0}, NumericRange{0.0, 1.0}),
        "numeric trajectory scale accepts positive values and rejects reversed ranges");
    check(rendered_time.has_value()
            && component.set_time_series_time_range(
                TimeRange{rendered_time->start, rendered_time->start})
            && component.set_time_series_position_range(
                PositionComponent::East, NumericRange{0.0, 0.0001}),
        "time-series numeric ranges are accepted and clamped to minimum spans");

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin(
        "plot numeric ranges", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    component.render_trajectory("Trajectory", PlotAreaSize{1500.0, 400.0});
    component.render_time_series("Time series", PlotAreaSize{1500.0, 700.0});
    ImGui::End();
    ImGui::Render();
    check(component.trajectory_metrics().has_value()
            && std::abs(component.trajectory_metrics()->meters_per_pixel - requested_scale)
                < requested_scale * 0.01,
        "numeric trajectory scale is applied on the next frame");

    const double previous_east_center = (component.trajectory_metrics()->east.minimum
                                            + component.trajectory_metrics()->east.maximum)
        * 0.5;
    const double east_span = component.trajectory_metrics()->east.length();
    check(component.pan_trajectory_by_fraction(0.05, 0.0),
        "trajectory pan accepts a fractional axis displacement");
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin(
        "plot keyboard pan", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    component.render_trajectory("Trajectory", PlotAreaSize{1500.0, 400.0});
    ImGui::End();
    ImGui::Render();
    const double moved_east_center = (component.trajectory_metrics()->east.minimum
                                         + component.trajectory_metrics()->east.maximum)
        * 0.5;
    check(
        std::abs((moved_east_center - previous_east_center) - east_span * 0.05) < east_span * 0.001,
        "trajectory pan moves only the requested axis by five percent");

    const double pre_zoom_span = component.trajectory_metrics()->east.length();
    const double pre_zoom_center = (component.trajectory_metrics()->east.minimum
                                       + component.trajectory_metrics()->east.maximum)
        * 0.5;
    check(
        component.zoom_trajectory_by_factor(0.5), "trajectory zoom accepts a center-fixed factor");
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin(
        "plot wheel zoom", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    component.render_trajectory("Trajectory", PlotAreaSize{1500.0, 400.0});
    ImGui::End();
    ImGui::Render();
    const double zoomed_center = (component.trajectory_metrics()->east.minimum
                                     + component.trajectory_metrics()->east.maximum)
        * 0.5;
    check(std::abs(component.trajectory_metrics()->east.length() - pre_zoom_span * 0.5)
                < pre_zoom_span * 0.01
            && std::abs(zoomed_center - pre_zoom_center) < pre_zoom_span * 0.001,
        "trajectory center-fixed zoom preserves the center and scales the span");

    const std::optional<TimeRange> pre_zoom_time = component.time_series_time_range();
    const auto pre_zoom_east = std::find_if(component.time_series_metrics().begin(),
        component.time_series_metrics().end(), [](const TimeSeriesPanelMetrics& metrics) {
            return metrics.component == PositionComponent::East;
        });
    check(pre_zoom_time.has_value() && pre_zoom_east != component.time_series_metrics().end()
            && component.zoom_time_series_time_by_factor(2.0)
            && component.zoom_time_series_position_by_factor(PositionComponent::East, 2.0),
        "time-series zoom accepts independent shared-time and vertical factors");
    const double pre_zoom_east_span = pre_zoom_east->position.length();
    const std::int64_t pre_zoom_time_span = pre_zoom_time->end - pre_zoom_time->start;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin(
        "time-series wheel zoom", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    component.render_time_series("Time series", PlotAreaSize{1500.0, 700.0});
    ImGui::End();
    ImGui::Render();
    const std::optional<TimeRange> zoomed_time = component.time_series_time_range();
    const auto zoomed_east = std::find_if(component.time_series_metrics().begin(),
        component.time_series_metrics().end(), [](const TimeSeriesPanelMetrics& metrics) {
            return metrics.component == PositionComponent::East;
        });
    check(zoomed_time.has_value() && zoomed_east != component.time_series_metrics().end()
            && std::abs(static_cast<double>(zoomed_time->end - zoomed_time->start)
                   - static_cast<double>(pre_zoom_time_span) * 2.0)
                < static_cast<double>(pre_zoom_time_span) * 0.001
            && std::abs(zoomed_east->position.length() - pre_zoom_east_span * 2.0)
                < pre_zoom_east_span * 0.001,
        "time-series zoom changes only the requested vertical range and shared time range");

    const double scale_before_resize = component.trajectory_metrics()->meters_per_pixel;
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin(
            "trajectory resize", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        component.render_trajectory("Trajectory", PlotAreaSize{1200.0, 500.0});
        ImGui::End();
        ImGui::Render();
    }
    check(std::abs(component.trajectory_metrics()->meters_per_pixel - scale_before_resize)
            < scale_before_resize * 0.01,
        "trajectory keeps meters per pixel when its drawing area is resized");

    ImPlotComponentOptions display_scale_options;
    display_scale_options.trajectory_range_priority = TrajectoryRangePriority::DisplayScale;
    component.prepare(data, QualityFilter{}, display_scale_options, false);
    const TrajectoryPlotMetrics priority_metrics = *component.trajectory_metrics();
    const NumericRange priority_east{-25.0, 35.0};
    check(component.apply_trajectory_axis_range(TrajectoryAxis::East, priority_east),
        "display-scale priority accepts a numeric East trajectory range");
    const std::optional<TrajectoryResizeRequest> scale_priority_request =
        component.consume_trajectory_resize_request();
    check(scale_priority_request.has_value()
            && scale_priority_request->fixed_target == TrajectoryResizeFixedTarget::DisplayScale
            && std::abs(scale_priority_request->desired_east_axis_length_px
                   - priority_east.length() / priority_metrics.meters_per_pixel)
                < 0.001
            && std::abs(scale_priority_request->desired_north_axis_length_px
                   - priority_metrics.north_axis_length_px)
                < 0.001,
        "display-scale priority resizes only the target direction at the existing scale");

    ImPlotComponentOptions fixed_axis_options = display_scale_options;
    fixed_axis_options.trajectory_scale_fixed_target = TrajectoryScaleFixedTarget::AxisRange;
    component.prepare(data, QualityFilter{}, fixed_axis_options, false);
    const TrajectoryPlotMetrics fixed_axis_metrics = *component.trajectory_metrics();
    const double fixed_axis_scale = fixed_axis_metrics.meters_per_pixel * 0.5;
    check(component.apply_trajectory_meters_per_pixel(fixed_axis_scale),
        "axis-range fixed target accepts a numeric trajectory scale");
    const std::optional<TrajectoryResizeRequest> fixed_axis_request =
        component.consume_trajectory_resize_request();
    check(fixed_axis_request.has_value()
            && fixed_axis_request->fixed_target == TrajectoryResizeFixedTarget::AxisRange
            && fixed_axis_request->east.minimum == fixed_axis_metrics.east.minimum
            && fixed_axis_request->east.maximum == fixed_axis_metrics.east.maximum
            && fixed_axis_request->north.minimum == fixed_axis_metrics.north.minimum
            && fixed_axis_request->north.maximum == fixed_axis_metrics.north.maximum
            && std::abs(fixed_axis_request->desired_east_axis_length_px
                   - fixed_axis_metrics.east.length() / fixed_axis_scale)
                < 0.001,
        "axis-range fixed target preserves both ranges and requests a new drawing size");

    ImPlotComponentOptions axis_priority_options;
    axis_priority_options.trajectory_range_priority = TrajectoryRangePriority::AxisRange;
    component.prepare(data, QualityFilter{}, axis_priority_options, false);
    const TrajectoryPlotMetrics before_axis_range = *component.trajectory_metrics();
    const double requested_east_center =
        (before_axis_range.east.minimum + before_axis_range.east.maximum) * 0.5 + 2.0;
    const double requested_east_span = before_axis_range.east.length() * 1.25;
    const NumericRange requested_east{requested_east_center - requested_east_span * 0.5,
        requested_east_center + requested_east_span * 0.5};
    check(component.apply_trajectory_axis_range(TrajectoryAxis::East, requested_east),
        "axis priority accepts a numeric East trajectory range");
    const auto render_axis_priority = [&component, &io](PlotAreaSize size) {
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("trajectory axis priority", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
            component.render_trajectory("Trajectory", size);
            ImGui::End();
            ImGui::Render();
        }
    };
    render_axis_priority(PlotAreaSize{1200.0, 500.0});
    check(std::abs(component.trajectory_metrics()->east.minimum - requested_east.minimum)
                < requested_east_span * 0.001
            && std::abs(component.trajectory_metrics()->east.maximum - requested_east.maximum)
                < requested_east_span * 0.001
            && std::abs(component.trajectory_metrics()->east.length()
                       / component.trajectory_metrics()->east_axis_length_px
                   - component.trajectory_metrics()->north.length()
                       / component.trajectory_metrics()->north_axis_length_px)
                < component.trajectory_metrics()->meters_per_pixel * 0.001,
        "axis priority fixes the target range and symmetrically updates the other axis");

    const NumericRange north_before_width_setup = component.trajectory_metrics()->north;
    check(component.apply_trajectory_axis_range(TrajectoryAxis::North, north_before_width_setup),
        "axis priority accepts a North range before width-only resize");
    render_axis_priority(PlotAreaSize{1200.0, 500.0});
    const TrajectoryPlotMetrics before_width_resize = *component.trajectory_metrics();
    render_axis_priority(PlotAreaSize{1400.0, 500.0});
    const TrajectoryPlotMetrics after_width_resize = *component.trajectory_metrics();
    check(std::abs(after_width_resize.east.minimum - before_width_resize.east.minimum)
                < before_width_resize.east.length() * 0.001
            && std::abs(after_width_resize.east.maximum - before_width_resize.east.maximum)
                < before_width_resize.east.length() * 0.001
            && std::abs(
                   after_width_resize.east_axis_length_px - before_width_resize.east_axis_length_px)
                > 0.5
            && std::abs(after_width_resize.north_axis_length_px
                   - before_width_resize.north_axis_length_px)
                <= 0.5,
        "axis priority preserves East range on width-only resize even after North was edited");

    const TrajectoryPlotMetrics before_height_resize = *component.trajectory_metrics();
    render_axis_priority(PlotAreaSize{1400.0, 650.0});
    const TrajectoryPlotMetrics after_height_resize = *component.trajectory_metrics();
    check(std::abs(after_height_resize.north.minimum - before_height_resize.north.minimum)
                < before_height_resize.north.length() * 0.001
            && std::abs(after_height_resize.north.maximum - before_height_resize.north.maximum)
                < before_height_resize.north.length() * 0.001,
        "axis priority preserves North range on height-only resize even after East was preserved");
    check(std::abs(
              after_height_resize.north_axis_length_px - before_height_resize.north_axis_length_px)
            > 0.5,
        "height-only widget resize changes the North axis pixel length");

    ImPlotComponent selected_panels;
    ImPlotComponentOptions selected_options;
    selected_panels.prepare(data, QualityFilter{}, selected_options);
    const auto render_selected_panels = [&selected_panels, &io]() {
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("selected time-series panels", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
            selected_panels.render_time_series("Selected time series", PlotAreaSize{1200.0, 600.0});
            ImGui::End();
            ImGui::Render();
        }
    };
    render_selected_panels();
    const std::optional<TimeRange> fitted_selection_time = selected_panels.time_series_time_range();
    const std::int64_t selection_time_span =
        fitted_selection_time->end - fitted_selection_time->start;
    const TimeRange custom_selection_time{
        GpsTime{fitted_selection_time->start.nanoseconds_since_gps_epoch + selection_time_span / 4},
        GpsTime{fitted_selection_time->end.nanoseconds_since_gps_epoch - selection_time_span / 4}};
    const NumericRange custom_east{-20.0, 20.0};
    const NumericRange custom_north{-30.0, 30.0};
    const NumericRange custom_up{-40.0, 40.0};
    const auto range_matches = [](NumericRange left, NumericRange right) {
        return std::abs(left.minimum - right.minimum) < 1.0e-9
            && std::abs(left.maximum - right.maximum) < 1.0e-9;
    };
    check(selected_panels.set_time_series_time_range(custom_selection_time)
            && selected_panels.set_time_series_position_range(PositionComponent::East, custom_east)
            && selected_panels.set_time_series_position_range(
                PositionComponent::North, custom_north)
            && selected_panels.set_time_series_position_range(PositionComponent::Up, custom_up),
        "time-series selection test accepts custom shared and component ranges");
    render_selected_panels();

    selected_options.show_east = false;
    selected_options.show_vertical = false;
    selected_panels.prepare(data, QualityFilter{}, selected_options, false);
    render_selected_panels();
    check(selected_panels.time_series_panel_count() == 1
            && selected_panels.time_series_metrics().front().component == PositionComponent::North
            && range_matches(selected_panels.time_series_metrics().front().position, custom_north)
            && selected_panels.time_series_time_range() == custom_selection_time,
        "3-to-1 selection preserves shared time and surviving North range by component identity");

    selected_options.show_east = true;
    selected_panels.prepare(data, QualityFilter{}, selected_options, false);
    render_selected_panels();
    const auto selected_east = std::find_if(selected_panels.time_series_metrics().begin(),
        selected_panels.time_series_metrics().end(), [](const TimeSeriesPanelMetrics& metrics) {
            return metrics.component == PositionComponent::East;
        });
    const auto selected_north = std::find_if(selected_panels.time_series_metrics().begin(),
        selected_panels.time_series_metrics().end(), [](const TimeSeriesPanelMetrics& metrics) {
            return metrics.component == PositionComponent::North;
        });
    const double panel_height_difference = selected_panels.time_series_metrics().size() == 2
        ? std::abs(selected_panels.time_series_metrics()[0].position_axis_length_px
              - selected_panels.time_series_metrics()[1].position_axis_length_px)
        : std::numeric_limits<double>::infinity();
    check(selected_panels.time_series_panel_count() == 2
            && selected_east != selected_panels.time_series_metrics().end()
            && selected_north != selected_panels.time_series_metrics().end()
            && range_matches(selected_north->position, custom_north)
            && !range_matches(selected_east->position, custom_east)
            && selected_panels.time_series_time_range() == custom_selection_time,
        "1-to-2 selection preserves the survivor and fits only the newly added East component");
    check(std::abs(selected_panels.time_series_widget_height_px() - 600.0) < 0.5
            && panel_height_difference < 2.0,
        "1-to-2 selection retains the 600 px subplot height and approximately equal panel rows");

    selected_options.show_vertical = true;
    selected_panels.prepare(data, QualityFilter{}, selected_options, false);
    render_selected_panels();
    selected_options.vertical_component = PositionComponent::EllipsoidalHeight;
    selected_panels.prepare(data, QualityFilter{}, selected_options, false);
    render_selected_panels();
    const auto selected_height = std::find_if(selected_panels.time_series_metrics().begin(),
        selected_panels.time_series_metrics().end(), [](const TimeSeriesPanelMetrics& metrics) {
            return metrics.component == PositionComponent::EllipsoidalHeight;
        });
    const auto preserved_north = std::find_if(selected_panels.time_series_metrics().begin(),
        selected_panels.time_series_metrics().end(), [](const TimeSeriesPanelMetrics& metrics) {
            return metrics.component == PositionComponent::North;
        });
    check(selected_height != selected_panels.time_series_metrics().end()
            && preserved_north != selected_panels.time_series_metrics().end()
            && range_matches(preserved_north->position, custom_north)
            && !range_matches(selected_height->position, custom_up)
            && selected_panels.time_series_time_range() == custom_selection_time,
        "vertical replacement fits only Height while preserving shared time and surviving ranges");

    selected_options.show_east = false;
    selected_options.show_north = false;
    selected_options.show_vertical = false;
    selected_panels.prepare(data, QualityFilter{}, selected_options, false);
    render_selected_panels();
    selected_options.show_north = true;
    selected_panels.prepare(data, QualityFilter{}, selected_options, false);
    render_selected_panels();
    check(selected_panels.time_series_panel_count() == 1
            && selected_panels.time_series_time_range() == custom_selection_time,
        "shared time range survives a one-to-zero-to-one component selection interval");

    ImPlotComponent auto_height_panels;
    ImPlotComponentOptions auto_height_options;
    auto_height_panels.prepare(data, QualityFilter{}, auto_height_options);
    const auto render_auto_height_panels = [&auto_height_panels, &io]() {
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("auto-height time-series parent", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
            ImGui::BeginChild("fixed time-series parent", ImVec2{1200.0F, 800.0F}, false);
            auto_height_panels.render_time_series(
                "Auto-height time series", PlotAreaSize{-1.0, -1.0});
            ImGui::EndChild();
            ImGui::End();
            ImGui::Render();
        }
    };
    render_auto_height_panels();
    const double three_component_widget_height = auto_height_panels.time_series_widget_height_px();
    auto_height_options.show_east = false;
    auto_height_options.show_vertical = false;
    auto_height_panels.prepare(data, QualityFilter{}, auto_height_options, false);
    render_auto_height_panels();
    const double one_component_widget_height = auto_height_panels.time_series_widget_height_px();
    auto_height_options.show_east = true;
    auto_height_panels.prepare(data, QualityFilter{}, auto_height_options, false);
    render_auto_height_panels();
    const double two_component_widget_height = auto_height_panels.time_series_widget_height_px();
    check(std::abs(three_component_widget_height - one_component_widget_height) < 0.5
            && std::abs(three_component_widget_height - two_component_widget_height) < 0.5,
        "canonical visible and hidden control rows keep auto-height subplot constant across "
        "3-to-1-to-2 selection");

    const auto prepare_ms =
        std::chrono::duration<double, std::milli>(prepare_end - prepare_start).count();
    const auto render_ms =
        std::chrono::duration<double, std::milli>(render_end - render_start).count();
    std::cout << "plot performance: " << slot_count << " slots x " << samples_per_slot
              << " samples; prepare=" << prepare_ms << " ms, headless frame=" << render_ms
              << " ms\n";

    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return failures == 0 ? 0 : 1;
}
