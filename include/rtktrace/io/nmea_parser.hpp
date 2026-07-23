#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>

#include "rtktrace/analysis/sample_rate.hpp"
#include "rtktrace/model/loaded_file.hpp"

namespace rtktrace {

inline constexpr std::int64_t default_nmea_rollover_tolerance_ns = 300'000'000'000;
inline constexpr std::int64_t default_nmea_datetime_validation_tolerance_ns = 300'000'000'000;
inline constexpr std::int64_t default_nmea_duplicate_epoch_tolerance_ns = 5'000'000;

struct NmeaDate {
    int year;
    unsigned month;
    unsigned day;
};

enum class MissingGeoidPolicy : std::uint8_t {
    RequireDecision,
    UseAltitudeAsEllipsoidalHeight,
    RejectFile,
};

struct NmeaParseOptions {
    std::int64_t duplicate_epoch_tolerance_ns{default_nmea_duplicate_epoch_tolerance_ns};
    std::int64_t rollover_tolerance_ns{default_nmea_rollover_tolerance_ns};
    std::int64_t datetime_validation_tolerance_ns{default_nmea_datetime_validation_tolerance_ns};
    std::int64_t rate_min_interval_ns{default_rate_min_interval_ns};
    std::optional<NmeaDate> fallback_date;
    MissingGeoidPolicy missing_geoid_policy{MissingGeoidPolicy::RequireDecision};
};

[[nodiscard]] LoadedFile parse_nmea(
    std::istream& input, std::filesystem::path source_path, NmeaParseOptions options = {});

} // namespace rtktrace
