#include "plotcore/analysis/relative.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace plotcore {
namespace {

[[nodiscard]] std::optional<RelativeSample> make_relative_sample(const NormalizedSample& reference,
    const NormalizedSample& comparison, const ReferenceMatch& match) noexcept
{
    const double delta_x = comparison.ecef.x_m - reference.ecef.x_m;
    const double delta_y = comparison.ecef.y_m - reference.ecef.y_m;
    const double delta_z = comparison.ecef.z_m - reference.ecef.z_m;
    const RelativeSample relative{
        .time = comparison.time,
        .delta_enu =
            Enu{
                comparison.enu.east_m - reference.enu.east_m,
                comparison.enu.north_m - reference.enu.north_m,
                comparison.enu.up_m - reference.enu.up_m,
            },
        .delta_ellipsoidal_height_m =
            comparison.llh.ellipsoidal_height_m - reference.llh.ellipsoidal_height_m,
        .reference_relative_distance_3d_m = std::hypot(delta_x, delta_y, delta_z),
        .quality = comparison.quality,
        .comparison_sample_index = match.comparison_index,
        .reference_sample_index = match.reference_index,
        .reference_time_difference_ns = match.time_difference_ns,
    };
    if (!std::isfinite(relative.delta_enu.east_m) || !std::isfinite(relative.delta_enu.north_m)
        || !std::isfinite(relative.delta_enu.up_m)
        || !std::isfinite(relative.delta_ellipsoidal_height_m)
        || !std::isfinite(relative.reference_relative_distance_3d_m)) {
        return std::nullopt;
    }
    return relative;
}

[[nodiscard]] RelativeCacheUpdateStatus unavailable_status(const RelativeCache& cache) noexcept
{
    return cache.dependencies.has_value() ? RelativeCacheUpdateStatus::RetainedPreviousData
                                          : RelativeCacheUpdateStatus::Unavailable;
}

} // namespace

std::optional<std::vector<RelativeSample>> make_relative_samples(
    std::span<const NormalizedSample> reference_samples, SampleRangeIndex reference_range,
    std::span<const NormalizedSample> comparison_samples, SampleRangeIndex comparison_range,
    ReferenceMatchConfiguration configuration)
{
    const std::optional<std::vector<ReferenceMatch>> matches = match_reference_epochs(
        reference_samples, reference_range, comparison_samples, comparison_range, configuration);
    if (!matches.has_value()) {
        return std::nullopt;
    }
    std::vector<RelativeSample> samples;
    samples.reserve(matches->size());
    for (const ReferenceMatch& match : *matches) {
        const std::optional<RelativeSample> sample =
            make_relative_sample(reference_samples[match.reference_index],
                comparison_samples[match.comparison_index], match);
        if (!sample.has_value()) {
            return std::nullopt;
        }
        samples.push_back(*sample);
    }
    return samples;
}

RelativeCacheUpdateStatus rebuild_relative_cache(const LoadedFiles& files,
    const CommonTimeRangeIndex& time_index, const EnuCache& enu_cache,
    ReferenceMatchConfiguration configuration, RelativeCache& cache)
{
    if (!time_index.range.has_value() || time_index.file_ranges.size() != files.size()
        || time_index.revision == 0 || !enu_cache.reference.has_value() || enu_cache.revision == 0
        || configuration.maximum_time_difference_ns < 0) {
        return unavailable_status(cache);
    }
    const RelativeCacheDependencies dependencies{
        time_index.revision,
        enu_cache.revision,
        configuration,
        files.size(),
    };
    if (cache.dependencies == dependencies) {
        return RelativeCacheUpdateStatus::Unchanged;
    }

    std::vector<std::vector<RelativeSample>> samples_by_slot(files.size());
    if (files.size() > 1) {
        const SampleRangeIndex* reference_range = range_at_slot(time_index, 1);
        if (reference_range == nullptr) {
            return unavailable_status(cache);
        }
        for (std::size_t slot = 2; slot <= files.size(); ++slot) {
            const SampleRangeIndex* comparison_range = range_at_slot(time_index, slot);
            if (comparison_range == nullptr) {
                return unavailable_status(cache);
            }
            std::optional<std::vector<RelativeSample>> relative_samples =
                make_relative_samples(files[0].samples, *reference_range, files[slot - 1].samples,
                    *comparison_range, configuration);
            if (!relative_samples.has_value()) {
                return unavailable_status(cache);
            }
            samples_by_slot[slot - 1] = std::move(*relative_samples);
        }
    }

    cache.samples_by_slot = std::move(samples_by_slot);
    cache.dependencies = dependencies;
    if (cache.revision != std::numeric_limits<std::uint64_t>::max()) {
        ++cache.revision;
    }
    return RelativeCacheUpdateStatus::Updated;
}

const std::vector<RelativeSample>* relative_samples_at_slot(
    const RelativeCache& cache, std::size_t slot_number) noexcept
{
    if (slot_number == 0 || slot_number > cache.samples_by_slot.size()) {
        return nullptr;
    }
    return &cache.samples_by_slot[slot_number - 1];
}

} // namespace plotcore
