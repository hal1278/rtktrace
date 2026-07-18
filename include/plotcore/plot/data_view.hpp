#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "plotcore/analysis/common_time_index.hpp"
#include "plotcore/analysis/relative.hpp"

namespace plotcore {

enum class PlotDataKind : std::uint8_t {
    Normal,
    Relative,
};

struct QualityFilter {
    std::array<bool, solution_quality_count> visible{true, true, true, true, true, true, true};
};

struct PlotSampleValue {
    GpsTime time;
    double east_m;
    double north_m;
    double up_m;
    double ellipsoidal_height_m;
    std::optional<double> reference_relative_distance_3d_m;
    SolutionQuality quality;
};

using PlotSeriesSource =
    std::variant<std::span<const NormalizedSample>, std::span<const RelativeSample>>;

struct PlotSeriesView {
    std::size_t slot_number;
    bool file_visible;
    PlotSeriesSource source;
};

struct PlotDataView {
    PlotDataKind kind;
    std::vector<PlotSeriesView> series;
};

[[nodiscard]] bool quality_is_visible(
    const QualityFilter& filter, SolutionQuality quality) noexcept;
[[nodiscard]] std::size_t plot_series_size(const PlotSeriesView& series) noexcept;
[[nodiscard]] std::optional<PlotSampleValue> plot_sample_at(
    const PlotSeriesView& series, std::size_t index) noexcept;

[[nodiscard]] std::optional<PlotDataView> make_normal_plot_data_view(
    const LoadedFiles& files, const CommonTimeRangeIndex& time_index);
[[nodiscard]] std::optional<PlotDataView> make_relative_plot_data_view(
    const LoadedFiles& files, const RelativeCache& relative_cache);

} // namespace plotcore
