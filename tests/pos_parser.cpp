#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

#include "plotcore/io/pos_parser.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] std::size_t diagnostic_count(
    const plotcore::LoadedFile& file, plotcore::DiagnosticCode code)
{
    std::size_t count = 0;
    for (const plotcore::Diagnostic& diagnostic : file.diagnostics) {
        if (diagnostic.code == code) {
            ++count;
        }
    }
    return count;
}

void test_time_formats_and_quality()
{
    using namespace plotcore;

    std::istringstream input{"  % ignored header\n"
                             "\n"
                             "0 0.1234567895 35.0 139.0 10.0 1 ignored\n"
                             "1980/01/06 00:00:01.9999999995 36.0 140.0 20.0 9\n"};
    const LoadedFile file = parse_pos(input, "formats.pos");

    check(file.samples.size() == 2, "week/TOW and calendar records are accepted");
    if (file.samples.size() == 2) {
        check(file.samples[0].time == GpsTime{123'456'790},
            "TOW fraction rounds to integer nanoseconds");
        check(file.samples[1].time == GpsTime{2'000'000'000},
            "calendar fractional carry advances the whole second");
        check(std::abs(file.samples[0].llh.latitude_rad - 35.0 * std::acos(-1.0) / 180.0) < 1.0e-15,
            "POS latitude degree is converted to radians");
        check(std::isfinite(file.samples[0].ecef.x_m), "POS LLH is converted to ECEF");
        check(file.samples[0].quality == SolutionQuality::Fixed, "known POS quality is preserved");
        check(file.samples[1].quality == SolutionQuality::InvalidOrUnknown,
            "unknown POS quality is loaded as zero");
        check(!file.samples[0].continuous_from_previous && file.samples[1].continuous_from_previous,
            "ordinary consecutive records retain continuity");
    }
    check(diagnostic_count(file, DiagnosticCode::UnknownPosQuality) == 1,
        "unknown POS quality produces a diagnostic");
    if (!file.diagnostics.empty()) {
        check(file.diagnostics.front().file_name == "formats.pos",
            "diagnostic stores the file name rather than the source path");
        check(file.diagnostics.front().action == DiagnosticAction::LoadedAsQualityZero,
            "unknown quality diagnostic records its action");
    }
}

void test_partial_loading_and_normalization()
{
    using namespace plotcore;

    std::istringstream input{"0 0.000 35 139 10 1\n"
                             "0 0.500 invalid 139 10 1\n"
                             "0 1.000 35 139 10 2\n"
                             "0 1.001 36 140 20 3\n"
                             "0 2.000 35 139 10 4\n"
                             "0 1.500 35 139 10 5\n"
                             "0 3.000 35 139 10 6\n"
                             "0 4.000 35 139\n"};
    const LoadedFile file = parse_pos(input, "partial.pos");

    check(file.samples.size() == 4, "invalid, duplicate, and reversed records are normalized");
    if (file.samples.size() == 4) {
        check(file.samples[0].source_line_number == 1, "first physical line is retained");
        check(file.samples[1].source_line_number == 4,
            "later duplicate replaces the earlier physical line");
        check(file.samples[1].quality == SolutionQuality::Sbas,
            "replacement sample retains its own values");
        check(!file.samples[1].continuous_from_previous,
            "record removal and duplicate replacement break continuity");
        check(file.samples[2].continuous_from_previous,
            "duplicate replacement does not break the following sample");
        check(!file.samples[3].continuous_from_previous,
            "time reversal breaks continuity at the next retained sample");
    }
    check(diagnostic_count(file, DiagnosticCode::ParseError) == 1,
        "invalid coordinate produces a parse diagnostic");
    check(diagnostic_count(file, DiagnosticCode::MissingField) == 1,
        "missing field produces a distinct diagnostic");
    check(diagnostic_count(file, DiagnosticCode::DuplicateEpoch) == 1,
        "duplicate epoch produces a replacement diagnostic");
    check(diagnostic_count(file, DiagnosticCode::TimeReversal) == 1,
        "time reversal produces a removal diagnostic");
}

void test_rejected_input()
{
    using namespace plotcore;

    std::istringstream empty{"% header only\n"};
    const LoadedFile rejected = parse_pos(empty, "empty.pos");
    check(rejected.samples.empty(), "file without valid samples is empty");
    check(!rejected.diagnostics.empty()
            && rejected.diagnostics.back().severity == DiagnosticSeverity::Fatal
            && rejected.diagnostics.back().action == DiagnosticAction::FileRejected,
        "file without valid samples is rejected explicitly");

    std::istringstream valid{"0 0 35 139 10 1\n"};
    const LoadedFile invalid_options = parse_pos(valid, "options.pos", PosParseOptions{-1});
    check(invalid_options.samples.empty() && !invalid_options.diagnostics.empty()
            && invalid_options.diagnostics.front().severity == DiagnosticSeverity::Fatal,
        "negative duplicate tolerance is rejected");
}

void test_pos_fixture(const char* path)
{
    using namespace plotcore;

    std::ifstream input{path};
    check(input.is_open(), "fictional POS fixture opens");
    if (!input.is_open()) {
        return;
    }
    const LoadedFile file = parse_pos(input, path);
    check(file.samples.size() == 5, "fictional POS fixture loads every record");
    check(file.diagnostics.empty(), "fictional POS fixture needs no diagnostic");
    if (!file.samples.empty()) {
        constexpr std::int64_t week_ns = 604'800'000'000'000;
        check(file.samples.front().time == GpsTime{2'324 * week_ns + 100'000'000'000'000},
            "fictional POS first GPS week/TOW is normalized exactly");
        check(file.samples.front().quality == SolutionQuality::Fixed,
            "fictional POS quality is normalized");
        check(file.samples.front().source_line_number == 2,
            "fictional POS physical source line includes the header");
    }
}

} // namespace

int main(int argc, char** argv)
{
    test_time_formats_and_quality();
    test_partial_loading_and_normalization();
    test_rejected_input();
    if (argc == 2) {
        test_pos_fixture(argv[1]);
    } else {
        check(false, "fictional POS fixture path is supplied");
    }

    if (failures != 0) {
        std::cerr << failures << " POS parser test(s) failed\n";
        return 1;
    }
    return 0;
}
