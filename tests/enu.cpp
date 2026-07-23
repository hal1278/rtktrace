#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <numbers>
#include <string_view>

#include "rtktrace/analysis/enu.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(double actual, double expected, double tolerance = 1.0e-9) noexcept
{
    return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] rtktrace::NormalizedSample sample_at(std::int64_t time_ns, rtktrace::Wgs84Llh llh,
    rtktrace::SolutionQuality quality = rtktrace::SolutionQuality::Fixed)
{
    return rtktrace::NormalizedSample{
        .time = rtktrace::GpsTime{time_ns},
        .llh = llh,
        .ecef = rtktrace::wgs84_llh_to_ecef(llh),
        .enu = {},
        .quality = quality,
        .source_line_number = 1,
        .continuous_from_previous = false,
    };
}

[[nodiscard]] rtktrace::LoadedFile file_with_samples(
    std::string_view name, std::initializer_list<rtktrace::NormalizedSample> samples)
{
    rtktrace::LoadedFile file{std::filesystem::path{name}, rtktrace::InputFormat::Pos};
    file.samples.assign(samples);
    return file;
}

[[nodiscard]] std::size_t diagnostic_count(
    const rtktrace::LoadedFile& file, rtktrace::DiagnosticCode code)
{
    std::size_t count = 0;
    for (const rtktrace::Diagnostic& diagnostic : file.diagnostics) {
        if (diagnostic.code == code) {
            ++count;
        }
    }
    return count;
}

void test_slot_reference_methods()
{
    using namespace rtktrace;
    constexpr double pi = std::numbers::pi_v<double>;
    const Wgs84Llh first{35.0 * pi / 180.0, 139.0 * pi / 180.0, 10.0};
    const Wgs84Llh middle{35.1 * pi / 180.0, 139.1 * pi / 180.0, 20.0};
    const Wgs84Llh last{35.2 * pi / 180.0, 139.2 * pi / 180.0, 30.0};
    LoadedFiles files;
    files.push_back(file_with_samples("slot1.pos",
        {
            sample_at(0, first),
            sample_at(10, middle, SolutionQuality::InvalidOrUnknown),
            sample_at(20, last),
        }));
    const TimeRange range{GpsTime{5}, GpsTime{20}};

    EnuReferenceConfiguration configuration;
    configuration.method = EnuReferenceMethod::Slot1Start;
    const std::optional<EnuReference> start = determine_enu_reference(files, range, configuration);
    check(start.has_value(), "slot 1 start reference is found");
    if (start.has_value()) {
        check(near(start->origin_ecef.x_m, files[0].samples[1].ecef.x_m),
            "range start selects the first included sample, including quality zero");
    }

    configuration.method = EnuReferenceMethod::Slot1End;
    const std::optional<EnuReference> end = determine_enu_reference(files, range, configuration);
    check(end.has_value(), "slot 1 end reference is found");
    if (end.has_value()) {
        check(near(end->origin_ecef.x_m, files[0].samples[2].ecef.x_m),
            "range end selects the last included sample");
    }

    configuration.method = EnuReferenceMethod::Slot1EcefAverage;
    const std::optional<EnuReference> average =
        determine_enu_reference(files, range, configuration);
    check(average.has_value(), "slot 1 ECEF average reference is found");
    if (average.has_value()) {
        check(near(average->origin_ecef.x_m,
                  (files[0].samples[1].ecef.x_m + files[0].samples[2].ecef.x_m) / 2.0, 1.0e-6),
            "average reference uses the arithmetic ECEF origin");
        const std::optional<Wgs84Llh> rotation_llh = wgs84_ecef_to_llh(average->origin_ecef);
        check(rotation_llh.has_value()
                && near(average->latitude_rad, rotation_llh->latitude_rad, 1.0e-14),
            "average reference derives rotation latitude from averaged ECEF");
    }
}

void test_user_specified_reference()
{
    using namespace rtktrace;
    constexpr double pi = std::numbers::pi_v<double>;
    EnuReferenceConfiguration llh_configuration{
        .method = EnuReferenceMethod::UserSpecified,
        .user_position = UserSpecifiedLlh{35.0, 139.0, 50.0},
    };
    const std::optional<EnuReference> llh_reference = determine_enu_reference(
        LoadedFiles{}, TimeRange{GpsTime{20}, GpsTime{10}}, llh_configuration);
    check(llh_reference.has_value(), "user LLH reference does not require slot 1 or a valid range");
    if (llh_reference.has_value()) {
        check(near(llh_reference->latitude_rad, 35.0 * pi / 180.0, 1.0e-15),
            "user LLH latitude is interpreted as degrees");
        check(near(llh_reference->longitude_rad, 139.0 * pi / 180.0, 1.0e-15),
            "user LLH longitude is interpreted as degrees");
    }

    const Ecef specified_ecef = wgs84_llh_to_ecef(Wgs84Llh{0.25, -0.5, 100.0});
    EnuReferenceConfiguration ecef_configuration{
        .method = EnuReferenceMethod::UserSpecified,
        .user_position = specified_ecef,
    };
    const std::optional<EnuReference> ecef_reference = determine_enu_reference(
        LoadedFiles{}, TimeRange{GpsTime{0}, GpsTime{0}}, ecef_configuration);
    check(ecef_reference.has_value(), "user ECEF reference is accepted");
    if (ecef_reference.has_value()) {
        check(near(ecef_reference->origin_ecef.x_m, specified_ecef.x_m),
            "user ECEF remains the translation origin");
    }

    EnuReferenceConfiguration missing{
        .method = EnuReferenceMethod::UserSpecified,
        .user_position = std::nullopt,
    };
    check(!determine_enu_reference(LoadedFiles{}, TimeRange{GpsTime{0}, GpsTime{0}}, missing)
              .has_value(),
        "missing user position cannot define an ENU reference");
}

void test_cache_rebuild_and_retention()
{
    using namespace rtktrace;
    constexpr double pi = std::numbers::pi_v<double>;
    const Wgs84Llh first_reference{35.0 * pi / 180.0, 139.0 * pi / 180.0, 10.0};
    const Wgs84Llh second_reference{36.0 * pi / 180.0, 140.0 * pi / 180.0, 20.0};
    LoadedFiles files;
    files.push_back(file_with_samples("one.pos",
        {
            sample_at(0, first_reference),
            sample_at(10, Wgs84Llh{35.001 * pi / 180.0, 139.002 * pi / 180.0, 15.0}),
        }));
    files.push_back(file_with_samples("two.pos",
        {
            sample_at(0, second_reference),
        }));

    EnuCache cache;
    const EnuReferenceConfiguration configuration;
    check(rebuild_enu_cache(files, TimeRange{GpsTime{0}, GpsTime{10}}, configuration, cache)
            == EnuCacheUpdateStatus::Updated,
        "initial ENU cache build succeeds");
    check(cache.reference.has_value() && cache.revision == 1,
        "successful cache build stores a reference and advances revision");
    check(near(files[0].samples[0].enu.east_m, 0.0) && near(files[0].samples[0].enu.north_m, 0.0)
            && near(files[0].samples[0].enu.up_m, 0.0),
        "reference sample maps to the ENU origin");
    const Enu second_file_before = files[1].samples[0].enu;

    check(move_slot(files, 2, 1), "slot order changes for cache invalidation test");
    check(rebuild_enu_cache(files, TimeRange{GpsTime{0}, GpsTime{10}}, configuration, cache)
            == EnuCacheUpdateStatus::Updated,
        "slot 1 change rebuilds the ENU cache");
    check(cache.revision == 2, "second successful cache build advances revision");
    check(near(files[0].samples[0].enu.east_m, 0.0) && near(files[0].samples[0].enu.north_m, 0.0)
            && near(files[0].samples[0].enu.up_m, 0.0),
        "new slot 1 sample becomes the ENU origin");
    check(!near(second_file_before.east_m, files[0].samples[0].enu.east_m)
            || !near(second_file_before.north_m, files[0].samples[0].enu.north_m),
        "changing slot 1 changes derived ENU coordinates");

    const Enu retained_value = files[1].samples[0].enu;
    check(rebuild_enu_cache(files, TimeRange{GpsTime{100}, GpsTime{200}}, configuration, cache)
            == EnuCacheUpdateStatus::RetainedPreviousReference,
        "empty slot 1 range retains a previous valid reference");
    check(cache.revision == 2 && near(files[1].samples[0].enu.east_m, retained_value.east_m),
        "failed rebuild preserves revision and cached coordinates");
    check(diagnostic_count(files[0], DiagnosticCode::EmptyEnuReferenceRange) == 1,
        "empty slot 1 range produces a diagnostic");

    EnuCache empty_cache;
    check(
        rebuild_enu_cache(files, TimeRange{GpsTime{100}, GpsTime{200}}, configuration, empty_cache)
            == EnuCacheUpdateStatus::Unavailable,
        "empty range without a previous reference leaves ENU unavailable");
    check(!empty_cache.reference.has_value() && empty_cache.revision == 0,
        "unavailable cache has no reference or revision");
}

} // namespace

int main()
{
    test_slot_reference_methods();
    test_user_specified_reference();
    test_cache_rebuild_and_retention();

    if (failures != 0) {
        std::cerr << failures << " ENU test(s) failed\n";
        return 1;
    }
    return 0;
}
