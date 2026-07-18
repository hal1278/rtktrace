#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "plotcore/analysis/common_time_index.hpp"

namespace plotcore {

struct ReferenceMatchConfiguration {
    bool tolerance_check_enabled;
    std::int64_t maximum_time_difference_ns;

    bool operator==(const ReferenceMatchConfiguration&) const = default;
};

struct ReferenceMatch {
    std::size_t comparison_index;
    std::size_t reference_index;
    std::int64_t time_difference_ns;
};

[[nodiscard]] std::optional<std::vector<ReferenceMatch>> match_reference_epochs(
    std::span<const NormalizedSample> reference_samples, SampleRangeIndex reference_range,
    std::span<const NormalizedSample> comparison_samples, SampleRangeIndex comparison_range,
    ReferenceMatchConfiguration configuration);

} // namespace plotcore
