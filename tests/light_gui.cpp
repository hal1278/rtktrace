#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <string>

#include "../src/light/gui.hpp"
#include "imgui.h"
#include "implot.h"
#include "plotcore/analysis/coordinates.hpp"

namespace {

bool check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

[[nodiscard]] plotcore::LoadedFile synthetic_file(std::size_t slot, std::size_t sample_count)
{
    using namespace plotcore;
    LoadedFile file{
        std::filesystem::path{"synthetic-" + std::to_string(slot) + ".pos"}, InputFormat::Pos};
    file.samples.reserve(sample_count);
    constexpr double radians_per_degree = std::numbers::pi / 180.0;
    for (std::size_t index = 0; index < sample_count; ++index) {
        const Wgs84Llh llh{
            (35.0 + static_cast<double>(index) * 1.0e-7) * radians_per_degree,
            (139.0 + static_cast<double>(slot) * 1.0e-5 + static_cast<double>(index) * 5.0e-8)
                * radians_per_degree,
            10.0 + static_cast<double>(slot) * 0.2 + static_cast<double>(index) * 1.0e-4,
        };
        file.samples.push_back(NormalizedSample{
            .time = GpsTime{1'400'000'000'000'000'000LL
                + static_cast<std::int64_t>(index) * 100'000'000},
            .llh = llh,
            .ecef = wgs84_llh_to_ecef(llh),
            .enu = {},
            .quality = static_cast<SolutionQuality>(index % 6 + 1),
            .source_line_number = index + 1,
            .continuous_from_previous = index != 0,
        });
    }
    static_cast<void>(file.set_estimated_hz(10.0));
    return file;
}

} // namespace

int main()
{
    using namespace plotcore;
    constexpr std::size_t slot_count = 4;
    constexpr std::size_t samples_per_slot = 10'000;

    const TrajectoryPlotMetrics resize_metrics{
        NumericRange{-250.0, 250.0}, NumericRange{-150.0, 150.0}, 1.0, 500.0, 300.0};
    const TrajectoryResizeRequest resize_request{NumericRange{-310.0, 310.0},
        NumericRange{-125.0, 125.0}, 620.0, 250.0, 1.0, TrajectoryResizeFixedTarget::DisplayScale};
    detail::TrajectoryResizeController resize_controller;
    const detail::TrajectoryWindowResize resize = detail::trajectory_window_resize(
        1000, 700, 1600, 1200, resize_metrics, resize_request, resize_controller);
    bool resize_helpers_ok = check(resize.width == 1120 && resize.height == 650
            && !resize.axis_size_satisfied && !resize.east_constrained && !resize.north_constrained,
        "trajectory resize compensates both outer-window dimensions from measured axis pixels");

    for (const double response : std::array{0.15, 0.5, 0.85}) {
        detail::TrajectoryResizeController split_controller;
        split_controller.east.response = response;
        const detail::TrajectoryWindowResize split_resize = detail::trajectory_window_resize(
            1000, 700, 3000, 1200, resize_metrics, resize_request, split_controller);
        const double resulting_east_axis =
            resize_metrics.east_axis_length_px + response * (split_resize.width - 1000);
        resize_helpers_ok =
            check(std::abs(resulting_east_axis - resize_request.desired_east_axis_length_px) <= 1.0,
                "trajectory resize compensates a non-unit Both-pane response")
            && resize_helpers_ok;
    }

    const TrajectoryResizeRequest constrained_request{NumericRange{-200.0, 200.0},
        NumericRange{-100.0, 100.0}, 400.0, 200.0, 1.0, TrajectoryResizeFixedTarget::AxisRange};
    detail::TrajectoryResizeController constrained_controller;
    const detail::TrajectoryWindowResize constrained =
        detail::trajectory_window_resize(light_minimum_window_width, light_minimum_window_height,
            1600, 1200, resize_metrics, constrained_request, constrained_controller);
    resize_helpers_ok =
        check(constrained.width == light_minimum_window_width
                && constrained.height == light_minimum_window_height && constrained.east_constrained
                && constrained.north_constrained,
            "trajectory resize reports a minimum-window constraint without violating 800x600")
        && resize_helpers_ok;

    TrajectoryResizeRequest satisfied_request = resize_request;
    satisfied_request.desired_east_axis_length_px = 500.5;
    satisfied_request.desired_north_axis_length_px = 299.5;
    detail::TrajectoryResizeController satisfied_controller;
    const detail::TrajectoryWindowResize satisfied = detail::trajectory_window_resize(
        1000, 700, 1600, 1200, resize_metrics, satisfied_request, satisfied_controller);
    resize_helpers_ok =
        check(satisfied.width == 1000 && satisfied.height == 700 && satisfied.axis_size_satisfied
                && !satisfied.east_constrained && !satisfied.north_constrained,
            "trajectory resize accepts subpixel layout feedback without oscillating")
        && resize_helpers_ok;

    detail::TrajectoryResizeController delayed_controller;
    delayed_controller.east.response = 0.5;
    const detail::TrajectoryWindowResize delayed_first = detail::trajectory_window_resize(
        1000, 700, 3000, 1200, resize_metrics, resize_request, delayed_controller);
    TrajectoryPlotMetrics zero_response_metrics = resize_metrics;
    const detail::TrajectoryWindowResize delayed_probe =
        detail::trajectory_window_resize(delayed_first.width, 650, 3000, 1200,
            zero_response_metrics, resize_request, delayed_controller);
    TrajectoryPlotMetrics delayed_positive_metrics = resize_metrics;
    delayed_positive_metrics.east_axis_length_px = 620.0;
    delayed_positive_metrics.north_axis_length_px = 250.0;
    const detail::TrajectoryWindowResize delayed_complete =
        detail::trajectory_window_resize(delayed_probe.width, delayed_probe.height, 3000, 1200,
            delayed_positive_metrics, resize_request, delayed_controller);
    resize_helpers_ok =
        check(delayed_probe.width > delayed_first.width && !delayed_probe.east_constrained
                && !delayed_probe.window_manager_no_progress
                && delayed_complete.axis_size_satisfied,
            "trajectory resize probes after zero response and converges after positive progress")
        && resize_helpers_ok;

    TrajectoryResizeRequest east_only_request = resize_request;
    east_only_request.desired_north_axis_length_px = resize_metrics.north_axis_length_px;
    detail::TrajectoryResizeController step_controller;
    detail::TrajectoryWindowResize step_result{};
    int step_width = 1000;
    for (int step = 0; step < 5 && !step_result.controller_termination; ++step) {
        step_result = detail::trajectory_window_resize(
            step_width, 700, 5000, 1200, resize_metrics, east_only_request, step_controller);
        step_width = step_result.width;
    }
    resize_helpers_ok =
        check(step_result.controller_termination && !step_result.east_constrained
                && !step_result.window_manager_no_progress,
            "trajectory controller terminates repeated non-decreasing error while the window moves")
        && resize_helpers_ok;

    TrajectoryResizeRequest subpixel_progress_request = east_only_request;
    subpixel_progress_request.desired_east_axis_length_px = 510.0;
    detail::TrajectoryResizeController subpixel_controller;
    TrajectoryPlotMetrics subpixel_metrics = resize_metrics;
    detail::TrajectoryWindowResize subpixel_result{};
    int subpixel_width = 1000;
    for (int step = 0; step < 8; ++step) {
        subpixel_result = detail::trajectory_window_resize(subpixel_width, 700, 100000, 1200,
            subpixel_metrics, subpixel_progress_request, subpixel_controller);
        subpixel_width = subpixel_result.width;
        subpixel_metrics.east_axis_length_px += 0.1;
    }
    resize_helpers_ok =
        check(!subpixel_result.controller_termination
                && subpixel_controller.non_decreasing_error_observations == 0,
            "subpixel monotonic error reduction resets trajectory stagnation detection")
        && resize_helpers_ok;

    detail::TrajectoryResizeController asymmetric_controller;
    const detail::TrajectoryWindowResize asymmetric_first = detail::trajectory_window_resize(
        1000, 700, 3000, 1200, resize_metrics, resize_request, asymmetric_controller);
    TrajectoryPlotMetrics east_progress_metrics = resize_metrics;
    east_progress_metrics.east_axis_length_px = 560.0;
    const detail::TrajectoryWindowResize asymmetric_second =
        detail::trajectory_window_resize(asymmetric_first.width, 700, 3000, 1200,
            east_progress_metrics, resize_request, asymmetric_controller);
    east_progress_metrics.east_axis_length_px = 620.0;
    const detail::TrajectoryWindowResize asymmetric_third =
        detail::trajectory_window_resize(asymmetric_second.width, 700, 3000, 1200,
            east_progress_metrics, resize_request, asymmetric_controller);
    resize_helpers_ok =
        check(asymmetric_third.window_manager_no_progress && !asymmetric_third.east_constrained
                && !asymmetric_third.north_constrained,
            "one-axis progress and other-axis refusal terminates through the rollback trigger")
        && resize_helpers_ok;

    const TrajectoryPlotMetrics boundary_metrics{
        NumericRange{-450.0, 450.0}, NumericRange{-150.0, 150.0}, 1.5, 600.0, 300.0};
    const TrajectoryResizeRequest boundary_request{boundary_metrics.east, boundary_metrics.north,
        900.0, 300.0, 1.0, TrajectoryResizeFixedTarget::AxisRange};
    detail::TrajectoryResizeController boundary_controller;
    const std::optional<TrajectoryResizeRequest> feasible = detail::feasible_axis_range_request(
        1600, 700, 1600, 1200, boundary_metrics, boundary_request, boundary_controller);
    resize_helpers_ok =
        check(feasible.has_value() && feasible->east.minimum == -450.0
                && feasible->east.maximum == 450.0 && feasible->north.minimum == -150.0
                && feasible->north.maximum == 150.0
                && std::abs(feasible->desired_east_axis_length_px * feasible->meters_per_pixel
                       - feasible->east.length())
                    < 1.0e-9
                && std::abs(feasible->desired_north_axis_length_px * feasible->meters_per_pixel
                       - feasible->north.length())
                    < 1.0e-9,
            "axis-range constraint selects a feasible common scale without changing either range")
        && resize_helpers_ok;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{1600.0F, 1200.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    LightGui gui;
    const auto setup_start = std::chrono::steady_clock::now();
    bool loaded = true;
    for (std::size_t slot = 1; slot <= slot_count; ++slot) {
        loaded = gui.add_loaded_file(synthetic_file(slot, samples_per_slot)) && loaded;
    }
    const auto setup_end = std::chrono::steady_clock::now();

    ImGui::NewFrame();
    const auto frame_start = std::chrono::steady_clock::now();
    gui.render(nullptr);
    const auto frame_end = std::chrono::steady_clock::now();
    ImGui::Render();

    const double setup_ms =
        std::chrono::duration<double, std::milli>(setup_end - setup_start).count();
    const double frame_ms =
        std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
    std::cout << "plotcore light performance: " << slot_count << " slots x " << samples_per_slot
              << " samples; pipeline=" << setup_ms << " ms, headless frame=" << frame_ms << " ms\n";

    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return loaded && resize_helpers_ok ? 0 : 1;
}
