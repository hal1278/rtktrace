#pragma once

#include <optional>

#include "plotcore/model/sample.hpp"

namespace plotcore {

[[nodiscard]] Ecef wgs84_llh_to_ecef(Wgs84Llh llh) noexcept;
[[nodiscard]] std::optional<Wgs84Llh> wgs84_ecef_to_llh(Ecef ecef) noexcept;

} // namespace plotcore
