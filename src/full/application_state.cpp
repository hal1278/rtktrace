#include "rtktrace/full/application_state.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace rtktrace {

const char* plot_type_name(PlotType type) noexcept
{
    switch (type) {
    case PlotType::NormalTrajectory:
        return "Normal Trajectory";
    case PlotType::NormalTimeSeries:
        return "Normal Time Series";
    case PlotType::RelativeTrajectory:
        return "Reference Trajectory";
    case PlotType::RelativeTimeSeries:
        return "Reference Time Series";
    }
    return "Plot";
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
