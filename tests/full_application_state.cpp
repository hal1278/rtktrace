#include <cstdint>
#include <iostream>
#include <string_view>

#include "plotcore/full/application_state.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

} // namespace

int main()
{
    using namespace plotcore;
    FullApplicationState state;
    const auto normal_trajectory = state.create_plot(PlotType::NormalTrajectory);
    const auto normal_time = state.create_plot(PlotType::NormalTimeSeries);
    const auto relative_trajectory = state.create_plot(PlotType::RelativeTrajectory);
    const auto relative_time = state.create_plot(PlotType::RelativeTimeSeries);
    check(normal_trajectory.has_value() && normal_time.has_value()
            && relative_trajectory.has_value() && relative_time.has_value()
            && normal_trajectory->value == 1 && relative_time->value == 4,
        "full state creates all four plot types with monotonic IDs");
    check(state.plots().size() == 4
            && state.find_plot(*relative_trajectory)->title
                == "Reference Trajectory 3",
        "plot instances have stable default titles");

    check(state.set_plot_visible(*normal_time, false)
            && !state.find_plot(*normal_time)->visible
            && state.set_plot_title(*normal_time, "Receiver time series")
            && state.find_plot(*normal_time)->title == "Receiver time series",
        "plot visibility and title are independently mutable");
    check(state.erase_plot(*normal_trajectory)
            && state.find_plot(*normal_trajectory) == nullptr
            && state.create_plot(PlotType::NormalTrajectory)->value == 5,
        "deleted plot IDs are not reused");
    check(!state.erase_plot(PlotWindowId{999})
            && !state.set_plot_visible(PlotWindowId{999}, true)
            && !state.set_plot_title(*relative_time, ""),
        "unknown plot IDs and empty titles are rejected");

    const std::uint64_t filter_revision = state.quality_filter_revision();
    check(state.set_quality_visible(SolutionQuality::Fixed, false)
            && !state.quality_filter().visible[1]
            && state.quality_filter_revision() == filter_revision + 1
            && state.set_quality_visible(SolutionQuality::Fixed, false)
            && state.quality_filter_revision() == filter_revision + 1,
        "shared quality filter revision changes only when its value changes");
    check(&state.session() == &static_cast<const FullApplicationState&>(state).session(),
        "mutable and const access share one plot session");

    return failures == 0 ? 0 : 1;
}
