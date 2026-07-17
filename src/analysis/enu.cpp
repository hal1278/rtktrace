#include "plotcore/analysis/enu.hpp"

#include <cmath>
#include <limits>
#include <numbers>

namespace plotcore {
namespace {

[[nodiscard]] std::optional<EnuReference> reference_from_sample(
    const NormalizedSample& sample) noexcept
{
    if (!std::isfinite(sample.ecef.x_m) || !std::isfinite(sample.ecef.y_m)
        || !std::isfinite(sample.ecef.z_m) || !std::isfinite(sample.llh.latitude_rad)
        || !std::isfinite(sample.llh.longitude_rad)) {
        return std::nullopt;
    }
    return EnuReference{sample.ecef, sample.llh.latitude_rad, sample.llh.longitude_rad};
}

[[nodiscard]] std::optional<EnuReference> slot_reference(
    const LoadedFile& file, TimeRange range, EnuReferenceMethod method) noexcept
{
    if (range.start > range.end) {
        return std::nullopt;
    }
    if (method == EnuReferenceMethod::Slot1Start) {
        for (const NormalizedSample& sample : file.samples) {
            if (contains(range, sample.time)) {
                return reference_from_sample(sample);
            }
        }
        return std::nullopt;
    }
    if (method == EnuReferenceMethod::Slot1End) {
        for (auto sample = file.samples.rbegin(); sample != file.samples.rend(); ++sample) {
            if (contains(range, sample->time)) {
                return reference_from_sample(*sample);
            }
        }
        return std::nullopt;
    }

    long double sum_x = 0.0L;
    long double sum_y = 0.0L;
    long double sum_z = 0.0L;
    std::size_t count = 0;
    for (const NormalizedSample& sample : file.samples) {
        if (!contains(range, sample.time)) {
            continue;
        }
        if (!std::isfinite(sample.ecef.x_m) || !std::isfinite(sample.ecef.y_m)
            || !std::isfinite(sample.ecef.z_m)) {
            return std::nullopt;
        }
        sum_x += sample.ecef.x_m;
        sum_y += sample.ecef.y_m;
        sum_z += sample.ecef.z_m;
        ++count;
    }
    if (count == 0) {
        return std::nullopt;
    }
    const long double divisor = static_cast<long double>(count);
    return make_enu_reference(Ecef{
        static_cast<double>(sum_x / divisor),
        static_cast<double>(sum_y / divisor),
        static_cast<double>(sum_z / divisor),
    });
}

[[nodiscard]] std::optional<EnuReference> user_reference(
    const EnuReferenceConfiguration& configuration) noexcept
{
    if (!configuration.user_position.has_value()) {
        return std::nullopt;
    }
    if (const auto* llh = std::get_if<UserSpecifiedLlh>(&*configuration.user_position)) {
        return make_enu_reference(Wgs84Llh{
            llh->latitude_deg * std::numbers::pi_v<double> / 180.0,
            llh->longitude_deg * std::numbers::pi_v<double> / 180.0,
            llh->ellipsoidal_height_m,
        });
    }
    return make_enu_reference(std::get<Ecef>(*configuration.user_position));
}

void add_empty_reference_diagnostic(LoadedFiles& files)
{
    LoadedFile* file = reference_file(files);
    if (file == nullptr) {
        return;
    }
    file->diagnostics.push_back(Diagnostic{
        .severity = DiagnosticSeverity::Warning,
        .code = DiagnosticCode::EmptyEnuReferenceRange,
        .file_name = file->source_path.filename().string(),
        .source_line_number = std::nullopt,
        .time = std::nullopt,
        .message = "Slot 1 has no sample in the ENU reference range",
        .action = DiagnosticAction::Ignored,
    });
}

} // namespace

std::optional<EnuReference> determine_enu_reference(const LoadedFiles& files,
    TimeRange range, const EnuReferenceConfiguration& configuration) noexcept
{
    if (configuration.method == EnuReferenceMethod::UserSpecified) {
        return user_reference(configuration);
    }
    const LoadedFile* file = reference_file(files);
    if (file == nullptr) {
        return std::nullopt;
    }
    return slot_reference(*file, range, configuration.method);
}

EnuCacheUpdateStatus rebuild_enu_cache(LoadedFiles& files, TimeRange range,
    const EnuReferenceConfiguration& configuration, EnuCache& cache)
{
    const std::optional<EnuReference> reference =
        determine_enu_reference(files, range, configuration);
    if (!reference.has_value()) {
        if (configuration.method != EnuReferenceMethod::UserSpecified) {
            add_empty_reference_diagnostic(files);
        }
        return cache.reference.has_value()
            ? EnuCacheUpdateStatus::RetainedPreviousReference
            : EnuCacheUpdateStatus::Unavailable;
    }

    for (LoadedFile& file : files) {
        for (NormalizedSample& sample : file.samples) {
            sample.enu = ecef_to_enu(sample.ecef, *reference);
        }
    }
    cache.reference = reference;
    if (cache.revision != std::numeric_limits<std::uint64_t>::max()) {
        ++cache.revision;
    }
    return EnuCacheUpdateStatus::Updated;
}

} // namespace plotcore
