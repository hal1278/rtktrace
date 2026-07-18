#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "plotcore/model/diagnostic.hpp"
#include "plotcore/model/loaded_file.hpp"
#include "plotcore/model/notification.hpp"
#include "plotcore/model/sample.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void test_gps_time()
{
    using plotcore::GpsTime;

    static_assert(std::is_same_v<decltype(std::declval<GpsTime>().nanoseconds_since_gps_epoch),
        std::int64_t>);
    static_assert(!std::is_convertible_v<std::int64_t, GpsTime>);

    constexpr GpsTime earlier{1'000};
    constexpr GpsTime same{1'000};
    constexpr GpsTime later{1'250};
    static_assert(earlier == same);
    static_assert(earlier < later);
    static_assert(later > earlier);
    static_assert(later - earlier == 250);
    static_assert(earlier - later == -250);

    check(earlier == same, "GpsTime equality");
    check(earlier < later, "GpsTime ordering");
    check(later - earlier == 250, "GpsTime positive difference");
    check(earlier - later == -250, "GpsTime negative difference");
}

void test_sample_types()
{
    using namespace plotcore;

    constexpr Wgs84Llh llh{0.62, 2.44, 12.5};
    constexpr Ecef ecef{-3'950'000.0, 3'350'000.0, 3'700'000.0};
    constexpr Enu enu{1.25, -2.5, 0.75};

    check(llh.latitude_rad == 0.62, "LLH latitude radians");
    check(llh.longitude_rad == 2.44, "LLH longitude radians");
    check(llh.ellipsoidal_height_m == 12.5, "LLH ellipsoidal height metres");
    check(ecef.x_m == -3'950'000.0 && ecef.y_m == 3'350'000.0 && ecef.z_m == 3'700'000.0,
        "ECEF metres");
    check(enu.east_m == 1.25 && enu.north_m == -2.5 && enu.up_m == 0.75, "ENU metres");

    constexpr SolutionQuality qualities[] = {
        SolutionQuality::InvalidOrUnknown,
        SolutionQuality::Fixed,
        SolutionQuality::Float,
        SolutionQuality::Sbas,
        SolutionQuality::Dgps,
        SolutionQuality::Single,
        SolutionQuality::Ppp,
    };
    static_assert(!std::is_convertible_v<int, SolutionQuality>);
    for (std::size_t index = 0; index < std::size(qualities); ++index) {
        check(static_cast<std::uint8_t>(qualities[index]) == index, "solution quality value");
    }

    const NormalizedSample sample{
        .time = GpsTime{123'456'789},
        .llh = llh,
        .ecef = ecef,
        .enu = enu,
        .quality = SolutionQuality::Fixed,
        .source_line_number = 42,
        .continuous_from_previous = true,
    };
    check(sample.time == GpsTime{123'456'789}, "normalized sample time");
    check(sample.quality == SolutionQuality::Fixed, "normalized sample quality");
    check(sample.source_line_number == 42, "normalized sample physical source line");
    check(sample.continuous_from_previous, "normalized sample continuity");
}

void test_diagnostic()
{
    using namespace plotcore;

    const Diagnostic located{
        .severity = DiagnosticSeverity::Warning,
        .code = DiagnosticCode::ChecksumError,
        .file_name = "receiver.nmea",
        .source_line_number = 17,
        .time = GpsTime{7'000},
        .message = "checksum mismatch",
        .action = DiagnosticAction::SampleRemoved,
    };
    check(located.source_line_number == 17, "diagnostic source line present");
    check(located.time == GpsTime{7'000}, "diagnostic time present");
    check(located.severity == DiagnosticSeverity::Warning, "diagnostic severity independent");
    check(located.code == DiagnosticCode::ChecksumError, "diagnostic code independent");
    check(located.action == DiagnosticAction::SampleRemoved, "diagnostic action independent");

    const Diagnostic unlocated{
        .severity = DiagnosticSeverity::Fatal,
        .code = DiagnosticCode::NoCommonIntersection,
        .file_name = "solution.pos",
        .source_line_number = std::nullopt,
        .time = std::nullopt,
        .message = "no common intersection",
        .action = DiagnosticAction::FileRejected,
    };
    check(!unlocated.source_line_number.has_value(), "diagnostic source line absent");
    check(!unlocated.time.has_value(), "diagnostic time absent");
}

void test_loaded_file()
{
    using namespace plotcore;

    LoadedFile file{std::filesystem::path{"survey.pos"}, InputFormat::Pos};
    check(!file.estimated_hz().has_value(), "estimated Hz initially unknown");
    check(!file.override_hz().has_value(), "override Hz initially unset");
    check(!file.effective_hz().has_value(), "effective Hz initially unknown");

    check(!file.set_estimated_hz(-1.0), "reject negative estimated Hz");
    check(file.set_estimated_hz(5.0), "accept positive finite estimated Hz");
    check(file.effective_hz() == 5.0, "estimated Hz is effective without override");
    check(!file.set_override_hz(0.0), "reject zero override Hz");
    check(!file.set_override_hz(std::numeric_limits<double>::infinity()),
        "reject infinite override Hz");
    check(
        !file.set_override_hz(std::numeric_limits<double>::quiet_NaN()), "reject NaN override Hz");
    check(file.set_override_hz(10.0), "accept positive finite override Hz");
    check(file.override_hz() == 10.0, "retain override Hz");
    check(file.effective_hz() == 10.0, "override Hz takes precedence");
    file.clear_override_hz();
    check(!file.override_hz().has_value(), "clear override Hz");
    check(file.effective_hz() == 5.0, "effective Hz returns to estimate");

    check(file.visible, "loaded file initially visible");
    file.visible = false;
    check(!file.visible, "loaded file visibility changes");
    check(file.source_path.filename() == "survey.pos", "source path retains file name");
    check(file.input_format == InputFormat::Pos, "loaded file input format");
    check(file.samples.empty() && file.diagnostics.empty(),
        "loaded file owns sample and diagnostic lists");
}

void test_notification_history()
{
    using namespace plotcore;

    NotificationHistory history;
    history.add(NotificationLevel::Info, "Loaded survey.pos");
    check(history.entries().size() == 1 && !history.has_caution(),
        "informational notification does not raise caution");
    history.add(Diagnostic{
        .severity = DiagnosticSeverity::Warning,
        .code = DiagnosticCode::ChecksumError,
        .file_name = "receiver.nmea",
        .source_line_number = 17,
        .time = std::nullopt,
        .message = "checksum mismatch",
        .action = DiagnosticAction::SampleRemoved,
    });
    check(history.entries().size() == 2 && history.has_caution()
            && history.entries().back().message == "receiver.nmea: line 17: checksum mismatch",
        "warning diagnostic records location and raises caution");
    history.clear();
    check(history.entries().empty() && !history.has_caution(),
        "clearing history also clears caution state");
}

void test_slots()
{
    using namespace plotcore;

    LoadedFiles files;
    check(reference_file(files) == nullptr, "empty slots have no reference file");

    files.emplace_back(std::filesystem::path{"one.pos"}, InputFormat::Pos);
    files.emplace_back(std::filesystem::path{"two.nmea"}, InputFormat::Nmea);
    files.emplace_back(std::filesystem::path{"three.pos"}, InputFormat::Pos);
    check(
        file_at_slot(files, 1)->source_path.filename() == "one.pos", "first added file is slot 1");
    check(file_at_slot(files, 2)->source_path.filename() == "two.nmea",
        "second added file is slot 2");
    check(file_at_slot(files, 3)->source_path.filename() == "three.pos",
        "third added file is slot 3");
    check(
        file_at_slot(files, 2)->input_format == InputFormat::Nmea, "NMEA input format is distinct");
    check(file_at_slot(files, 0) == nullptr, "slot zero is invalid");

    check(move_slot(files, 3, 1), "move slot 3 to slot 1");
    check(
        file_at_slot(files, 1)->source_path.filename() == "three.pos", "moved file becomes slot 1");
    check(file_at_slot(files, 2)->source_path.filename() == "one.pos",
        "intermediate slot remains contiguous");
    check(file_at_slot(files, 3)->source_path.filename() == "two.nmea",
        "last slot remains contiguous");
    check(reference_file(files) == file_at_slot(files, 1), "slot 1 is the reference file");

    check(move_slot(files, 1, 3), "move slot 1 to slot 3");
    check(
        file_at_slot(files, 1)->source_path.filename() == "one.pos", "forward move shifts slot 2");
    check(file_at_slot(files, 3)->source_path.filename() == "three.pos",
        "forward move reaches last slot");

    check(erase_slot(files, 2), "erase middle slot");
    check(files.size() == 2, "slot erase reduces file count");
    check(
        file_at_slot(files, 1)->source_path.filename() == "one.pos", "slot 1 remains after erase");
    check(
        file_at_slot(files, 2)->source_path.filename() == "three.pos", "later slot shifts forward");
    check(file_at_slot(files, 3) == nullptr, "removed final slot number is invalid");
}

} // namespace

int main()
{
    test_gps_time();
    test_sample_types();
    test_diagnostic();
    test_loaded_file();
    test_notification_history();
    test_slots();

    if (failures != 0) {
        std::cerr << failures << " model test(s) failed\n";
        return 1;
    }
    return 0;
}
