#include "gui_runtime.hpp"

#include <optional>

namespace rtktrace {
namespace {

[[nodiscard]] bool uses_relative_data(PlotType type) noexcept
{
    return type == PlotType::RelativeTrajectory || type == PlotType::RelativeTimeSeries;
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

void FullGuiRuntime::render_visible(FullApplicationState& state,
    const ImPlotComponentOptions& options, std::uint64_t options_revision,
    const VisiblePlotRenderer& renderer)
{
    synchronize(state);

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
            || runtime.info.prepared_options_revision != options_revision;
        if (stale) {
            const std::optional<PlotDataView>& data = data_view_for(plot.type);
            if (data.has_value()) {
                const bool fit_axes = !runtime.info.data_prepared;
                runtime.component.prepare(*data, state.quality_filter(), options, fit_axes);
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
            runtime.info.prepared_options_revision = options_revision;
        }
        if (renderer) {
            renderer(plot, runtime.component);
        }
    }
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

} // namespace rtktrace
