#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include "plotcore/analysis/common_time_range.hpp"
#include "plotcore/analysis/coordinates.hpp"

namespace plotcore {

enum class EnuReferenceMethod : std::uint8_t {
    Slot1Start,
    Slot1End,
    Slot1EcefAverage,
    UserSpecified,
};

struct UserSpecifiedLlh {
    double latitude_deg;
    double longitude_deg;
    double ellipsoidal_height_m;
};

using UserSpecifiedEnuPosition = std::variant<UserSpecifiedLlh, Ecef>;

struct EnuReferenceConfiguration {
    EnuReferenceMethod method{EnuReferenceMethod::Slot1Start};
    std::optional<UserSpecifiedEnuPosition> user_position;
};

struct EnuCache {
    std::optional<EnuReference> reference;
    std::uint64_t revision{0};
};

enum class EnuCacheUpdateStatus : std::uint8_t {
    Updated,
    RetainedPreviousReference,
    Unavailable,
};

[[nodiscard]] std::optional<EnuReference> determine_enu_reference(const LoadedFiles& files,
    TimeRange range, const EnuReferenceConfiguration& configuration) noexcept;

[[nodiscard]] EnuCacheUpdateStatus rebuild_enu_cache(LoadedFiles& files, TimeRange range,
    const EnuReferenceConfiguration& configuration, EnuCache& cache);

} // namespace plotcore
