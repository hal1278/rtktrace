#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "plotcore/analysis/enu.hpp"
#include "plotcore/analysis/reference_matching.hpp"

namespace plotcore {

struct RelativeSample {
    GpsTime time;
    Enu delta_enu;
    double delta_ellipsoidal_height_m;
    double reference_relative_distance_3d_m;
    SolutionQuality quality;
    std::size_t comparison_sample_index;
    std::size_t reference_sample_index;
    std::int64_t reference_time_difference_ns;
};

struct RelativeCacheDependencies {
    std::uint64_t common_time_index_revision;
    std::uint64_t enu_revision;
    ReferenceMatchConfiguration match_configuration;
    std::size_t slot_count;

    bool operator==(const RelativeCacheDependencies&) const = default;
};

struct RelativeCache {
    std::vector<std::vector<RelativeSample>> samples_by_slot;
    std::optional<RelativeCacheDependencies> dependencies;
    std::uint64_t revision{0};
};

enum class RelativeCacheUpdateStatus : std::uint8_t {
    Updated,
    Unchanged,
    RetainedPreviousData,
    Unavailable,
};

[[nodiscard]] std::optional<std::vector<RelativeSample>> make_relative_samples(
    std::span<const NormalizedSample> reference_samples, SampleRangeIndex reference_range,
    std::span<const NormalizedSample> comparison_samples, SampleRangeIndex comparison_range,
    ReferenceMatchConfiguration configuration);

[[nodiscard]] RelativeCacheUpdateStatus rebuild_relative_cache(const LoadedFiles& files,
    const CommonTimeRangeIndex& time_index, const EnuCache& enu_cache,
    ReferenceMatchConfiguration configuration, RelativeCache& cache);

[[nodiscard]] const std::vector<RelativeSample>* relative_samples_at_slot(
    const RelativeCache& cache, std::size_t slot_number) noexcept;

} // namespace plotcore
