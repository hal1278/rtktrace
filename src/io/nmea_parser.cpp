#include "rtktrace/io/nmea_parser.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rtktrace/analysis/coordinates.hpp"
#include "rtktrace/analysis/gps_time.hpp"

namespace rtktrace {
namespace {

constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
constexpr std::int64_t nanoseconds_per_day = 86'400 * nanoseconds_per_second;

struct TimeOfDay {
    unsigned hour;
    unsigned minute;
    unsigned second;
    std::uint32_t nanosecond;
    std::int64_t ordering_nanoseconds;
    int date_carry;
};

struct RawGga {
    TimeOfDay time;
    double latitude_deg;
    double longitude_deg;
    double altitude_m;
    std::optional<double> geoid_separation_m;
    int quality;
    bool is_gn;
    std::size_t line_number;
    bool break_before;
};

struct DateReference {
    UtcCivilTime utc;
    TimeOfDay time;
    std::size_t line_number;
};

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

[[nodiscard]] std::vector<std::string_view> split_commas(std::string_view body)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = body.find(',', start);
        fields.push_back(body.substr(start, comma - start));
        if (comma == std::string_view::npos) {
            return fields;
        }
        start = comma + 1;
    }
}

[[nodiscard]] int hexadecimal_digit(char value) noexcept
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

[[nodiscard]] std::optional<std::string_view> checked_body(std::string_view line) noexcept
{
    if (line.size() < 4 || line.front() != '$' || line[line.size() - 3] != '*') {
        return std::nullopt;
    }
    for (const unsigned char byte : line) {
        if (byte > 0x7f) {
            return std::nullopt;
        }
    }
    const int high = hexadecimal_digit(line[line.size() - 2]);
    const int low = hexadecimal_digit(line[line.size() - 1]);
    if (high < 0 || low < 0) {
        return std::nullopt;
    }
    unsigned char checksum = 0;
    for (std::size_t index = 1; index < line.size() - 3; ++index) {
        checksum ^= static_cast<unsigned char>(line[index]);
    }
    if (checksum != static_cast<unsigned char>((high << 4) | low)) {
        return std::nullopt;
    }
    return line.substr(1, line.size() - 4);
}

[[nodiscard]] std::optional<TimeOfDay> parse_time_of_day(std::string_view field) noexcept
{
    if (field.size() < 6) {
        return std::nullopt;
    }
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;
    if (!parse_integer(field.substr(0, 2), hour) || !parse_integer(field.substr(2, 2), minute)
        || !parse_integer(field.substr(4, 2), second) || hour >= 24 || minute >= 60
        || second > 60) {
        return std::nullopt;
    }
    const std::string_view suffix = field.substr(6);
    if (!suffix.empty() && suffix.front() != '.') {
        return std::nullopt;
    }
    const std::string_view fractional = suffix.empty() ? std::string_view{} : suffix.substr(1);
    if (!suffix.empty() && fractional.empty()) {
        return std::nullopt;
    }
    const std::optional<std::int64_t> rounded = round_fractional_seconds_to_nanoseconds(fractional);
    if (!rounded.has_value()) {
        return std::nullopt;
    }

    if (second == 60) {
        if (*rounded == nanoseconds_per_second) {
            return std::nullopt;
        }
        return TimeOfDay{hour, minute, second, static_cast<std::uint32_t>(*rounded),
            (static_cast<std::int64_t>(hour) * 3'600 + static_cast<std::int64_t>(minute) * 60 + 60)
                    * nanoseconds_per_second
                + *rounded,
            0};
    }

    std::int64_t total_seconds = static_cast<std::int64_t>(hour) * 3'600
        + static_cast<std::int64_t>(minute) * 60 + second + *rounded / nanoseconds_per_second;
    const int date_carry = static_cast<int>(total_seconds / 86'400);
    total_seconds %= 86'400;
    return TimeOfDay{
        static_cast<unsigned>(total_seconds / 3'600),
        static_cast<unsigned>((total_seconds % 3'600) / 60),
        static_cast<unsigned>(total_seconds % 60),
        static_cast<std::uint32_t>(*rounded % nanoseconds_per_second),
        total_seconds * nanoseconds_per_second + *rounded % nanoseconds_per_second,
        date_carry,
    };
}

[[nodiscard]] bool parse_coordinate(std::string_view value_field, std::string_view hemisphere,
    bool latitude, double& degrees) noexcept
{
    double raw = 0.0;
    if (!parse_finite_double(value_field, raw) || hemisphere.size() != 1) {
        return false;
    }
    const double whole_degrees = std::trunc(raw / 100.0);
    degrees = whole_degrees + (raw - whole_degrees * 100.0) / 60.0;
    const char direction = hemisphere.front();
    if ((latitude && direction == 'S') || (!latitude && direction == 'W')) {
        degrees = -degrees;
        return true;
    }
    return (latitude && direction == 'N') || (!latitude && direction == 'E');
}

[[nodiscard]] std::optional<std::chrono::sys_days> to_days(NmeaDate date) noexcept
{
    const std::chrono::year_month_day civil{
        std::chrono::year{date.year}, std::chrono::month{date.month}, std::chrono::day{date.day}};
    if (!civil.ok()) {
        return std::nullopt;
    }
    return std::chrono::sys_days{civil};
}

[[nodiscard]] UtcCivilTime make_utc(std::chrono::sys_days day, const TimeOfDay& time) noexcept
{
    day += std::chrono::days{time.date_carry};
    const std::chrono::year_month_day civil{day};
    return UtcCivilTime{static_cast<int>(civil.year()), static_cast<unsigned>(civil.month()),
        static_cast<unsigned>(civil.day()), time.hour, time.minute, time.second, time.nanosecond};
}

[[nodiscard]] std::optional<DateReference> parse_rmc(
    const std::vector<std::string_view>& fields, std::size_t line_number, LoadedFile& file)
{
    if (fields.size() <= 9) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::MissingField, line_number,
            std::nullopt, "RMC sentence has missing date/time fields", DiagnosticAction::Ignored);
        return std::nullopt;
    }
    const std::optional<TimeOfDay> time = parse_time_of_day(fields[1]);
    const std::string_view date = fields[9];
    unsigned day = 0;
    unsigned month = 0;
    unsigned short_year = 0;
    if (!time.has_value() || date.size() != 6 || !parse_integer(date.substr(0, 2), day)
        || !parse_integer(date.substr(2, 2), month)
        || !parse_integer(date.substr(4, 2), short_year)) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::InvalidTime, line_number,
            std::nullopt, "RMC sentence has an invalid UTC date/time", DiagnosticAction::Ignored);
        return std::nullopt;
    }
    const int year = short_year >= 80 ? 1900 + static_cast<int>(short_year)
                                      : 2000 + static_cast<int>(short_year);
    const std::optional<std::chrono::sys_days> days = to_days(NmeaDate{year, month, day});
    if (!days.has_value()) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::InvalidTime, line_number,
            std::nullopt, "RMC sentence has an invalid UTC date", DiagnosticAction::Ignored);
        return std::nullopt;
    }
    const UtcCivilTime utc = make_utc(*days, *time);
    if (!utc_civil_to_gps_time(utc).has_value()) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::InvalidTime, line_number,
            std::nullopt, "RMC sentence is outside the supported UTC range",
            DiagnosticAction::Ignored);
        return std::nullopt;
    }
    if (fields.size() > 2 && fields[2] == "V") {
        add_diagnostic(file, DiagnosticSeverity::Info, DiagnosticCode::VoidRmcStatus, line_number,
            utc_civil_to_gps_time(utc), "RMC status V was retained as a date/time reference",
            DiagnosticAction::Ignored);
    }
    return DateReference{utc, *time, line_number};
}

[[nodiscard]] std::optional<DateReference> parse_zda(
    const std::vector<std::string_view>& fields, std::size_t line_number, LoadedFile& file)
{
    if (fields.size() <= 4) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::MissingField, line_number,
            std::nullopt, "ZDA sentence has missing date/time fields", DiagnosticAction::Ignored);
        return std::nullopt;
    }
    const std::optional<TimeOfDay> time = parse_time_of_day(fields[1]);
    unsigned day = 0;
    unsigned month = 0;
    int year = 0;
    if (!time.has_value() || !parse_integer(fields[2], day) || !parse_integer(fields[3], month)
        || !parse_integer(fields[4], year)) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::InvalidTime, line_number,
            std::nullopt, "ZDA sentence has an invalid UTC date/time", DiagnosticAction::Ignored);
        return std::nullopt;
    }
    const std::optional<std::chrono::sys_days> days = to_days(NmeaDate{year, month, day});
    if (!days.has_value()) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::InvalidTime, line_number,
            std::nullopt, "ZDA sentence has an invalid UTC date", DiagnosticAction::Ignored);
        return std::nullopt;
    }
    const UtcCivilTime utc = make_utc(*days, *time);
    if (!utc_civil_to_gps_time(utc).has_value()) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::InvalidTime, line_number,
            std::nullopt, "ZDA sentence is outside the supported UTC range",
            DiagnosticAction::Ignored);
        return std::nullopt;
    }
    return DateReference{utc, *time, line_number};
}

[[nodiscard]] std::optional<RawGga> parse_gga(const std::vector<std::string_view>& fields,
    bool is_gn, std::size_t line_number, bool break_before, LoadedFile& file)
{
    if (fields.size() <= 11) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::MissingField, line_number,
            std::nullopt, "GGA sentence has missing position fields",
            DiagnosticAction::SampleRemoved);
        return std::nullopt;
    }
    const std::optional<TimeOfDay> time = parse_time_of_day(fields[1]);
    if (!time.has_value()) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::InvalidTime, line_number,
            std::nullopt, "GGA sentence has an invalid UTC time", DiagnosticAction::SampleRemoved);
        return std::nullopt;
    }
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    int quality = 0;
    if (!parse_coordinate(fields[2], fields[3], true, latitude)
        || !parse_coordinate(fields[4], fields[5], false, longitude)
        || !parse_integer(fields[6], quality) || !parse_finite_double(fields[9], altitude)) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::ParseError, line_number,
            std::nullopt, "GGA sentence has an invalid position or quality field",
            DiagnosticAction::SampleRemoved);
        return std::nullopt;
    }
    std::optional<double> geoid;
    if (!fields[11].empty()) {
        double value = 0.0;
        if (!parse_finite_double(fields[11], value)) {
            add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::ParseError,
                line_number, std::nullopt, "GGA sentence has an invalid geoid separation",
                DiagnosticAction::SampleRemoved);
            return std::nullopt;
        }
        geoid = value;
    }
    return RawGga{
        *time, latitude, longitude, altitude, geoid, quality, is_gn, line_number, break_before};
}

[[nodiscard]] SolutionQuality normalize_nmea_quality(int quality) noexcept
{
    switch (quality) {
    case 1:
        return SolutionQuality::Single;
    case 2:
        return SolutionQuality::Dgps;
    case 3:
        return SolutionQuality::Ppp;
    case 4:
        return SolutionQuality::Fixed;
    case 5:
        return SolutionQuality::Float;
    default:
        return SolutionQuality::InvalidOrUnknown;
    }
}

[[nodiscard]] std::int64_t absolute_difference(GpsTime lhs, GpsTime rhs) noexcept
{
    return lhs >= rhs ? lhs - rhs : rhs - lhs;
}

} // namespace

LoadedFile parse_nmea(
    std::istream& input, std::filesystem::path source_path, NmeaParseOptions options)
{
    LoadedFile file{std::move(source_path), InputFormat::Nmea};
    if (options.duplicate_epoch_tolerance_ns < 0 || options.rollover_tolerance_ns < 0
        || options.rollover_tolerance_ns >= nanoseconds_per_day
        || options.datetime_validation_tolerance_ns < 0 || options.rate_min_interval_ns < 0) {
        add_diagnostic(file, DiagnosticSeverity::Fatal, DiagnosticCode::ParseError, std::nullopt,
            std::nullopt, "NMEA parser tolerances are invalid", DiagnosticAction::FileRejected);
        return file;
    }

    std::vector<RawGga> ggas;
    std::vector<DateReference> references;
    bool saw_gp = false;
    bool saw_gn = false;
    bool break_continuity = false;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::optional<std::string_view> body = checked_body(line);
        if (!body.has_value()) {
            const bool looks_like_gga = line.size() >= 6 && line.front() == '$'
                && std::string_view{line}.substr(3, 3) == "GGA";
            add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::ChecksumError,
                line_number, std::nullopt,
                "NMEA line failed ASCII, framing, or checksum validation",
                looks_like_gga ? DiagnosticAction::SampleRemoved : DiagnosticAction::Ignored);
            break_continuity = break_continuity || looks_like_gga;
            continue;
        }
        const std::vector<std::string_view> fields = split_commas(*body);
        if (fields.empty() || fields[0].size() != 5) {
            continue;
        }
        const std::string_view talker = fields[0].substr(0, 2);
        const std::string_view type = fields[0].substr(2, 3);
        if (type != "GGA" && type != "RMC" && type != "ZDA") {
            continue;
        }
        if (talker != "GP" && talker != "GN") {
            add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::UnsupportedTalker,
                line_number, std::nullopt, "Unsupported NMEA talker was ignored",
                type == "GGA" ? DiagnosticAction::SampleRemoved : DiagnosticAction::Ignored);
            if (type == "GGA") {
                break_continuity = true;
            }
            continue;
        }
        saw_gp = saw_gp || talker == "GP";
        saw_gn = saw_gn || talker == "GN";

        if (type == "GGA") {
            std::optional<RawGga> gga =
                parse_gga(fields, talker == "GN", line_number, break_continuity, file);
            if (gga.has_value()) {
                ggas.push_back(*gga);
                break_continuity = false;
            } else {
                break_continuity = true;
            }
        } else {
            std::optional<DateReference> reference = type == "RMC"
                ? parse_rmc(fields, line_number, file)
                : parse_zda(fields, line_number, file);
            if (reference.has_value()) {
                references.push_back(*reference);
            }
        }
    }

    if (saw_gp && saw_gn) {
        add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::MultipleTalkers,
            std::nullopt, std::nullopt, "Both GP and GN NMEA talkers are present",
            DiagnosticAction::Ignored);
    }
    if (ggas.empty()) {
        add_diagnostic(file, DiagnosticSeverity::Fatal, DiagnosticCode::ParseError, std::nullopt,
            std::nullopt, "NMEA file contains no valid GGA position samples",
            DiagnosticAction::FileRejected);
        return file;
    }

    const auto missing_geoid = std::find_if(ggas.begin(), ggas.end(),
        [](const RawGga& gga) { return !gga.geoid_separation_m.has_value(); });
    bool decision_required = false;
    if (missing_geoid != ggas.end()) {
        if (options.missing_geoid_policy == MissingGeoidPolicy::RequireDecision) {
            add_diagnostic(file, DiagnosticSeverity::RequiresDecision,
                DiagnosticCode::MissingGeoidSeparation, missing_geoid->line_number, std::nullopt,
                "GGA geoid separation is missing", DiagnosticAction::UserDecisionRequired);
            decision_required = true;
        } else if (options.missing_geoid_policy == MissingGeoidPolicy::RejectFile) {
            add_diagnostic(file, DiagnosticSeverity::Fatal, DiagnosticCode::MissingGeoidSeparation,
                missing_geoid->line_number, std::nullopt,
                "NMEA file was rejected because geoid separation is missing",
                DiagnosticAction::FileRejected);
            return file;
        }
    }

    std::optional<std::chrono::sys_days> fallback_days;
    if (references.empty()) {
        if (options.fallback_date.has_value()) {
            fallback_days = to_days(*options.fallback_date);
            if (!fallback_days.has_value()) {
                add_diagnostic(file, DiagnosticSeverity::Fatal, DiagnosticCode::InvalidTime,
                    std::nullopt, std::nullopt, "The supplied fallback NMEA date is invalid",
                    DiagnosticAction::FileRejected);
                return file;
            }
        } else {
            add_diagnostic(file, DiagnosticSeverity::RequiresDecision, DiagnosticCode::MissingDate,
                std::nullopt, std::nullopt, "NMEA file has no valid RMC or ZDA date reference",
                DiagnosticAction::UserDecisionRequired);
            decision_required = true;
        }
    }
    if (decision_required) {
        return file;
    }

    std::vector<std::chrono::sys_days> assigned_days(ggas.size());
    if (references.empty()) {
        assigned_days.front() = *fallback_days;
        for (std::size_t index = 1; index < ggas.size(); ++index) {
            assigned_days[index] = assigned_days[index - 1];
            const std::int64_t raw_delta =
                ggas[index].time.ordering_nanoseconds - ggas[index - 1].time.ordering_nanoseconds;
            if (raw_delta <= -(nanoseconds_per_day - options.rollover_tolerance_ns)) {
                assigned_days[index] += std::chrono::days{1};
            }
        }
    } else {
        const DateReference& basis = references.front();
        const std::chrono::year_month_day basis_civil{std::chrono::year{basis.utc.year},
            std::chrono::month{basis.utc.month}, std::chrono::day{basis.utc.day}};
        const std::chrono::sys_days basis_day{basis_civil};
        const auto first_after = std::lower_bound(ggas.begin(), ggas.end(), basis.line_number,
            [](const RawGga& gga, std::size_t line) { return gga.line_number < line; });
        const std::size_t split = static_cast<std::size_t>(first_after - ggas.begin());

        std::chrono::sys_days day = basis_day;
        std::int64_t next_time = basis.time.ordering_nanoseconds;
        for (std::size_t index = split; index > 0; --index) {
            const std::size_t current = index - 1;
            if (ggas[current].time.ordering_nanoseconds - next_time
                >= nanoseconds_per_day - options.rollover_tolerance_ns) {
                day -= std::chrono::days{1};
            }
            assigned_days[current] = day;
            next_time = ggas[current].time.ordering_nanoseconds;
        }

        day = basis_day;
        std::int64_t previous_time = basis.time.ordering_nanoseconds;
        for (std::size_t index = split; index < ggas.size(); ++index) {
            const std::int64_t raw_delta = ggas[index].time.ordering_nanoseconds - previous_time;
            if (raw_delta <= -(nanoseconds_per_day - options.rollover_tolerance_ns)) {
                day += std::chrono::days{1};
            }
            assigned_days[index] = day;
            previous_time = ggas[index].time.ordering_nanoseconds;
        }
    }

    std::vector<bool> retained_is_gn;
    bool normalization_break = false;
    for (std::size_t index = 0; index < ggas.size(); ++index) {
        const RawGga& gga = ggas[index];
        const UtcCivilTime utc = make_utc(assigned_days[index], gga.time);
        const std::optional<GpsTime> time = utc_civil_to_gps_time(utc);
        if (!time.has_value()) {
            add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::InvalidTime,
                gga.line_number, std::nullopt, "GGA UTC date/time is invalid",
                DiagnosticAction::SampleRemoved);
            normalization_break = true;
            continue;
        }
        SolutionQuality quality = normalize_nmea_quality(gga.quality);
        if (gga.quality < 0 || gga.quality > 6) {
            add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::UnknownNmeaQuality,
                gga.line_number, time, "Unknown NMEA quality was loaded as quality zero",
                DiagnosticAction::LoadedAsQualityZero);
            quality = SolutionQuality::InvalidOrUnknown;
        }
        double height = gga.altitude_m;
        if (gga.geoid_separation_m.has_value()) {
            height += *gga.geoid_separation_m;
        } else {
            add_diagnostic(file, DiagnosticSeverity::Info, DiagnosticCode::MissingGeoidSeparation,
                gga.line_number, time, "GGA altitude was loaded as ellipsoidal height",
                DiagnosticAction::LoadedAltitudeAsEllipsoidalHeight);
        }
        const Wgs84Llh llh{
            gga.latitude_deg * std::numbers::pi_v<double> / 180.0,
            gga.longitude_deg * std::numbers::pi_v<double> / 180.0,
            height,
        };
        NormalizedSample sample{
            *time, llh, wgs84_llh_to_ecef(llh), {}, quality, gga.line_number, false};
        if (file.samples.empty()) {
            file.samples.push_back(sample);
            retained_is_gn.push_back(gga.is_gn);
            normalization_break = false;
            continue;
        }

        const GpsTime previous_time = file.samples.back().time;
        if (absolute_difference(sample.time, previous_time)
            <= options.duplicate_epoch_tolerance_ns) {
            const bool replace = gga.is_gn || !retained_is_gn.back();
            if (replace) {
                file.samples.back() = sample;
                retained_is_gn.back() = gga.is_gn;
                add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::DuplicateEpoch,
                    gga.line_number, sample.time,
                    gga.is_gn ? "Duplicate GGA epoch retained the GN talker sample"
                              : "Duplicate GGA epoch retained the later sample",
                    DiagnosticAction::SampleReplaced);
            } else {
                file.samples.back().continuous_from_previous = false;
                add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::DuplicateEpoch,
                    gga.line_number, sample.time,
                    "Duplicate GGA epoch ignored the lower-priority GP talker sample",
                    DiagnosticAction::SampleRemoved);
            }
            normalization_break = false;
            continue;
        }
        if (sample.time < previous_time) {
            add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::TimeReversal,
                gga.line_number, sample.time, "Time-reversed GGA sample was removed",
                DiagnosticAction::SampleRemoved);
            normalization_break = true;
            continue;
        }
        sample.continuous_from_previous = !normalization_break && !gga.break_before;
        file.samples.push_back(sample);
        retained_is_gn.push_back(gga.is_gn);
        normalization_break = false;
    }

    if (file.samples.empty()) {
        add_diagnostic(file, DiagnosticSeverity::Fatal, DiagnosticCode::ParseError, std::nullopt,
            std::nullopt, "NMEA file contains no usable position samples",
            DiagnosticAction::FileRejected);
        return file;
    }

    const std::optional<double> estimated_hz =
        estimate_sample_rate_hz(file.samples, options.rate_min_interval_ns);
    if (estimated_hz.has_value()) {
        static_cast<void>(file.set_estimated_hz(*estimated_hz));
        const double jump_tolerance_ns = 10.0e9 / *estimated_hz;
        for (std::size_t index = 1; index < ggas.size(); ++index) {
            const std::optional<GpsTime> previous =
                utc_civil_to_gps_time(make_utc(assigned_days[index - 1], ggas[index - 1].time));
            const std::optional<GpsTime> current =
                utc_civil_to_gps_time(make_utc(assigned_days[index], ggas[index].time));
            if (previous.has_value() && current.has_value()
                && static_cast<double>(absolute_difference(*current, *previous))
                    > jump_tolerance_ns) {
                add_diagnostic(file, DiagnosticSeverity::Warning, DiagnosticCode::TimeJump,
                    ggas[index].line_number, current, "GGA time jump exceeds ten estimated epochs",
                    DiagnosticAction::Ignored);
            }
        }
    } else {
        add_diagnostic(file, DiagnosticSeverity::Info, DiagnosticCode::RateEstimationFailure,
            std::nullopt, std::nullopt,
            "NMEA sample rate could not be estimated; time-jump validation was skipped",
            DiagnosticAction::Ignored);
    }

    for (std::size_t index = 1; index < references.size(); ++index) {
        const DateReference& reference = references[index];
        const std::optional<GpsTime> reference_time = utc_civil_to_gps_time(reference.utc);
        if (!reference_time.has_value()) {
            continue;
        }
        std::int64_t nearest = std::numeric_limits<std::int64_t>::max();
        for (const NormalizedSample& sample : file.samples) {
            nearest = std::min(nearest, absolute_difference(sample.time, *reference_time));
        }
        if (nearest > options.datetime_validation_tolerance_ns) {
            add_diagnostic(file, DiagnosticSeverity::Warning,
                DiagnosticCode::DateValidationMismatch, reference.line_number, reference_time,
                "RMC/ZDA date/time does not match the nearest GGA epoch",
                DiagnosticAction::Ignored);
        }
    }
    return file;
}

} // namespace rtktrace
