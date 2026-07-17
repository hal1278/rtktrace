#include "plotcore/plot/data_view.hpp"

#include <type_traits>

namespace plotcore {

bool quality_is_visible(const QualityFilter& filter, SolutionQuality quality) noexcept
{
    const std::size_t index = static_cast<std::size_t>(quality);
    return index < filter.visible.size() && filter.visible[index];
}

std::size_t plot_series_size(const PlotSeriesView& series) noexcept
{
    return std::visit([](const auto samples) { return samples.size(); }, series.source);
}

std::optional<PlotSampleValue> plot_sample_at(
    const PlotSeriesView& series, std::size_t index) noexcept
{
    return std::visit(
        [index](const auto samples) -> std::optional<PlotSampleValue> {
            if (index >= samples.size()) {
                return std::nullopt;
            }
            using Span = std::remove_cvref_t<decltype(samples)>;
            using Sample = typename Span::element_type;
            if constexpr (std::is_same_v<Sample, const NormalizedSample>) {
                const NormalizedSample& sample = samples[index];
                return PlotSampleValue{
                    sample.time,
                    sample.enu.east_m,
                    sample.enu.north_m,
                    sample.enu.up_m,
                    sample.llh.ellipsoidal_height_m,
                    std::nullopt,
                    sample.quality,
                };
            } else {
                const RelativeSample& sample = samples[index];
                return PlotSampleValue{
                    sample.time,
                    sample.delta_enu.east_m,
                    sample.delta_enu.north_m,
                    sample.delta_enu.up_m,
                    sample.delta_ellipsoidal_height_m,
                    sample.reference_relative_distance_3d_m,
                    sample.quality,
                };
            }
        },
        series.source);
}

std::optional<PlotDataView> make_normal_plot_data_view(
    const LoadedFiles& files, const CommonTimeRangeIndex& time_index)
{
    if (!time_index.range.has_value() || time_index.revision == 0
        || time_index.file_ranges.size() != files.size()) {
        return std::nullopt;
    }
    PlotDataView view{PlotDataKind::Normal, {}};
    view.series.reserve(files.size());
    for (std::size_t index = 0; index < files.size(); ++index) {
        const SampleRangeIndex range = time_index.file_ranges[index];
        if (range.begin > range.end || range.end > files[index].samples.size()) {
            return std::nullopt;
        }
        const std::span<const NormalizedSample> samples{files[index].samples};
        view.series.push_back(PlotSeriesView{
            index + 1,
            files[index].visible,
            samples.subspan(range.begin, range.size()),
        });
    }
    return view;
}

std::optional<PlotDataView> make_relative_plot_data_view(
    const LoadedFiles& files, const RelativeCache& relative_cache)
{
    if (!relative_cache.dependencies.has_value()
        || relative_cache.samples_by_slot.size() != files.size()
        || relative_cache.dependencies->slot_count != files.size()) {
        return std::nullopt;
    }
    PlotDataView view{PlotDataKind::Relative, {}};
    if (files.size() < 2) {
        return view;
    }
    view.series.reserve(files.size() - 1);
    for (std::size_t index = 1; index < files.size(); ++index) {
        view.series.push_back(PlotSeriesView{
            index + 1,
            files[index].visible,
            std::span<const RelativeSample>{relative_cache.samples_by_slot[index]},
        });
    }
    return view;
}

} // namespace plotcore
