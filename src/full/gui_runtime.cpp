#include "gui_runtime.hpp"

#include <cmath>
#include <limits>
#include <optional>

namespace rtktrace {
namespace {

[[nodiscard]] bool uses_relative_data(PlotType type) noexcept
{
    return type == PlotType::RelativeTrajectory || type == PlotType::RelativeTimeSeries;
}

[[nodiscard]] bool valid_draw_mode(DrawMode mode) noexcept
{
    return mode == DrawMode::Line || mode == DrawMode::Point
        || mode == DrawMode::LineAndPoint;
}

[[nodiscard]] bool valid_vertical_component(PositionComponent component) noexcept
{
    return component == PositionComponent::Up
        || component == PositionComponent::EllipsoidalHeight;
}

template <typename Runtime>
void advance_options_revision(Runtime& runtime) noexcept
{
    if (runtime.info.instance_options_revision
        != std::numeric_limits<std::uint64_t>::max()) {
        ++runtime.info.instance_options_revision;
    }
    runtime.options_dirty = true;
}

template <typename Runtime>
[[nodiscard]] bool apply_shared_options(Runtime& runtime,
    const ImPlotComponentOptions& shared, std::uint64_t shared_revision) noexcept
{
    if (!runtime.options_initialized) {
        runtime.options = shared;
        runtime.options_initialized = true;
        runtime.observed_shared_options_revision = shared_revision;
        advance_options_revision(runtime);
        return true;
    }
    if (runtime.observed_shared_options_revision == shared_revision) {
        return false;
    }

    const bool changed =
        runtime.options.trajectory_fit_ratio != shared.trajectory_fit_ratio
        || runtime.options.time_series_fit_ratio != shared.time_series_fit_ratio
        || runtime.options.zoom_center_modifier != shared.zoom_center_modifier
        || runtime.options.window_resize_modifier != shared.window_resize_modifier
        || runtime.options.batch.slot_order != shared.batch.slot_order
        || runtime.options.batch.quality_order != shared.batch.quality_order;
    runtime.options.trajectory_fit_ratio = shared.trajectory_fit_ratio;
    runtime.options.time_series_fit_ratio = shared.time_series_fit_ratio;
    runtime.options.zoom_center_modifier = shared.zoom_center_modifier;
    runtime.options.window_resize_modifier = shared.window_resize_modifier;
    runtime.options.batch.slot_order = shared.batch.slot_order;
    runtime.options.batch.quality_order = shared.batch.quality_order;
    runtime.observed_shared_options_revision = shared_revision;
    if (changed) {
        advance_options_revision(runtime);
    }
    return changed;
}

} // namespace

void FullGuiRuntime::synchronize(const FullApplicationState& state)
{
    for (auto runtime = runtimes_.begin(); runtime != runtimes_.end();) {
        if (state.find_plot(runtime->first) == nullptr) {
            runtime = runtimes_.erase(runtime);
        } else {
            ++runtime;
        }
    }

    for (const PlotInstanceState& plot : state.plots()) {
        static_cast<void>(runtimes_.try_emplace(plot.id, plot.type));
    }
}

void FullGuiRuntime::synchronize(const FullApplicationState& state,
    const ImPlotComponentOptions& options, std::uint64_t options_revision)
{
    synchronize(state);
    for (auto& [id, runtime] : runtimes_) {
        static_cast<void>(id);
        static_cast<void>(apply_shared_options(runtime, options, options_revision));
    }
}

void FullGuiRuntime::render_visible(FullApplicationState& state,
    const ImPlotComponentOptions& options, std::uint64_t options_revision,
    const VisiblePlotRenderer& renderer)
{
    synchronize(state, options, options_revision);

    const std::uint64_t session_revision = state.session().revision();
    const std::uint64_t quality_filter_revision = state.quality_filter_revision();
    bool normal_view_loaded = false;
    bool relative_view_loaded = false;
    std::optional<PlotDataView> normal_view;
    std::optional<PlotDataView> relative_view;

    const auto data_view_for =
        [&](PlotType type) -> const std::optional<PlotDataView>& {
        if (uses_relative_data(type)) {
            if (!relative_view_loaded) {
                relative_view = state.session().relative_plot_data_view();
                relative_view_loaded = true;
            }
            return relative_view;
        }
        if (!normal_view_loaded) {
            normal_view = state.session().normal_plot_data_view();
            normal_view_loaded = true;
        }
        return normal_view;
    };

    for (const PlotInstanceState& plot : state.plots()) {
        if (!plot.visible) {
            continue;
        }
        const auto runtime_position = runtimes_.find(plot.id);
        if (runtime_position == runtimes_.end()) {
            continue;
        }
        PlotRuntime& runtime = runtime_position->second;
        const bool stale = !runtime.info.synchronized
            || runtime.info.prepared_session_revision != session_revision
            || runtime.info.prepared_quality_filter_revision != quality_filter_revision
            || runtime.options_dirty;
        if (stale) {
            const std::optional<PlotDataView>& data = data_view_for(plot.type);
            if (data.has_value()) {
                const bool fit_axes = !runtime.info.data_prepared;
                runtime.component.prepare(
                    *data, state.quality_filter(), runtime.options, fit_axes);
                runtime.info.data_prepared = true;
                ++runtime.info.prepare_count;
                runtime.info.last_prepare_fit_axes = fit_axes;
            } else {
                runtime.component.clear();
                runtime.info.data_prepared = false;
                runtime.info.last_prepare_fit_axes = false;
            }
            runtime.info.synchronized = true;
            runtime.info.prepared_session_revision = session_revision;
            runtime.info.prepared_quality_filter_revision = quality_filter_revision;
            runtime.info.prepared_options_revision =
                runtime.observed_shared_options_revision;
            runtime.info.prepared_instance_options_revision =
                runtime.info.instance_options_revision;
            runtime.options_dirty = false;
        }
        if (renderer) {
            renderer(plot, runtime.component);
        }
    }
}

bool FullGuiRuntime::set_current_point_size(PlotWindowId id, float point_size_px) noexcept
{
    const auto runtime = runtimes_.find(id);
    if (runtime == runtimes_.end() || !runtime->second.options_initialized
        || !std::isfinite(point_size_px) || point_size_px < 1.0F) {
        return false;
    }
    if (runtime->second.options.marker_size_px != point_size_px) {
        runtime->second.options.marker_size_px = point_size_px;
        advance_options_revision(runtime->second);
    }
    return true;
}

bool FullGuiRuntime::set_draw_mode(PlotWindowId id, DrawMode mode) noexcept
{
    const auto runtime = runtimes_.find(id);
    if (runtime == runtimes_.end() || !runtime->second.options_initialized
        || !valid_draw_mode(mode)) {
        return false;
    }
    if (runtime->second.options.batch.draw_mode != mode) {
        runtime->second.options.batch.draw_mode = mode;
        advance_options_revision(runtime->second);
    }
    return true;
}

bool FullGuiRuntime::set_time_series_components(
    PlotWindowId id, bool show_east, bool show_north, bool show_vertical) noexcept
{
    const auto runtime = runtimes_.find(id);
    if (runtime == runtimes_.end() || !runtime->second.options_initialized) {
        return false;
    }
    ImPlotComponentOptions& options = runtime->second.options;
    if (options.show_east != show_east || options.show_north != show_north
        || options.show_vertical != show_vertical) {
        options.show_east = show_east;
        options.show_north = show_north;
        options.show_vertical = show_vertical;
        advance_options_revision(runtime->second);
    }
    return true;
}

bool FullGuiRuntime::set_vertical_component(
    PlotWindowId id, PositionComponent component) noexcept
{
    const auto runtime = runtimes_.find(id);
    if (runtime == runtimes_.end() || !runtime->second.options_initialized
        || !valid_vertical_component(component)) {
        return false;
    }
    if (runtime->second.options.vertical_component != component) {
        runtime->second.options.vertical_component = component;
        advance_options_revision(runtime->second);
    }
    return true;
}

std::size_t FullGuiRuntime::runtime_count() const noexcept
{
    return runtimes_.size();
}

std::optional<FullPlotRuntimeInfo> FullGuiRuntime::runtime_info(PlotWindowId id) const noexcept
{
    const auto runtime = runtimes_.find(id);
    if (runtime == runtimes_.end()) {
        return std::nullopt;
    }
    return runtime->second.info;
}

std::optional<ImPlotComponentOptions> FullGuiRuntime::plot_options(
    PlotWindowId id) const noexcept
{
    const auto runtime = runtimes_.find(id);
    if (runtime == runtimes_.end() || !runtime->second.options_initialized) {
        return std::nullopt;
    }
    return runtime->second.options;
}

} // namespace rtktrace
