#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "rtktrace/analysis/gps_time.hpp"
#include "rtktrace/io/nmea_parser.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] std::string sentence(std::string_view body)
{
    unsigned char checksum = 0;
    for (const unsigned char byte : body) {
        checksum ^= byte;
    }
    std::ostringstream output;
    output << '$' << body << '*' << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
           << static_cast<unsigned>(checksum) << '\n';
    return output.str();
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

void test_date_coordinates_quality_and_talker_priority()
{
    using namespace rtktrace;
    std::istringstream input{sentence("GPRMC,120000.000,A,,,,,,,010724,,")
        + sentence("GPGGA,115959.000,3500.000,N,13900.000,E,1,12,0.8,10.0,M,30.0,M,,")
        + sentence("GPGGA,120000.000,3500.000,N,13900.000,E,4,12,0.8,11.0,M,31.0,M,,")
        + sentence("GNGGA,120000.001,3600.000,N,14000.000,E,5,12,0.8,12.0,M,32.0,M,,")};
    const LoadedFile file = parse_nmea(input, "mixed.nmea");

    check(file.samples.size() == 2, "same-epoch GP/GN GGA records are coalesced");
    check(file.input_format == InputFormat::Nmea, "NMEA input format is retained");
    check(diagnostic_count(file, DiagnosticCode::MultipleTalkers) == 1,
        "mixed GP and GN talkers produce one file diagnostic");
    check(diagnostic_count(file, DiagnosticCode::DuplicateEpoch) == 1,
        "talker-priority replacement is diagnosed");
    if (file.samples.size() == 2) {
        const std::optional<GpsTime> expected =
            utc_civil_to_gps_time(UtcCivilTime{2024, 7, 1, 12, 0, 0, 1'000'000});
        check(expected.has_value() && file.samples[1].time == *expected,
            "GGA UTC is converted to GPST using the date reference");
        check(std::abs(file.samples[1].llh.latitude_rad - 36.0 * std::acos(-1.0) / 180.0) < 1.0e-15,
            "NMEA degrees/minutes latitude is normalized to radians");
        check(std::abs(file.samples[1].llh.ellipsoidal_height_m - 44.0) < 1.0e-12,
            "ellipsoidal height includes geoid separation");
        check(file.samples[1].quality == SolutionQuality::Float,
            "GGA quality is mapped to normalized quality");
        check(file.samples[1].source_line_number == 4,
            "GN talker priority retains the adopted physical line");
        check(!file.samples[1].continuous_from_previous,
            "talker-priority replacement breaks continuity");
    }
    check(file.estimated_hz().has_value(), "NMEA parser stores an estimated sample rate");
}

void test_user_decisions()
{
    using namespace rtktrace;
    const std::string without_context =
        sentence("GPGGA,120000.000,3500.000,N,13900.000,E,1,12,0.8,10.0,M,,M,,");
    std::istringstream undecided_input{without_context};
    const LoadedFile undecided = parse_nmea(undecided_input, "undecided.nmea");
    check(undecided.samples.empty(), "unresolved NMEA decisions do not expose partial samples");
    check(diagnostic_count(undecided, DiagnosticCode::MissingGeoidSeparation) == 1,
        "missing geoid separation requests a decision");
    check(diagnostic_count(undecided, DiagnosticCode::MissingDate) == 1,
        "missing date requests an independent decision");

    NmeaParseOptions options;
    options.fallback_date = NmeaDate{2024, 7, 1};
    options.missing_geoid_policy = MissingGeoidPolicy::UseAltitudeAsEllipsoidalHeight;
    std::istringstream accepted_input{without_context};
    const LoadedFile accepted = parse_nmea(accepted_input, "accepted.nmea", options);
    check(accepted.samples.size() == 1, "explicit NMEA decisions complete parsing");
    if (!accepted.samples.empty()) {
        check(std::abs(accepted.samples[0].llh.ellipsoidal_height_m - 10.0) < 1.0e-12,
            "approved altitude is used as ellipsoidal height");
    }
    check(!accepted.diagnostics.empty()
            && diagnostic_count(accepted, DiagnosticCode::RateEstimationFailure) == 1,
        "single-sample rate-estimation failure is explicit");
}

void test_rollover_partial_loading_and_validation()
{
    using namespace rtktrace;
    std::string text;
    text += sentence("GPGGA,235959.000,3500.000,N,13900.000,E,1,12,0.8,10.0,M,30.0,M,,");
    text += sentence("GPRMC,000000.000,A,,,,,,,020724,,");
    text += "$GPGGA,000000.500,3500.000,N,13900.000,E,1,12,0.8,10.0,M,30.0,M,,*00\n";
    text += sentence("GPGGA,000001.000,3500.000,N,13900.000,E,2,12,0.8,10.0,M,30.0,M,,");
    text += sentence("GPGGA,000002.000,3500.000,N,13900.000,E,3,12,0.8,10.0,M,30.0,M,,");
    text += sentence("GPGGA,000001.500,3500.000,N,13900.000,E,4,12,0.8,10.0,M,30.0,M,,");
    text += sentence("GPGGA,000003.000,3500.000,N,13900.000,E,9,12,0.8,10.0,M,30.0,M,,");
    text += sentence("GLGGA,000004.000,3500.000,N,13900.000,E,1,12,0.8,10.0,M,30.0,M,,");
    text += sentence("GPZDA,120000.000,03,07,2024,00,00");
    std::istringstream input{text};
    const LoadedFile file = parse_nmea(input, "rollover.nmea");

    check(file.samples.size() == 4,
        "checksum failure and time reversal are removed while later GGA records load");
    if (file.samples.size() == 4) {
        const std::optional<GpsTime> before_midnight =
            utc_civil_to_gps_time(UtcCivilTime{2024, 7, 1, 23, 59, 59, 0});
        const std::optional<GpsTime> after_midnight =
            utc_civil_to_gps_time(UtcCivilTime{2024, 7, 2, 0, 0, 1, 0});
        check(before_midnight.has_value() && file.samples[0].time == *before_midnight,
            "GGA before the first date sentence is assigned to the previous day");
        check(after_midnight.has_value() && file.samples[1].time == *after_midnight,
            "GGA after the first date sentence retains its current day");
        check(!file.samples[1].continuous_from_previous,
            "checksum removal breaks continuity at the next retained sample");
        check(!file.samples[3].continuous_from_previous,
            "time reversal breaks continuity at the next retained sample");
        check(file.samples[3].quality == SolutionQuality::InvalidOrUnknown,
            "unknown GGA quality is retained as quality zero");
    }
    check(diagnostic_count(file, DiagnosticCode::ChecksumError) == 1,
        "checksum mismatch is diagnosed");
    check(diagnostic_count(file, DiagnosticCode::TimeReversal) == 1, "time reversal is diagnosed");
    check(diagnostic_count(file, DiagnosticCode::UnknownNmeaQuality) == 1,
        "unknown NMEA quality is diagnosed");
    check(diagnostic_count(file, DiagnosticCode::UnsupportedTalker) == 1,
        "unsupported talker is diagnosed");
    check(diagnostic_count(file, DiagnosticCode::DateValidationMismatch) == 1,
        "later date sentence mismatch is diagnosed");
}

void test_rejected_options_and_empty_input()
{
    using namespace rtktrace;
    std::istringstream valid{sentence("GPRMC,120000.000,A,,,,,,,010724,,")
        + sentence("GPGGA,120000.000,3500.000,N,13900.000,E,1,12,0.8,10.0,M,30.0,M,,")};
    NmeaParseOptions invalid_options;
    invalid_options.rollover_tolerance_ns = 86'400'000'000'000;
    const LoadedFile invalid = parse_nmea(valid, "invalid-options.nmea", invalid_options);
    check(invalid.samples.empty() && !invalid.diagnostics.empty()
            && invalid.diagnostics.front().severity == DiagnosticSeverity::Fatal,
        "invalid NMEA tolerances reject parsing");

    std::istringstream empty{""};
    const LoadedFile rejected = parse_nmea(empty, "empty.nmea");
    check(rejected.samples.empty() && !rejected.diagnostics.empty()
            && rejected.diagnostics.back().action == DiagnosticAction::FileRejected,
        "NMEA file without valid GGA records is rejected");
}

} // namespace

int main()
{
    test_date_coordinates_quality_and_talker_priority();
    test_user_decisions();
    test_rollover_partial_loading_and_validation();
    test_rejected_options_and_empty_input();

    if (failures != 0) {
        std::cerr << failures << " NMEA parser test(s) failed\n";
        return 1;
    }
    return 0;
}
