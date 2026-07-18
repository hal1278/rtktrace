#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "plotcore/analysis/relative.hpp"

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

[[nodiscard]] plotcore::NormalizedSample sample_at(std::int64_t time_ns, plotcore::Enu enu,
    double height_m, plotcore::Ecef ecef,
    plotcore::SolutionQuality quality = plotcore::SolutionQuality::Fixed)
{
    return plotcore::NormalizedSample{
        .time = plotcore::GpsTime{time_ns},
        .llh = plotcore::Wgs84Llh{0.0, 0.0, height_m},
        .ecef = ecef,
        .enu = enu,
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

[[nodiscard]] std::vector<plotcore::NormalizedSample> reference_samples()
{
    using namespace plotcore;
    return {
        sample_at(0, Enu{1.0, 2.0, 3.0}, 10.0, Ecef{100.0, 200.0, 300.0}),
        sample_at(10, Enu{10.0, 20.0, 30.0}, 20.0, Ecef{110.0, 210.0, 310.0}),
    };
}

[[nodiscard]] std::vector<plotcore::NormalizedSample> comparison_samples()
{
    using namespace plotcore;
    return {
        sample_at(5, Enu{4.0, 8.0, 12.0}, 15.0, Ecef{103.0, 204.0, 312.0}, SolutionQuality::Float),
        sample_at(
            15, Enu{13.0, 24.0, 35.0}, 18.0, Ecef{113.0, 214.0, 322.0}, SolutionQuality::Dgps),
        sample_at(
            25, Enu{15.0, 26.0, 37.0}, 22.0, Ecef{116.0, 218.0, 334.0}, SolutionQuality::Single),
    };
}

void test_relative_components()
{
    using namespace plotcore;
    const std::vector references = reference_samples();
    const std::vector comparisons = comparison_samples();
    const std::optional<std::vector<RelativeSample>> relative =
        make_relative_samples(references, SampleRangeIndex{0, references.size()}, comparisons,
            SampleRangeIndex{0, comparisons.size()}, ReferenceMatchConfiguration{false, 0});

    check(relative.has_value() && relative->size() == 3,
        "relative samples are generated for every matched comparison");
    if (!relative.has_value() || relative->size() != 3) {
        return;
    }
    const RelativeSample& first = relative->front();
    check(first.time == GpsTime{5}, "relative sample uses comparison time");
    check(near(first.delta_enu.east_m, 3.0) && near(first.delta_enu.north_m, 6.0)
            && near(first.delta_enu.up_m, 9.0),
        "relative ENU components subtract the matched reference");
    check(near(first.delta_ellipsoidal_height_m, 5.0),
        "relative ellipsoidal height is independent of ENU Up");
    check(near(first.reference_relative_distance_3d_m, 13.0),
        "reference-relative distance is the ECEF Euclidean norm");
    check(first.quality == SolutionQuality::Float, "relative sample retains comparison quality");
    check(first.comparison_sample_index == 0 && first.reference_sample_index == 0
            && first.reference_time_difference_ns == 5,
        "relative sample retains match source indices and time difference");

    const RelativeSample& second = (*relative)[1];
    check(second.reference_sample_index == 1 && near(second.delta_ellipsoidal_height_m, -2.0),
        "later comparison uses the newest non-future reference");
    check((*relative)[2].reference_sample_index == 1,
        "disabled tolerance continues using the final reference sample");

    const std::optional<std::vector<RelativeSample>> limited =
        make_relative_samples(references, SampleRangeIndex{0, references.size()}, comparisons,
            SampleRangeIndex{0, comparisons.size()}, ReferenceMatchConfiguration{true, 4});
    check(limited.has_value() && limited->empty(),
        "relative generation applies enabled reference tolerance");
}

void test_invalid_relative_values()
{
    using namespace plotcore;
    std::vector references = reference_samples();
    std::vector comparisons = comparison_samples();
    comparisons[0].ecef.x_m = std::numeric_limits<double>::infinity();
    check(!make_relative_samples(references, SampleRangeIndex{0, references.size()}, comparisons,
              SampleRangeIndex{0, comparisons.size()}, ReferenceMatchConfiguration{false, 0})
              .has_value(),
        "non-finite relative component input is rejected");
}

void test_relative_cache_dependencies()
{
    using namespace plotcore;
    LoadedFiles files;
    files.push_back(file_with_samples("reference.pos", reference_samples()));
    files.push_back(file_with_samples("comparison.pos", comparison_samples()));
    files.push_back(file_with_samples("third.pos",
        {
            sample_at(5, Enu{-4.0, -8.0, -12.0}, 5.0, Ecef{97.0, 196.0, 288.0}),
        }));

    CommonTimeRangeIndex time_index;
    check(rebuild_common_time_range_index(files, TimeRange{GpsTime{0}, GpsTime{30}}, time_index),
        "relative cache test builds common time indexes");
    EnuCache enu_cache{
        .reference = make_enu_reference(Wgs84Llh{0.0, 0.0, 0.0}),
        .revision = 1,
    };
    RelativeCache cache;
    const ReferenceMatchConfiguration unlimited{false, 0};
    check(rebuild_relative_cache(files, time_index, enu_cache, unlimited, cache)
            == RelativeCacheUpdateStatus::Updated,
        "relative cache builds from shared range and ENU revisions");
    check(cache.revision == 1 && cache.samples_by_slot.size() == files.size(),
        "relative cache stores one slot-aligned vector and advances revision");
    const std::vector<RelativeSample>* reference_slot = relative_samples_at_slot(cache, 1);
    const std::vector<RelativeSample>* comparison_slot = relative_samples_at_slot(cache, 2);
    check(reference_slot != nullptr && reference_slot->empty(),
        "slot 1 has no self-relative samples");
    check(comparison_slot != nullptr && comparison_slot->size() == 3,
        "comparison slot exposes its shared relative samples");
    check(relative_samples_at_slot(cache, 0) == nullptr
            && relative_samples_at_slot(cache, 4) == nullptr,
        "invalid slot has no relative data view");

    check(rebuild_relative_cache(files, time_index, enu_cache, unlimited, cache)
            == RelativeCacheUpdateStatus::Unchanged,
        "unchanged dependencies avoid duplicate matching work");
    check(cache.revision == 1, "unchanged relative cache keeps its revision");

    const ReferenceMatchConfiguration limited{true, 5};
    check(rebuild_relative_cache(files, time_index, enu_cache, limited, cache)
            == RelativeCacheUpdateStatus::Updated,
        "tolerance setting change invalidates relative cache");
    comparison_slot = relative_samples_at_slot(cache, 2);
    check(cache.revision == 2 && comparison_slot != nullptr && comparison_slot->size() == 2,
        "tolerance rebuild removes comparison beyond maximum time difference");

    files[1].samples[0].enu.east_m += 10.0;
    ++enu_cache.revision;
    check(rebuild_relative_cache(files, time_index, enu_cache, limited, cache)
            == RelativeCacheUpdateStatus::Updated,
        "ENU revision change invalidates ENU-dependent relative components");
    comparison_slot = relative_samples_at_slot(cache, 2);
    check(cache.revision == 3 && comparison_slot != nullptr
            && near(comparison_slot->front().delta_enu.east_m, 13.0),
        "ENU invalidation rebuilds relative component values");

    const std::uint64_t retained_revision = cache.revision;
    const std::size_t retained_count = comparison_slot->size();
    const EnuCache unavailable_enu;
    check(rebuild_relative_cache(files, time_index, unavailable_enu, limited, cache)
            == RelativeCacheUpdateStatus::RetainedPreviousData,
        "missing ENU dependency retains previous relative data");
    check(cache.revision == retained_revision
            && relative_samples_at_slot(cache, 2)->size() == retained_count,
        "failed relative rebuild preserves cache contents and revision");

    RelativeCache empty_cache;
    check(rebuild_relative_cache(files, time_index, unavailable_enu, limited, empty_cache)
            == RelativeCacheUpdateStatus::Unavailable,
        "missing dependencies without previous data leave relative cache unavailable");
}

void test_slot_order_invalidation()
{
    using namespace plotcore;
    LoadedFiles files;
    files.push_back(file_with_samples("reference.pos", reference_samples()));
    files.push_back(file_with_samples("comparison.pos", comparison_samples()));
    files.push_back(file_with_samples("third.pos",
        {
            sample_at(5, Enu{-4.0, -8.0, -12.0}, 5.0, Ecef{97.0, 196.0, 288.0}),
        }));
    CommonTimeRangeIndex time_index;
    static_cast<void>(
        rebuild_common_time_range_index(files, TimeRange{GpsTime{0}, GpsTime{30}}, time_index));
    EnuCache enu_cache{
        .reference = make_enu_reference(Wgs84Llh{0.0, 0.0, 0.0}),
        .revision = 1,
    };
    RelativeCache cache;
    static_cast<void>(rebuild_relative_cache(
        files, time_index, enu_cache, ReferenceMatchConfiguration{false, 0}, cache));

    check(move_slot(files, 3, 2), "comparison slot order changes");
    check(rebuild_common_time_range_index(files, TimeRange{GpsTime{0}, GpsTime{30}}, time_index),
        "slot order change rebuilds common time index revision");
    check(rebuild_relative_cache(
              files, time_index, enu_cache, ReferenceMatchConfiguration{false, 0}, cache)
            == RelativeCacheUpdateStatus::Updated,
        "slot order revision invalidates relative cache");
    const std::vector<RelativeSample>* new_slot_two = relative_samples_at_slot(cache, 2);
    check(new_slot_two != nullptr && new_slot_two->size() == 1
            && near(new_slot_two->front().delta_enu.east_m, -5.0),
        "rebuilt slot mapping follows the reordered comparison file");
}

} // namespace

int main()
{
    test_relative_components();
    test_invalid_relative_values();
    test_relative_cache_dependencies();
    test_slot_order_invalidation();

    if (failures != 0) {
        std::cerr << failures << " relative test(s) failed\n";
        return 1;
    }
    return 0;
}
