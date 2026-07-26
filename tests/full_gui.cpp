#include <cmath>
#include <iostream>
#include <string_view>

#include "../src/full/gui.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] bool overlaps(
    const rtktrace::full_detail::WindowRect& lhs,
    const rtktrace::full_detail::WindowRect& rhs) noexcept
{
    return lhs.x < rhs.x + rhs.width && rhs.x < lhs.x + lhs.width
        && lhs.y < rhs.y + rhs.height && rhs.y < lhs.y + lhs.height;
}

} // namespace

int main()
{
    using namespace rtktrace;

    const full_detail::InitialWindowLayout layout =
        full_detail::initial_window_layout(0.0F, 24.0F, 1280.0F, 696.0F);
    check(!overlaps(layout.file_slots, layout.window_manager)
            && !overlaps(layout.file_slots, layout.shared_controls)
            && !overlaps(layout.window_manager, layout.shared_controls)
            && !overlaps(layout.initial_plot, layout.window_manager)
            && !overlaps(layout.initial_plot, layout.shared_controls),
        "startup File/Slots, manager, shared controls, and plot regions do not overlap");
    check(layout.file_slots.x < layout.window_manager.x
            && layout.window_manager.x < layout.initial_plot.x
            && layout.window_manager.y < layout.shared_controls.y,
        "startup layout places File/Slots left, manager center-top, controls center-bottom, plot right");

    const full_detail::WindowRect region{700.0F, 40.0F, 560.0F, 620.0F};
    const auto first = full_detail::cascaded_plot_rect(region, 0);
    const auto second = full_detail::cascaded_plot_rect(region, 1);
    check(first.x == region.x && first.y == region.y
            && std::abs(second.x - first.x - 24.0F) < 0.01F
            && std::abs(second.y - first.y - 24.0F) < 0.01F,
        "new plot windows cascade by 24 pixels in the right region");
    check(second.x + second.width <= region.x + region.width
            && second.y + second.height <= region.y + region.height,
        "cascaded plot rectangles remain inside the right plot region");

    FullGui gui;
    const auto& plots = gui.application_state().plots();
    check(plots.size() == 1 && plots.front().id.value == 1
            && plots.front().type == PlotType::NormalTrajectory && plots.front().visible
            && plots.front().title == "Normal 2D 1",
        "full GUI starts with one visible Normal 2D plot using the documented title");
    check(gui.runtime_count() == 1,
        "the initial plot receives an independent runtime immediately at creation");

    return failures == 0 ? 0 : 1;
}
