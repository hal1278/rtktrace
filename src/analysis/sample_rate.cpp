#include "plotcore/analysis/sample_rate.hpp"

#include <algorithm>
#include <limits>

namespace plotcore {

std::optional<double> estimate_sample_rate_hz(
    std::span<const NormalizedSample> samples, std::int64_t rate_min_interval_ns) noexcept
{
    if (samples.size() < 2) {
        return std::nullopt;
    }

    std::int64_t minimum_interval_ns = std::numeric_limits<std::int64_t>::max();
    for (std::size_t index = 1; index < samples.size(); ++index) {
        if (samples[index].time <= samples[index - 1].time) {
            continue;
        }
        const std::int64_t interval_ns = samples[index].time - samples[index - 1].time;
        if (interval_ns > 0 && interval_ns > rate_min_interval_ns) {
            minimum_interval_ns = std::min(minimum_interval_ns, interval_ns);
        }
    }

    if (minimum_interval_ns == std::numeric_limits<std::int64_t>::max()) {
        return std::nullopt;
    }
    return 1'000'000'000.0 / static_cast<double>(minimum_interval_ns);
}

} // namespace plotcore
