#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#include "rtktrace/analysis/reference_matching.hpp"

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

[[nodiscard]] rtktrace::LoadedFile file_at_times(
    std::string_view name, std::initializer_list<std::int64_t> times)
{
    rtktrace::LoadedFile file{std::filesystem::path{name}, rtktrace::InputFormat::Pos};
    for (const std::int64_t time : times) {
        file.samples.push_back(sample_at(time));
    }
    return file;
}

void test_sample_range_index()
{
    using namespace rtktrace;
    const std::vector samples{sample_at(0), sample_at(10), sample_at(20), sample_at(30)};

    const std::optional<SampleRangeIndex> middle =
        sample_range_index(samples, TimeRange{GpsTime{10}, GpsTime{20}});
    check(middle.has_value() && middle->begin == 1 && middle->end == 3,
        "common time range includes both closed boundaries");
    const std::optional<SampleRangeIndex> empty =
        sample_range_index(samples, TimeRange{GpsTime{11}, GpsTime{19}});
    check(empty.has_value() && empty->begin == 2 && empty->end == 2 && empty->empty(),
        "range without samples has an empty insertion-point index");
    check(!sample_range_index(samples, TimeRange{GpsTime{20}, GpsTime{10}}).has_value(),
        "reversed common time range is invalid");

    const std::vector unsorted{sample_at(0), sample_at(20), sample_at(10)};
    check(!sample_range_index(unsorted, TimeRange{GpsTime{0}, GpsTime{20}}).has_value(),
        "unsorted normalized samples are rejected before binary search");
}

void test_common_time_range_cache()
{
    using namespace rtktrace;
    LoadedFiles files;
    files.push_back(file_at_times("one.pos", {0, 10, 20}));
    files.push_back(file_at_times("two.pos", {5, 15, 25}));
    files.push_back(file_at_times("empty.pos", {}));

    CommonTimeRangeIndex index;
    check(rebuild_common_time_range_index(files, TimeRange{GpsTime{10}, GpsTime{20}}, index),
        "common time range index cache builds for every slot");
    check(index.revision == 1 && index.range == TimeRange{GpsTime{10}, GpsTime{20}},
        "successful index build stores range and advances revision");
    const SampleRangeIndex* first = range_at_slot(index, 1);
    const SampleRangeIndex* second = range_at_slot(index, 2);
    const SampleRangeIndex* third = range_at_slot(index, 3);
    check(first != nullptr && first->begin == 1 && first->end == 3,
        "slot 1 index selects its samples");
    check(second != nullptr && second->begin == 1 && second->end == 2,
        "slot 2 index selects its samples independently");
    check(third != nullptr && third->empty(), "empty file receives an empty range index");
    check(range_at_slot(index, 0) == nullptr && range_at_slot(index, 4) == nullptr,
        "invalid slot has no range index");

    check(!rebuild_common_time_range_index(files, TimeRange{GpsTime{20}, GpsTime{10}}, index),
        "invalid range does not rebuild the index cache");
    check(index.revision == 1 && range_at_slot(index, 1)->begin == 1,
        "failed index rebuild preserves the previous cache and revision");
}

void test_reference_matching_without_tolerance()
{
    using namespace rtktrace;
    const std::vector references{sample_at(10), sample_at(20), sample_at(30)};
    const std::vector comparisons{
        sample_at(5),
        sample_at(10),
        sample_at(15),
        sample_at(20),
        sample_at(25),
        sample_at(30),
        sample_at(35),
        sample_at(40),
    };
    const std::optional<std::vector<ReferenceMatch>> matches =
        match_reference_epochs(references, SampleRangeIndex{0, references.size()}, comparisons,
            SampleRangeIndex{0, comparisons.size()}, ReferenceMatchConfiguration{false, 0});

    check(matches.has_value() && matches->size() == 7,
        "comparison before first reference is omitted without using a future epoch");
    if (!matches.has_value() || matches->size() != 7) {
        return;
    }
    check((*matches)[0].comparison_index == 1 && (*matches)[0].reference_index == 0
            && (*matches)[0].time_difference_ns == 0,
        "equal epoch matches exactly");
    check((*matches)[2].comparison_index == 3 && (*matches)[2].reference_index == 1,
        "new reference epoch becomes active at its own time");
    check((*matches)[6].comparison_index == 7 && (*matches)[6].reference_index == 2
            && (*matches)[6].time_difference_ns == 10,
        "disabled tolerance continues using the final reference epoch");
}

void test_reference_range_and_tolerance()
{
    using namespace rtktrace;
    const std::vector references{sample_at(10), sample_at(20), sample_at(20), sample_at(30)};
    const std::vector comparisons{
        sample_at(15),
        sample_at(20),
        sample_at(25),
        sample_at(30),
        sample_at(35),
        sample_at(40),
    };

    const std::optional<std::vector<ReferenceMatch>> ranged =
        match_reference_epochs(references, SampleRangeIndex{1, references.size()}, comparisons,
            SampleRangeIndex{0, comparisons.size()}, ReferenceMatchConfiguration{false, 0});
    check(ranged.has_value() && ranged->size() == 5 && (*ranged)[0].comparison_index == 1,
        "reference sample outside common range is not used for an earlier comparison");
    if (ranged.has_value() && !ranged->empty()) {
        check((*ranged)[0].reference_index == 2,
            "latest duplicate reference at the same epoch is selected");
    }

    const std::optional<std::vector<ReferenceMatch>> limited =
        match_reference_epochs(references, SampleRangeIndex{0, references.size()}, comparisons,
            SampleRangeIndex{0, comparisons.size()}, ReferenceMatchConfiguration{true, 5});
    check(limited.has_value() && limited->size() == 5,
        "enabled tolerance excludes only differences greater than the maximum");
    if (limited.has_value() && !limited->empty()) {
        check(limited->back().comparison_index == 4 && limited->back().time_difference_ns == 5,
            "maximum tolerance boundary remains inclusive");
    }
}

void test_invalid_matching_inputs()
{
    using namespace rtktrace;
    const std::vector ordered{sample_at(0), sample_at(10)};
    const std::vector unsorted{sample_at(10), sample_at(0)};
    check(!match_reference_epochs(ordered, SampleRangeIndex{0, 3}, ordered, SampleRangeIndex{0, 2},
              ReferenceMatchConfiguration{false, 0})
              .has_value(),
        "out-of-bounds sample range is rejected");
    check(!match_reference_epochs(unsorted, SampleRangeIndex{0, 2}, ordered, SampleRangeIndex{0, 2},
              ReferenceMatchConfiguration{false, 0})
              .has_value(),
        "time-reversed reference range is rejected");
    check(!match_reference_epochs(ordered, SampleRangeIndex{0, 2}, ordered, SampleRangeIndex{0, 2},
              ReferenceMatchConfiguration{true, -1})
              .has_value(),
        "negative reference tolerance is rejected");

    const std::vector extreme_reference{sample_at(std::numeric_limits<std::int64_t>::min())};
    const std::vector extreme_comparison{sample_at(std::numeric_limits<std::int64_t>::max())};
    check(!match_reference_epochs(extreme_reference, SampleRangeIndex{0, 1}, extreme_comparison,
              SampleRangeIndex{0, 1}, ReferenceMatchConfiguration{false, 0})
              .has_value(),
        "unrepresentable signed time difference is rejected without overflow");
}

} // namespace

int main()
{
    test_sample_range_index();
    test_common_time_range_cache();
    test_reference_matching_without_tolerance();
    test_reference_range_and_tolerance();
    test_invalid_matching_inputs();

    if (failures != 0) {
        std::cerr << failures << " reference matching test(s) failed\n";
        return 1;
    }
    return 0;
}
