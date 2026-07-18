#include "plotcore/session_state.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

#include "plotcore/analysis/sample_rate.hpp"

namespace plotcore {
namespace {

[[nodiscard]] std::string lowercase_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

[[nodiscard]] bool has_severity(const LoadedFile& file, DiagnosticSeverity severity) noexcept
{
    return std::any_of(file.diagnostics.begin(), file.diagnostics.end(),
        [severity](const Diagnostic& diagnostic) { return diagnostic.severity == severity; });
}

[[nodiscard]] Diagnostic io_diagnostic(const std::filesystem::path& path)
{
    return Diagnostic{
        DiagnosticSeverity::Fatal,
        DiagnosticCode::ParseError,
        path.filename().string(),
        std::nullopt,
        std::nullopt,
        "The input file could not be opened",
        DiagnosticAction::FileRejected,
    };
}

} // namespace

std::optional<InputFormat> infer_input_format(const std::filesystem::path& path)
{
    const std::string extension = lowercase_extension(path);
    if (extension == ".pos") {
        return InputFormat::Pos;
    }
    if (extension == ".nmea" || extension == ".gga") {
        return InputFormat::Nmea;
    }
    return std::nullopt;
}

FileLoadResult PlotSessionState::load_file(const std::filesystem::path& path,
    std::optional<InputFormat> format, NmeaParseOptions nmea_options)
{
    if (!format.has_value()) {
        format = infer_input_format(path);
    }
    if (!format.has_value()) {
        return FileLoadResult{FileLoadStatus::NeedsInputFormat, path, {}};
    }

    std::ifstream input{path};
    if (!input.is_open()) {
        Diagnostic diagnostic = io_diagnostic(path);
        diagnostic_history_.push_back(diagnostic);
        return FileLoadResult{FileLoadStatus::IoError, path, {std::move(diagnostic)}};
    }

    LoadedFile file = *format == InputFormat::Pos ? parse_pos(input, path)
                                                  : parse_nmea(input, path, nmea_options);
    if (has_severity(file, DiagnosticSeverity::RequiresDecision)) {
        return FileLoadResult{FileLoadStatus::NeedsNmeaDecision, path, file.diagnostics};
    }
    if (file.samples.empty() || has_severity(file, DiagnosticSeverity::Fatal)) {
        record_diagnostics(file);
        return FileLoadResult{FileLoadStatus::Rejected, path, file.diagnostics};
    }
    if (!file.estimated_hz().has_value()) {
        const std::optional<double> hz = estimate_sample_rate_hz(file.samples);
        if (hz.has_value()) {
            static_cast<void>(file.set_estimated_hz(*hz));
        }
    }
    const std::vector<Diagnostic> diagnostics = file.diagnostics;
    if (!add_loaded_file(std::move(file))) {
        return FileLoadResult{FileLoadStatus::Rejected, path, diagnostics};
    }
    return FileLoadResult{FileLoadStatus::Loaded, path, diagnostics};
}

bool PlotSessionState::add_loaded_file(LoadedFile file)
{
    if (file.samples.empty()) {
        return false;
    }
    if (!file.estimated_hz().has_value()) {
        const std::optional<double> hz = estimate_sample_rate_hz(file.samples);
        if (hz.has_value()) {
            static_cast<void>(file.set_estimated_hz(*hz));
        }
    }
    record_diagnostics(file);
    files_.push_back(std::move(file));
    if (!rebuild_processing_state()) {
        files_.pop_back();
        static_cast<void>(rebuild_processing_state());
        return false;
    }
    return true;
}

bool PlotSessionState::set_file_visible(std::size_t slot_number, bool visible)
{
    LoadedFile* file = file_at_slot(files_, slot_number);
    if (file == nullptr) {
        return false;
    }
    if (file->visible != visible) {
        file->visible = visible;
        if (revision_ != std::numeric_limits<std::uint64_t>::max()) {
            ++revision_;
        }
    }
    return true;
}

bool PlotSessionState::move_file(std::size_t from_slot_number, std::size_t to_slot_number)
{
    if (!move_slot(files_, from_slot_number, to_slot_number)) {
        return false;
    }
    return rebuild_processing_state();
}

bool PlotSessionState::erase_file(std::size_t slot_number)
{
    if (!erase_slot(files_, slot_number)) {
        return false;
    }
    return rebuild_processing_state();
}

bool PlotSessionState::set_file_override_hz(std::size_t slot_number, std::optional<double> hz)
{
    LoadedFile* file = file_at_slot(files_, slot_number);
    if (file == nullptr) {
        return false;
    }
    if (hz.has_value()) {
        return file->set_override_hz(*hz);
    }
    file->clear_override_hz();
    return true;
}

bool PlotSessionState::set_common_time_range(CommonTimeRange range)
{
    const std::optional<TimeRange> union_range = union_time_range(files_);
    if (!union_range.has_value() || !effective_time_range(range, *union_range).has_value()) {
        return false;
    }
    const CommonTimeRange previous = configured_time_range_;
    configured_time_range_ = range;
    if (!rebuild_processing_state()) {
        configured_time_range_ = previous;
        static_cast<void>(rebuild_processing_state());
        return false;
    }
    return true;
}

bool PlotSessionState::use_intersection_time_range()
{
    CommonTimeRange range = configured_time_range_;
    if (!apply_intersection(range, files_)) {
        return false;
    }
    return set_common_time_range(range);
}

bool PlotSessionState::set_enu_reference_configuration(EnuReferenceConfiguration configuration)
{
    if (!files_.empty()
        && (!effective_range_.has_value()
            || !determine_enu_reference(files_, *effective_range_, configuration).has_value())) {
        return false;
    }
    const EnuReferenceConfiguration previous = enu_configuration_;
    enu_configuration_ = std::move(configuration);
    if (!rebuild_processing_state()) {
        enu_configuration_ = previous;
        static_cast<void>(rebuild_processing_state());
        return false;
    }
    return true;
}

bool PlotSessionState::set_reference_match_configuration(ReferenceMatchConfiguration configuration)
{
    if (configuration.maximum_time_difference_ns < 0) {
        return false;
    }
    match_configuration_ = configuration;
    if (!files_.empty()) {
        static_cast<void>(rebuild_relative_cache(
            files_, time_index_, enu_cache_, match_configuration_, relative_cache_));
    }
    if (revision_ != std::numeric_limits<std::uint64_t>::max()) {
        ++revision_;
    }
    return true;
}

std::optional<PlotDataView> PlotSessionState::normal_plot_data_view() const
{
    if (!enu_cache_.reference.has_value()) {
        return std::nullopt;
    }
    return make_normal_plot_data_view(files_, time_index_);
}

std::optional<PlotDataView> PlotSessionState::relative_plot_data_view() const
{
    return make_relative_plot_data_view(files_, relative_cache_);
}

std::optional<TimeRange> PlotSessionState::effective_range() const noexcept
{
    return effective_range_;
}

std::vector<RecordedStatistics> PlotSessionState::recorded_statistics() const
{
    std::vector<RecordedStatistics> result;
    if (!effective_range_.has_value()) {
        return result;
    }
    result.reserve(files_.size());
    for (const LoadedFile& file : files_) {
        result.push_back(calculate_recorded_statistics(file.samples, *effective_range_));
    }
    return result;
}

const LoadedFiles& PlotSessionState::files() const noexcept
{
    return files_;
}

const CommonTimeRange& PlotSessionState::configured_time_range() const noexcept
{
    return configured_time_range_;
}

const EnuReferenceConfiguration& PlotSessionState::enu_configuration() const noexcept
{
    return enu_configuration_;
}

const ReferenceMatchConfiguration& PlotSessionState::match_configuration() const noexcept
{
    return match_configuration_;
}

const std::vector<Diagnostic>& PlotSessionState::diagnostic_history() const noexcept
{
    return diagnostic_history_;
}

std::uint64_t PlotSessionState::revision() const noexcept
{
    return revision_;
}

bool PlotSessionState::enu_available() const noexcept
{
    return enu_cache_.reference.has_value();
}

bool PlotSessionState::rebuild_processing_state()
{
    if (files_.empty()) {
        time_index_ = CommonTimeRangeIndex{};
        enu_cache_ = EnuCache{};
        relative_cache_ = RelativeCache{};
        effective_range_.reset();
        if (revision_ != std::numeric_limits<std::uint64_t>::max()) {
            ++revision_;
        }
        return true;
    }
    const std::optional<TimeRange> union_range = union_time_range(files_);
    if (!union_range.has_value()) {
        return false;
    }
    const std::optional<TimeRange> range =
        effective_time_range(configured_time_range_, *union_range);
    if (!range.has_value() || !rebuild_common_time_range_index(files_, *range, time_index_)) {
        return false;
    }
    static_cast<void>(rebuild_enu_cache(files_, *range, enu_configuration_, enu_cache_));
    static_cast<void>(rebuild_relative_cache(
        files_, time_index_, enu_cache_, match_configuration_, relative_cache_));
    effective_range_ = range;
    if (revision_ != std::numeric_limits<std::uint64_t>::max()) {
        ++revision_;
    }
    return true;
}

void PlotSessionState::record_diagnostics(const LoadedFile& file)
{
    diagnostic_history_.insert(
        diagnostic_history_.end(), file.diagnostics.begin(), file.diagnostics.end());
}

} // namespace plotcore
