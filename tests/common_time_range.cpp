#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "rtktrace/analysis/common_time_range.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] rtktrace::NormalizedSample sample_at(std::int64_t time_ns)
{
    return rtktrace::NormalizedSample{
        .time = rtktrace::GpsTime{time_ns},
        .llh = {},
        .ecef = {},
        .enu = {},
        .quality = rtktrace::SolutionQuality::InvalidOrUnknown,
        .source_line_number = 1,
        .continuous_from_previous = false,
    };
}

[[nodiscard]] rtktrace::LoadedFile file_with_range(
    const char* name, std::int64_t start_ns, std::int64_t end_ns)
{
    rtktrace::LoadedFile file{std::filesystem::path{name}, rtktrace::InputFormat::Pos};
    file.samples.push_back(sample_at(start_ns));
    file.samples.push_back(sample_at(end_ns));
    return file;
}

void test_union_and_intersection()
{
    using namespace rtktrace;

    LoadedFiles files;
    files.emplace_back(std::filesystem::path{"empty.pos"}, InputFormat::Pos);
    files.push_back(file_with_range("first.pos", 10, 20));
    files.push_back(file_with_range("second.pos", 15, 25));

    check(union_time_range(files) == TimeRange{GpsTime{10}, GpsTime{25}},
        "union uses earliest start and latest end");
    check(intersection_time_range(files) == TimeRange{GpsTime{15}, GpsTime{20}},
        "intersection uses latest start and earliest end");
    check(!file_time_range(files.front()).has_value(), "empty file has no time range");

    const LoadedFiles empty_files;
    check(!union_time_range(empty_files).has_value(), "empty file list has no union");
    check(!intersection_time_range(empty_files).has_value(), "empty file list has no intersection");
}

void test_effective_range()
{
    using namespace rtktrace;

    const TimeRange union_range{GpsTime{10}, GpsTime{30}};
    CommonTimeRange configured;
    check(effective_time_range(configured, union_range) == union_range,
        "disabled boundaries use union");

    configured.entered_start = GpsTime{15};
    configured.entered_end = GpsTime{25};
    check(effective_time_range(configured, union_range) == union_range,
        "disabled boundaries retain but do not apply entered values");

    configured.start_enabled = true;
    check(effective_time_range(configured, union_range) == TimeRange{GpsTime{15}, GpsTime{30}},
        "enabled start uses entered value");
    configured.end_enabled = true;
    check(effective_time_range(configured, union_range) == TimeRange{GpsTime{15}, GpsTime{25}},
        "enabled end uses entered value");

    configured.entered_start = GpsTime{35};
    check(!effective_time_range(configured, union_range).has_value(),
        "effective start after end is invalid");
    configured.entered_start = std::nullopt;
    check(!effective_time_range(configured, union_range).has_value(),
        "enabled boundary without a value is invalid");
}

void test_intersection_operation()
{
    using namespace rtktrace;

    LoadedFiles overlapping;
    overlapping.push_back(file_with_range("first.pos", 10, 20));
    overlapping.push_back(file_with_range("second.pos", 20, 30));
    CommonTimeRange configured;
    check(apply_intersection(configured, overlapping), "inclusive boundary intersection exists");
    check(
        configured.start_enabled && configured.end_enabled, "intersection enables both boundaries");
    check(configured.entered_start == GpsTime{20} && configured.entered_end == GpsTime{20},
        "intersection operation stores inclusive boundary");
    check(contains(TimeRange{GpsTime{20}, GpsTime{20}}, GpsTime{20}),
        "closed range contains both boundaries");

    LoadedFiles disjoint;
    disjoint.push_back(file_with_range("first.pos", 0, 10));
    disjoint.push_back(file_with_range("second.pos", 20, 30));
    const CommonTimeRange previous = configured;
    check(!apply_intersection(configured, disjoint), "disjoint files have no intersection");
    check(configured.entered_start == previous.entered_start
            && configured.entered_end == previous.entered_end
            && configured.start_enabled == previous.start_enabled
            && configured.end_enabled == previous.end_enabled,
        "failed intersection leaves configuration unchanged");
}

} // namespace

int main()
{
    test_union_and_intersection();
    test_effective_range();
    test_intersection_operation();

    if (failures != 0) {
        std::cerr << failures << " common-time-range test(s) failed\n";
        return 1;
    }
    return 0;
}
