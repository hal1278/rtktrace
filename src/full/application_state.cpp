#include "rtktrace/full/application_state.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace rtktrace {

const char* plot_type_name(PlotType type) noexcept
{
    switch (type) {
    case PlotType::NormalTrajectory:
        return "Normal 2D";
    case PlotType::NormalTimeSeries:
        return "Normal Time Series";
    case PlotType::RelativeTrajectory:
        return "Relative 2D";
    case PlotType::RelativeTimeSeries:
        return "Relative Time Series";
    }
    return "Plot";
}

bool FullApplicationState::application_window_visible(ApplicationWindow window) const noexcept
{
    switch (window) {
    case ApplicationWindow::FileSlots:
        return file_slots_visible_;
    case ApplicationWindow::SharedControls:
        return shared_controls_visible_;
    case ApplicationWindow::WindowManager:
        return window_manager_visible_;
    }
    return false;
}

bool FullApplicationState::set_application_window_visible(
    ApplicationWindow window, bool visible) noexcept
{
    switch (window) {
    case ApplicationWindow::FileSlots:
        file_slots_visible_ = visible;
        return true;
    case ApplicationWindow::SharedControls:
        shared_controls_visible_ = visible;
        return true;
    case ApplicationWindow::WindowManager:
        window_manager_visible_ = visible;
        return true;
    }
    return false;
}

bool FullApplicationState::toggle_application_window(ApplicationWindow window) noexcept
{
    switch (window) {
    case ApplicationWindow::FileSlots:
        file_slots_visible_ = !file_slots_visible_;
        return true;
    case ApplicationWindow::SharedControls:
        shared_controls_visible_ = !shared_controls_visible_;
        return true;
    case ApplicationWindow::WindowManager:
        window_manager_visible_ = !window_manager_visible_;
        return true;
    }
    return false;
}

std::optional<PlotWindowId> FullApplicationState::create_plot(PlotType type)
{
    if (plot_ids_exhausted_) {
        return std::nullopt;
    }
    const PlotWindowId id{next_plot_id_};
    if (next_plot_id_ == std::numeric_limits<std::uint64_t>::max()) {
        plot_ids_exhausted_ = true;
    } else {
        ++next_plot_id_;
    }
    plots_.push_back(PlotInstanceState{
        id,
        type,
        std::string{plot_type_name(type)} + " " + std::to_string(id.value),
        true,
        std::nullopt,
        std::nullopt,
    });
    return id;
}

bool FullApplicationState::set_plot_visible(PlotWindowId id, bool visible) noexcept
{
    PlotInstanceState* plot = find_plot(id);
    if (plot == nullptr) {
        return false;
    }
    plot->visible = visible;
    return true;
}

bool FullApplicationState::set_plot_title(PlotWindowId id, std::string title)
{
    PlotInstanceState* plot = find_plot(id);
    if (plot == nullptr || title.empty()) {
        return false;
    }
    plot->title = std::move(title);
    return true;
}

bool FullApplicationState::set_plot_position(
    PlotWindowId id, std::optional<FloatingWindowPosition> position) noexcept
{
    PlotInstanceState* plot = find_plot(id);
    if (plot == nullptr) {
        return false;
    }
    plot->position = position;
    return true;
}

bool FullApplicationState::set_plot_size(
    PlotWindowId id, std::optional<FloatingWindowSize> size) noexcept
{
    PlotInstanceState* plot = find_plot(id);
    if (plot == nullptr) {
        return false;
    }
    plot->size = size;
    return true;
}

bool FullApplicationState::erase_plot(PlotWindowId id) noexcept
{
    const auto plot = std::find_if(
        plots_.begin(), plots_.end(), [id](const auto& item) { return item.id == id; });
    if (plot == plots_.end()) {
        return false;
    }
    plots_.erase(plot);
    return true;
}

PlotInstanceState* FullApplicationState::find_plot(PlotWindowId id) noexcept
{
    const auto plot = std::find_if(
        plots_.begin(), plots_.end(), [id](const auto& item) { return item.id == id; });
    return plot == plots_.end() ? nullptr : &*plot;
}

const PlotInstanceState* FullApplicationState::find_plot(PlotWindowId id) const noexcept
{
    const auto plot = std::find_if(
        plots_.begin(), plots_.end(), [id](const auto& item) { return item.id == id; });
    return plot == plots_.end() ? nullptr : &*plot;
}

const std::vector<PlotInstanceState>& FullApplicationState::plots() const noexcept
{
    return plots_;
}

bool FullApplicationState::set_quality_visible(SolutionQuality quality, bool visible) noexcept
{
    const std::size_t index = static_cast<std::size_t>(quality);
    if (index >= quality_filter_.visible.size()) {
        return false;
    }
    if (quality_filter_.visible[index] != visible) {
        quality_filter_.visible[index] = visible;
        if (quality_filter_revision_ != std::numeric_limits<std::uint64_t>::max()) {
            ++quality_filter_revision_;
        }
    }
    return true;
}

const QualityFilter& FullApplicationState::quality_filter() const noexcept
{
    return quality_filter_;
}

std::uint64_t FullApplicationState::quality_filter_revision() const noexcept
{
    return quality_filter_revision_;
}

PlotSessionState& FullApplicationState::session() noexcept
{
    return session_;
}

const PlotSessionState& FullApplicationState::session() const noexcept
{
    return session_;
}

} // namespace rtktrace
