#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>

#include "plotcore/model/loaded_file.hpp"

namespace plotcore {

inline constexpr std::int64_t default_duplicate_epoch_tolerance_ns = 5'000'000;

struct PosParseOptions {
    std::int64_t duplicate_epoch_tolerance_ns{default_duplicate_epoch_tolerance_ns};
};

[[nodiscard]] LoadedFile parse_pos(
    std::istream& input, std::filesystem::path source_path, PosParseOptions options = {});

} // namespace plotcore
