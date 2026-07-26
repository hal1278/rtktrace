#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rtktrace/session_state.hpp"

namespace rtktrace {

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

enum class ApplicationWindow : std::uint8_t {
    FileSlots,
    SharedControls,
    WindowManager,
};

struct FloatingWindowPosition {
    float x_px;
    float y_px;

    auto operator<=>(const FloatingWindowPosition&) const = default;
};

struct FloatingWindowSize {
    float width_px;
    float height_px;

    auto operator<=>(const FloatingWindowSize&) const = default;
};

struct PlotInstanceState {
    PlotWindowId id;
    PlotType type;
    std::string title;
    bool visible{true};
    std::optional<FloatingWindowPosition> position;
    std::optional<FloatingWindowSize> size;
};

class FullApplicationState {
public:
    [[nodiscard]] bool application_window_visible(ApplicationWindow window) const noexcept;
    [[nodiscard]] bool set_application_window_visible(
        ApplicationWindow window, bool visible) noexcept;
    [[nodiscard]] bool toggle_application_window(ApplicationWindow window) noexcept;

    [[nodiscard]] std::optional<PlotWindowId> create_plot(PlotType type);
    [[nodiscard]] bool set_plot_visible(PlotWindowId id, bool visible) noexcept;
    [[nodiscard]] bool set_plot_title(PlotWindowId id, std::string title);
    [[nodiscard]] bool set_plot_position(
        PlotWindowId id, std::optional<FloatingWindowPosition> position) noexcept;
    [[nodiscard]] bool set_plot_size(
        PlotWindowId id, std::optional<FloatingWindowSize> size) noexcept;
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
    bool file_slots_visible_{true};
    bool shared_controls_visible_{true};
    bool window_manager_visible_{true};
    std::vector<PlotInstanceState> plots_;
    std::uint64_t next_plot_id_{1};
    std::uint64_t quality_filter_revision_{0};
    bool plot_ids_exhausted_{false};
};

[[nodiscard]] const char* plot_type_name(PlotType type) noexcept;

} // namespace rtktrace
