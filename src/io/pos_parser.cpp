#include "plotcore/io/pos_parser.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "plotcore/analysis/coordinates.hpp"
#include "plotcore/analysis/gps_time.hpp"

namespace plotcore {
namespace {

constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
constexpr std::int64_t seconds_per_week = 604'800;

[[nodiscard]] std::vector<std::string_view> split_fields(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t')) {
            ++position;
        }
        const std::size_t start = position;
        while (position < line.size() && line[position] != ' ' && line[position] != '\t') {
            ++position;
        }
        if (start != position) {
            fields.push_back(line.substr(start, position - start));
        }
    }
    return fields;
}

template <typename Integer>
[[nodiscard]] bool parse_integer(std::string_view field, Integer& value) noexcept
{
    const auto result = std::from_chars(field.data(), field.data() + field.size(), value);
    return result.ec == std::errc{} && result.ptr == field.data() + field.size();
}

[[nodiscard]] bool parse_finite_double(std::string_view field, double& value) noexcept
{
    const auto result = std::from_chars(field.data(), field.data() + field.size(), value);
    return result.ec == std::errc{} && result.ptr == field.data() + field.size()
        && std::isfinite(value);
}

[[nodiscard]] std::optional<std::int64_t> parse_nonnegative_seconds_ns(
    std::string_view field) noexcept
{
    const std::size_t decimal = field.find('.');
    if (decimal != std::string_view::npos
        && field.find('.', decimal + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view whole_field = field.substr(0, decimal);
    const std::string_view fraction_field =
        decimal == std::string_view::npos ? std::string_view{} : field.substr(decimal + 1);
    if (whole_field.empty() || (decimal != std::string_view::npos && fraction_field.empty())) {
        return std::nullopt;
    }

    std::int64_t whole_seconds = 0;
    if (!parse_integer(whole_field, whole_seconds) || whole_seconds < 0) {
        return std::nullopt;
    }
    const std::optional<std::int64_t> fraction_ns =
        round_fractional_seconds_to_nanoseconds(fraction_field);
    if (!fraction_ns.has_value()) {
        return std::nullopt;
    }

    const std::int64_t carry = *fraction_ns / nanoseconds_per_second;
    const std::int64_t remainder = *fraction_ns % nanoseconds_per_second;
    if (whole_seconds
        > (std::numeric_limits<std::int64_t>::max() - remainder) / nanoseconds_per_second - carry) {
        return std::nullopt;
    }
    return (whole_seconds + carry) * nanoseconds_per_second + remainder;
}

[[nodiscard]] std::optional<GpsTime> parse_week_tow(
    std::string_view week_field, std::string_view tow_field) noexcept
{
    std::int64_t week = 0;
    const std::optional<std::int64_t> tow_ns = parse_nonnegative_seconds_ns(tow_field);
    if (!parse_integer(week_field, week) || week < 0 || !tow_ns.has_value()
        || *tow_ns >= seconds_per_week * nanoseconds_per_second) {
        return std::nullopt;
    }
    constexpr std::int64_t nanoseconds_per_week = seconds_per_week * nanoseconds_per_second;
    if (week > (std::numeric_limits<std::int64_t>::max() - *tow_ns) / nanoseconds_per_week) {
        return std::nullopt;
    }
    return GpsTime{week * nanoseconds_per_week + *tow_ns};
}

[[nodiscard]] bool parse_fixed_unsigned(std::string_view field, unsigned& value) noexcept
{
    return !field.empty() && parse_integer(field, value);
}

[[nodiscard]] std::optional<GpsTime> parse_calendar(
    std::string_view date_field, std::string_view time_field) noexcept
{
    if (date_field.size() != 10 || date_field[4] != '/' || date_field[7] != '/'
        || time_field.size() < 8 || time_field[2] != ':' || time_field[5] != ':') {
        return std::nullopt;
    }

    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    unsigned hour = 0;
    unsigned minute = 0;
    if (!parse_integer(date_field.substr(0, 4), year)
        || !parse_fixed_unsigned(date_field.substr(5, 2), month)
        || !parse_fixed_unsigned(date_field.substr(8, 2), day)
        || !parse_fixed_unsigned(time_field.substr(0, 2), hour)
        || !parse_fixed_unsigned(time_field.substr(3, 2), minute)) {
        return std::nullopt;
    }

    const std::string_view seconds_field = time_field.substr(6);
    const std::size_t decimal = seconds_field.find('.');
    if (decimal != std::string_view::npos
        && seconds_field.find('.', decimal + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    unsigned second = 0;
    const std::string_view whole_second = seconds_field.substr(0, decimal);
    const std::string_view fraction =
        decimal == std::string_view::npos ? std::string_view{} : seconds_field.substr(decimal + 1);
    if (!parse_fixed_unsigned(whole_second, second) || second >= 60
        || (decimal != std::string_view::npos && fraction.empty())) {
        return std::nullopt;
    }
    const std::optional<std::int64_t> fraction_ns =
        round_fractional_seconds_to_nanoseconds(fraction);
    if (!fraction_ns.has_value()) {
        return std::nullopt;
    }
    const std::uint32_t nanosecond =
        static_cast<std::uint32_t>(*fraction_ns % nanoseconds_per_second);
    std::optional<GpsTime> result =
        gps_civil_to_gps_time(GpsCivilTime{year, month, day, hour, minute, second, nanosecond});
    if (result.has_value() && *fraction_ns == nanoseconds_per_second) {
        if (result->nanoseconds_since_gps_epoch
            > std::numeric_limits<std::int64_t>::max() - nanoseconds_per_second) {
            return std::nullopt;
        }
        result->nanoseconds_since_gps_epoch += nanoseconds_per_second;
    }
    return result;
}

[[nodiscard]] SolutionQuality normalize_quality(int quality) noexcept
{
    switch (quality) {
    case 1:
        return SolutionQuality::Fixed;
    case 2:
        return SolutionQuality::Float;
    case 3:
        return SolutionQuality::Sbas;
    case 4:
        return SolutionQuality::Dgps;
    case 5:
        return SolutionQuality::Single;
    case 6:
        return SolutionQuality::Ppp;
    default:
        return SolutionQuality::InvalidOrUnknown;
    }
}

void add_diagnostic(LoadedFile& file, DiagnosticSeverity severity, DiagnosticCode code,
    std::optional<std::size_t> line_number, std::optional<GpsTime> time, std::string message,
    DiagnosticAction action)
{
    file.diagnostics.push_back(Diagnostic{
        .severity = severity,
        .code = code,
        .file_name = file.source_path.filename().string(),
        .source_line_number = line_number,
        .time = time,
        .message = std::move(message),
        .action = action,
    });
}

[[nodiscard]] std::optional<NormalizedSample> parse_record(
    const std::vector<std::string_view>& fields, std::size_t line_number, LoadedFile& file)
{
    if (fields.size() < 6) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::MissingField, line_number,
            std::nullopt, "POS record has fewer than six fields", DiagnosticAction::SampleRemoved);
        return std::nullopt;
    }

    const bool calendar_format = fields[0].find('/') != std::string_view::npos;
    const std::optional<GpsTime> time = calendar_format ? parse_calendar(fields[0], fields[1])
                                                        : parse_week_tow(fields[0], fields[1]);
    if (!time.has_value()) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::InvalidTime, line_number,
            std::nullopt, "POS record has an invalid time", DiagnosticAction::SampleRemoved);
        return std::nullopt;
    }

    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double height_m = 0.0;
    if (!parse_finite_double(fields[2], latitude_deg)
        || !parse_finite_double(fields[3], longitude_deg)
        || !parse_finite_double(fields[4], height_m)) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::ParseError, line_number,
            time, "POS record has an invalid coordinate", DiagnosticAction::SampleRemoved);
        return std::nullopt;
    }

    int input_quality = 0;
    if (!parse_integer(fields[5], input_quality)) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::ParseError, line_number,
            time, "POS record has an invalid quality field", DiagnosticAction::SampleRemoved);
        return std::nullopt;
    }
    const SolutionQuality quality = normalize_quality(input_quality);
    if (quality == SolutionQuality::InvalidOrUnknown) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::UnknownPosQuality,
            line_number, time, "Unknown POS quality was loaded as quality zero",
            DiagnosticAction::LoadedAsQualityZero);
    }

    const Wgs84Llh llh{
        .latitude_rad = latitude_deg * std::numbers::pi_v<double> / 180.0,
        .longitude_rad = longitude_deg * std::numbers::pi_v<double> / 180.0,
        .ellipsoidal_height_m = height_m,
    };
    return NormalizedSample{
        .time = *time,
        .llh = llh,
        .ecef = wgs84_llh_to_ecef(llh),
        .enu = {},
        .quality = quality,
        .source_line_number = line_number,
        .continuous_from_previous = false,
    };
}

} // namespace

LoadedFile parse_pos(
    std::istream& input, std::filesystem::path source_path, PosParseOptions options)
{
    LoadedFile file{std::move(source_path), InputFormat::Pos};
    if (options.duplicate_epoch_tolerance_ns < 0) {
        add_diagnostic(file, DiagnosticSeverity::Fatal, DiagnosticCode::ParseError, std::nullopt,
            std::nullopt, "Duplicate epoch tolerance must not be negative",
            DiagnosticAction::FileRejected);
        return file;
    }

    std::string line;
    std::size_t line_number = 0;
    bool break_continuity = false;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::vector<std::string_view> fields = split_fields(line);
        if (fields.empty() || fields.front().starts_with('%')) {
            continue;
        }

        std::optional<NormalizedSample> sample = parse_record(fields, line_number, file);
        if (!sample.has_value()) {
            break_continuity = true;
            continue;
        }
        if (file.samples.empty()) {
            file.samples.push_back(*sample);
            break_continuity = false;
            continue;
        }

        const GpsTime previous_time = file.samples.back().time;
        const std::int64_t absolute_difference = sample->time >= previous_time
            ? sample->time - previous_time
            : previous_time - sample->time;
        if (absolute_difference <= options.duplicate_epoch_tolerance_ns) {
            sample->continuous_from_previous = false;
            file.samples.back() = *sample;
            add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::DuplicateEpoch,
                line_number, sample->time, "Duplicate POS epoch replaced the previous sample",
                DiagnosticAction::SampleReplaced);
            break_continuity = false;
            continue;
        }
        if (sample->time < previous_time) {
            add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::TimeReversal,
                line_number, sample->time, "Time-reversed POS record was removed",
                DiagnosticAction::SampleRemoved);
            break_continuity = true;
            continue;
        }

        sample->continuous_from_previous = !break_continuity;
        file.samples.push_back(*sample);
        break_continuity = false;
    }

    if (file.samples.empty()) {
        add_diagnostic(file, DiagnosticSeverity::Fatal, DiagnosticCode::ParseError, std::nullopt,
            std::nullopt, "POS file contains no valid position samples",
            DiagnosticAction::FileRejected);
    }
    return file;
}

} // namespace plotcore
