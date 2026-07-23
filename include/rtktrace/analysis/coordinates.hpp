#pragma once

#include <optional>

#include "rtktrace/model/sample.hpp"

namespace rtktrace {

struct EnuReference {
    Ecef origin_ecef;
    double latitude_rad;
    double longitude_rad;
};

[[nodiscard]] Ecef wgs84_llh_to_ecef(Wgs84Llh llh) noexcept;
[[nodiscard]] std::optional<Wgs84Llh> wgs84_ecef_to_llh(Ecef ecef) noexcept;
[[nodiscard]] std::optional<EnuReference> make_enu_reference(Wgs84Llh llh) noexcept;
[[nodiscard]] std::optional<EnuReference> make_enu_reference(Ecef ecef) noexcept;
[[nodiscard]] Enu ecef_to_enu(Ecef position, const EnuReference& reference) noexcept;
[[nodiscard]] Ecef enu_to_ecef(Enu position, const EnuReference& reference) noexcept;

} // namespace rtktrace
