#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "plotcore/model/sample.hpp"

namespace plotcore {

inline constexpr std::int64_t default_rate_min_interval_ns = 5'000'000;

[[nodiscard]] std::optional<double> estimate_sample_rate_hz(
    std::span<const NormalizedSample> samples,
    std::int64_t rate_min_interval_ns = default_rate_min_interval_ns) noexcept;

} // namespace plotcore
