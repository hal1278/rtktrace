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

    // Prepare stale visible instances and pass only visible instances to the
    // application composition. The callback owns window placement and sizing.
    void render_visible(FullApplicationState& state, const ImPlotComponentOptions& options,
        std::uint64_t options_revision, const VisiblePlotRenderer& renderer);

    [[nodiscard]] std::size_t runtime_count() const noexcept;
    [[nodiscard]] std::optional<FullPlotRuntimeInfo> runtime_info(PlotWindowId id) const noexcept;

private:
    struct PlotRuntime {
        explicit PlotRuntime(PlotType plot_type)
            : info{.type = plot_type}
        {
        }

        ImPlotComponent component;
        FullPlotRuntimeInfo info;
    };

    std::map<PlotWindowId, PlotRuntime> runtimes_;
};

} // namespace rtktrace
