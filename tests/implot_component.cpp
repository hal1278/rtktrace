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
                1'400'000'000'000'000'000LL
                    + static_cast<std::int64_t>(index) * 100'000'000,
                slot + 1, index));
        }
        data.series.push_back(PlotSeriesView{
            slot + 1, true, std::span<const NormalizedSample>{samples[slot]}});
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
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
    ImGui::Begin("plot performance", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    const auto render_start = std::chrono::steady_clock::now();
    component.render_trajectory("Trajectory", PlotAreaSize{1500.0, 400.0});
    component.render_time_series("Time series", PlotAreaSize{1500.0, 700.0});
    const auto render_end = std::chrono::steady_clock::now();
    ImGui::End();
    ImGui::Render();

    check(component.trajectory_metrics().has_value()
            && component.trajectory_metrics()->meters_per_pixel > 0.0,
        "trajectory rendering reports range, axis length, and meters per pixel");
    check(component.time_series_metrics().size() == 3,
        "normal time-series rendering produces linked East, North, and vertical panels");

    const double requested_scale = component.trajectory_metrics()->meters_per_pixel * 2.0;
    const std::optional<TimeRange> rendered_time = component.time_series_time_range();
    check(component.set_trajectory_meters_per_pixel(requested_scale)
            && !component.set_trajectory_ranges(NumericRange{1.0, 0.0},
                NumericRange{0.0, 1.0}),
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
    ImGui::Begin("plot numeric ranges", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    component.render_trajectory("Trajectory", PlotAreaSize{1500.0, 400.0});
    component.render_time_series("Time series", PlotAreaSize{1500.0, 700.0});
    ImGui::End();
    ImGui::Render();
    check(component.trajectory_metrics().has_value()
            && std::abs(component.trajectory_metrics()->meters_per_pixel
                    - requested_scale)
                < requested_scale * 0.01,
        "numeric trajectory scale is applied on the next frame");

    const auto prepare_ms = std::chrono::duration<double, std::milli>(
        prepare_end - prepare_start).count();
    const auto render_ms = std::chrono::duration<double, std::milli>(
        render_end - render_start).count();
    std::cout << "plot performance: " << slot_count << " slots x "
              << samples_per_slot << " samples; prepare=" << prepare_ms
              << " ms, headless frame=" << render_ms << " ms\n";

    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return failures == 0 ? 0 : 1;
}
