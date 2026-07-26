#include <cstdint>
#include <iostream>
#include <string_view>

#include "rtktrace/full/application_state.hpp"

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
    using namespace rtktrace;
    FullApplicationState state;
    check(state.application_window_visible(ApplicationWindow::FileSlots)
            && state.application_window_visible(ApplicationWindow::SharedControls)
            && state.application_window_visible(ApplicationWindow::WindowManager),
        "full application windows are visible at startup");
    check(state.toggle_application_window(ApplicationWindow::FileSlots)
            && !state.application_window_visible(ApplicationWindow::FileSlots)
            && state.set_application_window_visible(ApplicationWindow::FileSlots, true)
            && state.application_window_visible(ApplicationWindow::FileSlots)
            && state.set_application_window_visible(ApplicationWindow::SharedControls, false)
            && !state.application_window_visible(ApplicationWindow::SharedControls)
            && state.toggle_application_window(ApplicationWindow::WindowManager)
            && !state.application_window_visible(ApplicationWindow::WindowManager),
        "full application window visibility can be set and toggled independently");

    const auto normal_trajectory = state.create_plot(PlotType::NormalTrajectory);
    const auto normal_time = state.create_plot(PlotType::NormalTimeSeries);
    const auto relative_trajectory = state.create_plot(PlotType::RelativeTrajectory);
    const auto relative_time = state.create_plot(PlotType::RelativeTimeSeries);
    check(normal_trajectory.has_value() && normal_time.has_value()
            && relative_trajectory.has_value() && relative_time.has_value()
            && normal_trajectory->value == 1 && relative_time->value == 4,
        "full state creates all four plot types with monotonic IDs");
    check(state.plots().size() == 4
            && state.find_plot(*normal_trajectory)->title == "Normal 2D 1"
            && state.find_plot(*normal_time)->title == "Normal Time Series 2"
            && state.find_plot(*relative_trajectory)->title == "Relative 2D 3"
            && state.find_plot(*relative_time)->title == "Relative Time Series 4",
        "plot instances use the specified PlotType names in stable default titles");

    check(state.set_plot_visible(*normal_time, false) && !state.find_plot(*normal_time)->visible
            && state.set_plot_title(*normal_time, "Receiver time series")
            && state.find_plot(*normal_time)->title == "Receiver time series",
        "plot visibility and title are independently mutable");
    check(state.set_plot_position(
              *normal_time, FloatingWindowPosition{720.0F, 48.0F})
            && state.set_plot_size(*normal_time, FloatingWindowSize{480.0F, 320.0F})
            && state.find_plot(*normal_time)->position
                == FloatingWindowPosition{720.0F, 48.0F}
            && state.find_plot(*normal_time)->size == FloatingWindowSize{480.0F, 320.0F}
            && !state.find_plot(*normal_time)->visible
            && state.set_plot_visible(*normal_time, true)
            && state.find_plot(*normal_time)->position
                == FloatingWindowPosition{720.0F, 48.0F}
            && state.find_plot(*normal_time)->size == FloatingWindowSize{480.0F, 320.0F},
        "hidden plot instances retain optional floating position and size when redisplayed");
    check(state.set_plot_position(*normal_time, std::nullopt)
            && state.set_plot_size(*normal_time, std::nullopt)
            && !state.find_plot(*normal_time)->position.has_value()
            && !state.find_plot(*normal_time)->size.has_value(),
        "plot floating geometry can return to an unset state");
    check(state.erase_plot(*normal_trajectory) && state.find_plot(*normal_trajectory) == nullptr
            && state.create_plot(PlotType::NormalTrajectory)->value == 5,
        "deleted plot state is discarded and PlotWindowIds are not reused");
    check(!state.erase_plot(PlotWindowId{999}) && !state.set_plot_visible(PlotWindowId{999}, true)
            && !state.set_plot_title(*relative_time, "")
            && !state.set_plot_position(
                PlotWindowId{999}, FloatingWindowPosition{0.0F, 0.0F})
            && !state.set_plot_size(PlotWindowId{999}, FloatingWindowSize{1.0F, 1.0F}),
        "unknown plot IDs and empty titles are rejected without changing state");

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
