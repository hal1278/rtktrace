#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

#include "../src/full/gui_runtime.hpp"
#include "rtktrace/io/pos_parser.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] rtktrace::LoadedFile parsed_file(std::string_view name, double longitude_offset)
{
    std::ostringstream text;
    text << "0 0.0 35.0 " << 139.0 + longitude_offset << " 10.0 1\n"
         << "0 1.0 35.0 " << 139.0 + longitude_offset << " 11.0 2\n";
    std::istringstream input{text.str()};
    return rtktrace::parse_pos(input, std::filesystem::path{name});
}

} // namespace

int main()
{
    using namespace rtktrace;

    FullApplicationState state;
    check(state.session().add_loaded_file(parsed_file("one.pos", 0.0))
            && state.session().add_loaded_file(parsed_file("two.pos", 0.00001)),
        "full runtime test data enters the single shared session");

    const PlotWindowId first =
        *state.create_plot(PlotType::NormalTrajectory);
    const PlotWindowId second =
        *state.create_plot(PlotType::NormalTrajectory);
    const PlotWindowId relative_trajectory =
        *state.create_plot(PlotType::RelativeTrajectory);
    const PlotWindowId relative_time_series =
        *state.create_plot(PlotType::RelativeTimeSeries);
    const PlotWindowId initially_hidden =
        *state.create_plot(PlotType::NormalTimeSeries);
    static_cast<void>(state.set_plot_visible(initially_hidden, false));

    FullGuiRuntime runtime;
    ImPlotComponentOptions options;
    std::vector<PlotWindowId> rendered;
    const ImPlotComponent* first_component = nullptr;
    const ImPlotComponent* second_component = nullptr;
    PlotDataKind relative_trajectory_kind = PlotDataKind::Normal;
    PlotDataKind relative_time_series_kind = PlotDataKind::Normal;
    runtime.render_visible(state, options, 1,
        [&](const PlotInstanceState& plot, ImPlotComponent& component) {
            rendered.push_back(plot.id);
            if (plot.id == first) {
                first_component = &component;
            } else if (plot.id == second) {
                second_component = &component;
            } else if (plot.id == relative_trajectory) {
                relative_trajectory_kind = component.data_kind();
            } else if (plot.id == relative_time_series) {
                relative_time_series_kind = component.data_kind();
            }
        });

    const auto first_initial = runtime.runtime_info(first);
    const auto second_initial = runtime.runtime_info(second);
    const auto hidden_initial = runtime.runtime_info(initially_hidden);
    check(runtime.runtime_count() == 5
            && rendered
                == std::vector{first, second, relative_trajectory, relative_time_series},
        "only visible plot instances enter the rendering composition");
    check(first_component != nullptr && second_component != nullptr
            && first_component != second_component,
        "same-type plot instances own independent ImPlot components");
    check(relative_trajectory_kind == PlotDataKind::Relative
            && relative_time_series_kind == PlotDataKind::Relative,
        "both relative plot types prepare from the shared relative data view");
    check(first_initial.has_value() && second_initial.has_value()
            && first_initial->prepare_count == 1 && second_initial->prepare_count == 1
            && first_initial->last_prepare_fit_axes && second_initial->last_prepare_fit_axes,
        "new visible instances prepare once with an initial fit");
    check(hidden_initial.has_value() && !hidden_initial->synchronized
            && hidden_initial->prepare_count == 0,
        "hidden instances retain a runtime without preparing");

    rendered.clear();
    static_cast<void>(state.set_plot_visible(second, false));
    const std::uint64_t old_session_revision = state.session().revision();
    check(state.session().set_file_visible(1, false)
            && state.session().revision() > old_session_revision,
        "shared session mutation advances the revision");
    runtime.render_visible(state, options, 1,
        [&](const PlotInstanceState& plot, ImPlotComponent&) { rendered.push_back(plot.id); });
    const auto first_after_session = runtime.runtime_info(first);
    const auto second_while_hidden = runtime.runtime_info(second);
    check(rendered == std::vector{first, relative_trajectory, relative_time_series}
            && first_after_session->prepare_count == 2
            && !first_after_session->last_prepare_fit_axes
            && second_while_hidden->prepare_count == 1,
        "only visible stale instances prepare and existing view state is not fitted again");

    rendered.clear();
    static_cast<void>(state.set_plot_visible(second, true));
    runtime.render_visible(state, options, 1,
        [&](const PlotInstanceState& plot, ImPlotComponent&) { rendered.push_back(plot.id); });
    const auto first_unchanged = runtime.runtime_info(first);
    const auto second_reshown = runtime.runtime_info(second);
    check(rendered == std::vector{first, second, relative_trajectory, relative_time_series}
            && first_unchanged->prepare_count == 2
            && second_reshown->prepare_count == 2
            && !second_reshown->last_prepare_fit_axes,
        "reshown stale instance prepares lazily without refitting retained view state");

    const std::uint64_t session_revision_before_filter = state.session().revision();
    const std::uint64_t filter_revision = state.quality_filter_revision();
    check(state.set_quality_visible(SolutionQuality::Fixed, false)
            && state.session().revision() == session_revision_before_filter
            && state.quality_filter_revision() == filter_revision + 1,
        "quality filter is shared independently of the session revision");
    runtime.render_visible(state, options, 1, {});
    const auto first_after_filter = runtime.runtime_info(first);
    const auto second_after_filter = runtime.runtime_info(second);
    const auto hidden_after_filter = runtime.runtime_info(initially_hidden);
    check(first_after_filter->prepare_count == 3 && second_after_filter->prepare_count == 3
            && hidden_after_filter->prepare_count == 0,
        "shared quality-filter revision invalidates visible instances only");

    std::size_t hidden_visible_samples = 0;
    static_cast<void>(state.set_plot_visible(initially_hidden, true));
    runtime.render_visible(state, options, 1,
        [&](const PlotInstanceState& plot, ImPlotComponent& component) {
            if (plot.id == initially_hidden) {
                hidden_visible_samples = component.visible_sample_count();
            }
        });
    const auto hidden_reshown = runtime.runtime_info(initially_hidden);
    check(hidden_reshown->prepare_count == 1 && hidden_reshown->last_prepare_fit_axes
            && hidden_reshown->prepared_quality_filter_revision
                == state.quality_filter_revision()
            && hidden_visible_samples == 1,
        "first display prepares against the latest shared filter and fits once");

    options.trajectory_fit_ratio = 0.75;
    options.time_series_fit_ratio = 0.8;
    options.zoom_center_modifier = ImGuiMod_Shift;
    options.window_resize_modifier = ImGuiMod_Ctrl;
    options.batch.slot_order = SlotDrawingOrder::SmallerSlotInFront;
    options.batch.quality_order = QualityDrawingOrder::LowerQualityInFront;
    options.marker_size_px = 6.0F;
    static_cast<void>(state.set_plot_visible(second, false));
    runtime.render_visible(state, options, 2, {});
    const auto first_after_shared_options = runtime.runtime_info(first);
    const auto second_after_shared_options = runtime.runtime_info(second);
    const auto first_shared_options = runtime.plot_options(first);
    const auto second_shared_options = runtime.plot_options(second);
    check(first_after_shared_options->prepare_count == 4
            && second_after_shared_options->prepare_count == 3
            && second_after_shared_options->prepared_options_revision == 1,
        "shared rendering Options reprepare visible components but leave hidden components stale");
    check(first_shared_options->trajectory_fit_ratio == 0.75
            && first_shared_options->time_series_fit_ratio == 0.8
            && first_shared_options->zoom_center_modifier == ImGuiMod_Shift
            && first_shared_options->window_resize_modifier == ImGuiMod_Ctrl
            && first_shared_options->batch.slot_order
                == SlotDrawingOrder::SmallerSlotInFront
            && first_shared_options->batch.quality_order
                == QualityDrawingOrder::LowerQualityInFront
            && first_shared_options->marker_size_px == 2.0F
            && second_shared_options->marker_size_px == 2.0F,
        "shared Fit ratios, modifiers, and drawing order propagate while existing point sizes "
        "remain unchanged");
    static_cast<void>(state.set_plot_visible(second, true));
    runtime.render_visible(state, options, 2, {});
    const auto second_after_shared_reshow = runtime.runtime_info(second);
    check(second_after_shared_reshow->prepare_count == 4
            && second_after_shared_reshow->prepared_options_revision == 2
            && !second_after_shared_reshow->last_prepare_fit_axes,
        "reshow applies shared Options lazily without fitting retained view state");

    check(runtime.set_current_point_size(first, 4.0F)
            && runtime.set_draw_mode(first, DrawMode::Point)
            && runtime.set_time_series_components(
                initially_hidden, false, true, false)
            && runtime.set_vertical_component(
                initially_hidden, PositionComponent::EllipsoidalHeight),
        "instance-local plot toolbar options accept valid updates");
    check(!runtime.set_current_point_size(first, 0.0F)
            && !runtime.set_draw_mode(first, static_cast<DrawMode>(99))
            && !runtime.set_vertical_component(first, PositionComponent::East)
            && !runtime.set_current_point_size(PlotWindowId{999}, 2.0F),
        "instance-local plot toolbar options reject invalid values and unknown IDs");
    runtime.render_visible(state, options, 2, {});
    const auto first_after_local_options = runtime.runtime_info(first);
    const auto second_after_local_options = runtime.runtime_info(second);
    const auto hidden_after_local_options = runtime.runtime_info(initially_hidden);
    const auto first_local_options = runtime.plot_options(first);
    const auto second_local_options = runtime.plot_options(second);
    const auto time_series_local_options = runtime.plot_options(initially_hidden);
    check(first_after_local_options->prepare_count == 5
            && second_after_local_options->prepare_count == 4
            && hidden_after_local_options->prepare_count == 3
            && !first_after_local_options->last_prepare_fit_axes
            && !hidden_after_local_options->last_prepare_fit_axes,
        "only instances with changed local options reprepare without fitting");
    check(first_local_options->marker_size_px == 4.0F
            && first_local_options->batch.draw_mode == DrawMode::Point
            && second_local_options->marker_size_px == 2.0F
            && second_local_options->batch.draw_mode == DrawMode::LineAndPoint
            && !time_series_local_options->show_east
            && time_series_local_options->show_north
            && !time_series_local_options->show_vertical
            && time_series_local_options->vertical_component
                == PositionComponent::EllipsoidalHeight,
        "point size, draw mode, time-series components, and vertical component are independent");

    static_cast<void>(state.set_plot_visible(second, false));
    check(runtime.set_current_point_size(second, 5.0F)
            && runtime.set_draw_mode(second, DrawMode::Line),
        "hidden instance local options remain editable");
    runtime.render_visible(state, options, 2, {});
    check(runtime.runtime_info(second)->prepare_count == 4,
        "hidden instance local option changes do not prepare eagerly");
    static_cast<void>(state.set_plot_visible(second, true));
    runtime.render_visible(state, options, 2, {});
    const auto second_after_local_reshow = runtime.runtime_info(second);
    check(second_after_local_reshow->prepare_count == 5
            && !second_after_local_reshow->last_prepare_fit_axes
            && runtime.plot_options(second)->marker_size_px == 5.0F
            && runtime.plot_options(second)->batch.draw_mode == DrawMode::Line,
        "reshow applies hidden local changes lazily while preserving view state");

    options.marker_size_px = 8.0F;
    const PlotWindowId after_default_change =
        *state.create_plot(PlotType::NormalTrajectory);
    static_cast<void>(state.set_plot_visible(after_default_change, false));
    runtime.synchronize(state, options, 3);
    check(runtime.plot_options(after_default_change)->marker_size_px == 8.0F
            && !runtime.runtime_info(after_default_change)->synchronized,
        "creation-time runtime registration snapshots Options for a hidden new instance");

    options.marker_size_px = 9.0F;
    runtime.render_visible(state, options, 4, {});
    check(runtime.plot_options(after_default_change)->marker_size_px == 8.0F
            && runtime.runtime_info(after_default_change)->prepare_count == 0,
        "later default point-size change neither replaces the snapshot nor prepares hidden data");
    static_cast<void>(state.set_plot_visible(after_default_change, true));
    runtime.render_visible(state, options, 4, {});
    const auto new_options = runtime.plot_options(after_default_change);
    check(runtime.runtime_info(first)->prepare_count == 5
            && runtime.runtime_info(second)->prepare_count == 5
            && runtime.plot_options(first)->marker_size_px == 4.0F
            && runtime.plot_options(second)->marker_size_px == 5.0F
            && new_options.has_value() && new_options->marker_size_px == 8.0F
            && runtime.runtime_info(after_default_change)->last_prepare_fit_axes,
        "first render uses the creation-time point-size snapshot and fits the new instance");

    check(state.erase_plot(first), "plot instance deletion succeeds in application state");
    runtime.synchronize(state);
    check(runtime.runtime_count() == 5 && !runtime.runtime_info(first).has_value()
            && runtime.runtime_info(second).has_value(),
        "runtime synchronization destroys the component of an erased plot instance");

    return failures == 0 ? 0 : 1;
}
