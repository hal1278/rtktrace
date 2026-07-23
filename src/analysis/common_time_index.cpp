#include "rtktrace/analysis/common_time_index.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace rtktrace {

std::optional<SampleRangeIndex> sample_range_index(
    std::span<const NormalizedSample> samples, TimeRange range) noexcept
{
    if (range.start > range.end) {
        return std::nullopt;
    }
    if (!std::is_sorted(samples.begin(), samples.end(),
            [](const NormalizedSample& lhs, const NormalizedSample& rhs) {
                return lhs.time < rhs.time;
            })) {
        return std::nullopt;
    }
    const auto first = std::lower_bound(samples.begin(), samples.end(), range.start,
        [](const NormalizedSample& sample, GpsTime time) { return sample.time < time; });
    const auto last = std::upper_bound(first, samples.end(), range.end,
        [](GpsTime time, const NormalizedSample& sample) { return time < sample.time; });
    return SampleRangeIndex{
        static_cast<std::size_t>(first - samples.begin()),
        static_cast<std::size_t>(last - samples.begin()),
    };
}

bool rebuild_common_time_range_index(
    const LoadedFiles& files, TimeRange range, CommonTimeRangeIndex& index)
{
    if (range.start > range.end) {
        return false;
    }
    std::vector<SampleRangeIndex> ranges;
    ranges.reserve(files.size());
    for (const LoadedFile& file : files) {
        const std::optional<SampleRangeIndex> file_range = sample_range_index(file.samples, range);
        if (!file_range.has_value()) {
            return false;
        }
        ranges.push_back(*file_range);
    }
    index.range = range;
    index.file_ranges = std::move(ranges);
    if (index.revision != std::numeric_limits<std::uint64_t>::max()) {
        ++index.revision;
    }
    return true;
}

const SampleRangeIndex* range_at_slot(
    const CommonTimeRangeIndex& index, std::size_t slot_number) noexcept
{
    if (slot_number == 0 || slot_number > index.file_ranges.size()) {
        return nullptr;
    }
    return &index.file_ranges[slot_number - 1];
}

} // namespace rtktrace
