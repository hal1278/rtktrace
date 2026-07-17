#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include "plotcore/plot/axis.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(double actual, double expected, double tolerance = 1.0e-12) noexcept
{
    return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] plotcore::NormalizedSample sample_at(std::int64_t time_ns,
    double east_m, double north_m, double up_m, double height_m,
    plotcore::SolutionQuality quality)
{
    return plotcore::NormalizedSample{
        .time = plotcore::GpsTime{time_ns},
        .llh = plotcore::Wgs84Llh{0.0, 0.0, height_m},
        .ecef = {},
        .enu = plotcore::Enu{east_m, north_m, up_m},
        .quality = quality,
        .source_line_number = 1,
        .continuous_from_previous = false,
    };
}

[[nodiscard]] plotcore::LoadedFile file_with_samples(
    std::string_view name, std::vector<plotcore::NormalizedSample> samples)
{
    plotcore::LoadedFile file{std::filesystem::path{name}, plotcore::InputFormat::Pos};
    file.samples = std::move(samples);
    return file;
}

void test_normal_and_relative_data_views()
{
    using namespace plotcore;
    LoadedFiles files;
    files.push_back(file_with_samples("one.pos", {
        sample_at(0, 0.0, 1.0, 2.0, 3.0, SolutionQuality::Fixed),
        sample_at(10, 4.0, 5.0, 6.0, 7.0, SolutionQuality::Float),
        sample_at(20, 8.0, 9.0, 10.0, 11.0, SolutionQuality::Single),
    }));
    files.push_back(file_with_samples("two.pos", {
        sample_at(10, -1.0, -2.0, -3.0, -4.0, SolutionQuality::Dgps),
    }));
    files[1].visible = false;
    CommonTimeRangeIndex index;
    static_cast<void>(rebuild_common_time_range_index(
        files, TimeRange{GpsTime{10}, GpsTime{20}}, index));

    const std::optional<PlotDataView> normal = make_normal_plot_data_view(files, index);
    check(normal.has_value() && normal->kind == PlotDataKind::Normal
            && normal->series.size() == 2,
        "normal plot view exposes one zero-copy series per slot");
    if (normal.has_value()) {
        check(plot_series_size(normal->series[0]) == 2
                && normal->series[0].slot_number == 1,
            "normal plot view applies the common time range index");
        const std::optional<PlotSampleValue> value = plot_sample_at(normal->series[0], 0);
        check(value.has_value() && value->time == GpsTime{10}
                && near(value->east_m, 4.0) && near(value->ellipsoidal_height_m, 7.0)
                && !value->reference_relative_distance_3d_m.has_value(),
            "normal sample is projected to the shared plot value interface");
        check(!normal->series[1].file_visible,
            "plot series captures per-file visibility");
        check(!plot_sample_at(normal->series[0], 2).has_value(),
            "out-of-range plot sample access is rejected");
    }

    RelativeCache relative_cache;
    relative_cache.samples_by_slot.resize(2);
    relative_cache.samples_by_slot[1].push_back(RelativeSample{
        .time = GpsTime{10},
        .delta_enu = Enu{1.0, 2.0, 3.0},
        .delta_ellipsoidal_height_m = 4.0,
        .reference_relative_distance_3d_m = 5.0,
        .quality = SolutionQuality::Dgps,
        .comparison_sample_index = 0,
        .reference_sample_index = 0,
        .reference_time_difference_ns = 0,
    });
    relative_cache.dependencies = RelativeCacheDependencies{
        1, 1, ReferenceMatchConfiguration{false, 0}, 2};
    relative_cache.revision = 1;
    const std::optional<PlotDataView> relative =
        make_relative_plot_data_view(files, relative_cache);
    check(relative.has_value() && relative->kind == PlotDataKind::Relative
            && relative->series.size() == 1 && relative->series[0].slot_number == 2,
        "relative plot view omits reference slot and preserves comparison slot number");
    if (relative.has_value()) {
        const std::optional<PlotSampleValue> value = plot_sample_at(relative->series[0], 0);
        check(value.has_value() && near(value->east_m, 1.0)
                && near(value->ellipsoidal_height_m, 4.0)
                && value->reference_relative_distance_3d_m == 5.0,
            "relative sample uses the same plot value interface");
    }

    QualityFilter filter;
    filter.visible[static_cast<std::size_t>(SolutionQuality::Dgps)] = false;
    check(!quality_is_visible(filter, SolutionQuality::Dgps)
            && quality_is_visible(filter, SolutionQuality::Fixed),
        "quality filter independently controls normalized qualities");
    check(!quality_is_visible(filter, static_cast<SolutionQuality>(99)),
        "invalid quality is never visible");
}

void test_auto_fit_trajectory_and_components()
{
    using namespace plotcore;
    const std::vector samples{
        sample_at(0, 0.0, 0.0, 7.0, 100.0, SolutionQuality::Fixed),
        sample_at(2'000'000'000, 9.0, 18.0, 7.0, 109.0, SolutionQuality::Float),
    };
    const PlotDataView data{
        PlotDataKind::Normal,
        {PlotSeriesView{1, true, std::span<const NormalizedSample>{samples}}},
    };
    QualityFilter filter;
    const std::optional<TrajectoryAxisRanges> trajectory =
        auto_fit_trajectory(data, filter, PlotAreaSize{100.0, 200.0});
    check(trajectory.has_value(), "trajectory auto-fit accepts visible data");
    if (trajectory.has_value()) {
        check(near(trajectory->meters_per_pixel, 0.1),
            "trajectory auto-fit reserves ten percent margin");
        check(near(trajectory->east.minimum, -0.5)
                && near(trajectory->east.maximum, 9.5)
                && near(trajectory->north.minimum, -1.0)
                && near(trajectory->north.maximum, 19.0),
            "trajectory auto-fit centers equal-scale axis ranges");
        check(near(trajectory->east.length() / 100.0,
                  trajectory->north.length() / 200.0),
            "trajectory axes share one meter-per-pixel scale");
    }

    const std::optional<NumericRange> east =
        auto_fit_position_component(data, filter, PositionComponent::East);
    const std::optional<NumericRange> up =
        auto_fit_position_component(data, filter, PositionComponent::Up);
    check(east.has_value() && near(east->minimum, 0.0) && near(east->maximum, 9.0),
        "time-series auto-fit adds no margin to varying position data");
    check(up.has_value() && near(up->minimum, 6.5) && near(up->maximum, 7.5),
        "constant time-series component receives a one-meter range");
    check(!auto_fit_position_component(
               data, filter, PositionComponent::ReferenceRelativeDistance3d)
               .has_value(),
        "normal data has no reference-relative distance component");

    const std::optional<TimeRange> time = auto_fit_time_axis(data, filter);
    check(time == TimeRange{GpsTime{0}, GpsTime{2'000'000'000}},
        "time auto-fit uses exact varying sample bounds");

    filter.visible[static_cast<std::size_t>(SolutionQuality::Float)] = false;
    const std::optional<TrajectoryAxisRanges> single =
        auto_fit_trajectory(data, filter, PlotAreaSize{100.0, 200.0});
    check(single.has_value() && near(single->meters_per_pixel, 0.01)
            && near(single->east.length(), 1.0) && near(single->north.length(), 2.0),
        "single-point trajectory assigns one meter to the shorter plot dimension");
    const std::optional<TimeRange> single_time = auto_fit_time_axis(data, filter);
    check(single_time.has_value()
            && single_time->end - single_time->start == degenerate_time_axis_range_ns,
        "single time receives a one-minute axis range");
}

void test_minimum_ranges_and_visibility()
{
    using namespace plotcore;
    const std::vector tiny{
        sample_at(0, 0.0, 0.0, 0.0, 0.0, SolutionQuality::Fixed),
        sample_at(100, 0.0001, 0.0001, 0.0001, 0.0001, SolutionQuality::Fixed),
    };
    const PlotDataView data{
        PlotDataKind::Normal,
        {PlotSeriesView{1, true, std::span<const NormalizedSample>{tiny}}},
    };
    const QualityFilter filter;
    const std::optional<TrajectoryAxisRanges> trajectory =
        auto_fit_trajectory(data, filter, PlotAreaSize{100.0, 200.0});
    check(trajectory.has_value() && near(trajectory->east.length(), 0.001)
            && near(trajectory->north.length(), 0.002),
        "trajectory auto-fit enforces one-millimeter shorter-axis minimum");
    const std::optional<NumericRange> east =
        auto_fit_position_component(data, filter, PositionComponent::East);
    check(east.has_value() && near(east->length(), minimum_position_axis_range_m),
        "time-series position component enforces one-millimeter minimum");
    const std::optional<TimeRange> time = auto_fit_time_axis(data, filter);
    check(time.has_value() && time->end - time->start == minimum_time_axis_range_ns,
        "time axis enforces one-millisecond minimum");

    PlotDataView hidden = data;
    hidden.series[0].file_visible = false;
    check(!auto_fit_trajectory(hidden, filter, PlotAreaSize{100.0, 200.0}).has_value(),
        "hidden files are excluded from auto-fit");
    check(!auto_fit_trajectory(data, filter, PlotAreaSize{0.0, 200.0}).has_value(),
        "invalid plot area cannot produce trajectory ranges");
}

} // namespace

int main()
{
    test_normal_and_relative_data_views();
    test_auto_fit_trajectory_and_components();
    test_minimum_ranges_and_visibility();

    if (failures != 0) {
        std::cerr << failures << " plot test(s) failed\n";
        return 1;
    }
    return 0;
}
