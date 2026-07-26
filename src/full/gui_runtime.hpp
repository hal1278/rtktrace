#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>

#include "rtktrace/full/application_state.hpp"
#include "rtktrace/plot/implot_component.hpp"

namespace rtktrace {

struct FullPlotRuntimeInfo {
    PlotType type;
    bool synchronized{false};
    bool data_prepared{false};
    std::uint64_t prepared_session_revision{0};
    std::uint64_t prepared_quality_filter_revision{0};
    std::uint64_t prepared_options_revision{0};
    std::uint64_t instance_options_revision{0};
    std::uint64_t prepared_instance_options_revision{0};
    std::size_t prepare_count{0};
    bool last_prepare_fit_axes{false};
};

class FullGuiRuntime {
public:
    using VisiblePlotRenderer =
        std::function<void(const PlotInstanceState&, ImPlotComponent&)>;

    // Reconcile component ownership with the backend-free application state.
    // Hidden instances retain their component; erased instances do not.
    void synchronize(const FullApplicationState& state);
    // Call immediately after plot creation to snapshot instance-local defaults
    // from the Options that were current at creation time.
    void synchronize(const FullApplicationState& state,
        const ImPlotComponentOptions& options, std::uint64_t options_revision);

    // Prepare stale visible instances and pass only visible instances to the
    // application composition. The callback owns window placement and sizing.
    void render_visible(FullApplicationState& state, const ImPlotComponentOptions& options,
        std::uint64_t options_revision, const VisiblePlotRenderer& renderer);

    [[nodiscard]] bool set_current_point_size(PlotWindowId id, float point_size_px) noexcept;
    [[nodiscard]] bool set_draw_mode(PlotWindowId id, DrawMode mode) noexcept;
    [[nodiscard]] bool set_time_series_components(
        PlotWindowId id, bool show_east, bool show_north, bool show_vertical) noexcept;
    [[nodiscard]] bool set_vertical_component(
        PlotWindowId id, PositionComponent component) noexcept;

    [[nodiscard]] std::size_t runtime_count() const noexcept;
    [[nodiscard]] std::optional<FullPlotRuntimeInfo> runtime_info(PlotWindowId id) const noexcept;
    [[nodiscard]] std::optional<ImPlotComponentOptions> plot_options(
        PlotWindowId id) const noexcept;

private:
    struct PlotRuntime {
        explicit PlotRuntime(PlotType plot_type)
            : info{.type = plot_type}
        {
        }

        ImPlotComponent component;
        ImPlotComponentOptions options;
        FullPlotRuntimeInfo info;
        std::uint64_t observed_shared_options_revision{0};
        bool options_initialized{false};
        bool options_dirty{true};
    };

    std::map<PlotWindowId, PlotRuntime> runtimes_;
};

} // namespace rtktrace
