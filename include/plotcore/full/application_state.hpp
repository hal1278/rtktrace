#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "plotcore/session_state.hpp"

namespace plotcore {

struct PlotWindowId {
    std::uint64_t value;

    auto operator<=>(const PlotWindowId&) const = default;
};

enum class PlotType : std::uint8_t {
    NormalTrajectory,
    NormalTimeSeries,
    RelativeTrajectory,
    RelativeTimeSeries,
};

struct PlotInstanceState {
    PlotWindowId id;
    PlotType type;
    std::string title;
    bool visible{true};
};

class FullApplicationState {
public:
    [[nodiscard]] std::optional<PlotWindowId> create_plot(PlotType type);
    [[nodiscard]] bool set_plot_visible(PlotWindowId id, bool visible) noexcept;
    [[nodiscard]] bool set_plot_title(PlotWindowId id, std::string title);
    [[nodiscard]] bool erase_plot(PlotWindowId id) noexcept;

    [[nodiscard]] PlotInstanceState* find_plot(PlotWindowId id) noexcept;
    [[nodiscard]] const PlotInstanceState* find_plot(PlotWindowId id) const noexcept;
    [[nodiscard]] const std::vector<PlotInstanceState>& plots() const noexcept;

    [[nodiscard]] bool set_quality_visible(SolutionQuality quality, bool visible) noexcept;
    [[nodiscard]] const QualityFilter& quality_filter() const noexcept;
    [[nodiscard]] std::uint64_t quality_filter_revision() const noexcept;

    [[nodiscard]] PlotSessionState& session() noexcept;
    [[nodiscard]] const PlotSessionState& session() const noexcept;

private:
    PlotSessionState session_;
    QualityFilter quality_filter_;
    std::vector<PlotInstanceState> plots_;
    std::uint64_t next_plot_id_{1};
    std::uint64_t quality_filter_revision_{0};
    bool plot_ids_exhausted_{false};
};

[[nodiscard]] const char* plot_type_name(PlotType type) noexcept;

} // namespace plotcore
