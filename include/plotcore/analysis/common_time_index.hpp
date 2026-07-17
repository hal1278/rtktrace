#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "plotcore/analysis/common_time_range.hpp"

namespace plotcore {

struct SampleRangeIndex {
    std::size_t begin;
    std::size_t end;

    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return end - begin;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return begin == end;
    }
};

struct CommonTimeRangeIndex {
    std::optional<TimeRange> range;
    std::vector<SampleRangeIndex> file_ranges;
    std::uint64_t revision{0};
};

[[nodiscard]] std::optional<SampleRangeIndex> sample_range_index(
    std::span<const NormalizedSample> samples, TimeRange range) noexcept;

[[nodiscard]] bool rebuild_common_time_range_index(
    const LoadedFiles& files, TimeRange range, CommonTimeRangeIndex& index);

[[nodiscard]] const SampleRangeIndex* range_at_slot(
    const CommonTimeRangeIndex& index, std::size_t slot_number) noexcept;

} // namespace plotcore
