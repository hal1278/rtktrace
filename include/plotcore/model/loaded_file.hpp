#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "plotcore/model/diagnostic.hpp"
#include "plotcore/model/sample.hpp"

namespace plotcore {

enum class InputFormat : std::uint8_t {
    Pos,
    Nmea,
};

class LoadedFile {
public:
    LoadedFile(std::filesystem::path source_path, InputFormat input_format);

    std::filesystem::path source_path;
    InputFormat input_format;
    std::vector<NormalizedSample> samples;
    std::vector<Diagnostic> diagnostics;
    bool visible{true};

    [[nodiscard]] std::optional<double> estimated_hz() const noexcept;
    [[nodiscard]] std::optional<double> override_hz() const noexcept;
    [[nodiscard]] std::optional<double> effective_hz() const noexcept;

    [[nodiscard]] bool set_estimated_hz(double hz) noexcept;
    void clear_estimated_hz() noexcept;
    [[nodiscard]] bool set_override_hz(double hz) noexcept;
    void clear_override_hz() noexcept;

private:
    std::optional<double> estimated_hz_;
    std::optional<double> override_hz_;
};

using LoadedFiles = std::vector<LoadedFile>;

// Slot numbers are one-based; vector position keeps them contiguous.
[[nodiscard]] LoadedFile* file_at_slot(LoadedFiles& files, std::size_t slot_number) noexcept;
[[nodiscard]] const LoadedFile* file_at_slot(
    const LoadedFiles& files, std::size_t slot_number) noexcept;
[[nodiscard]] LoadedFile* reference_file(LoadedFiles& files) noexcept;
[[nodiscard]] const LoadedFile* reference_file(const LoadedFiles& files) noexcept;
[[nodiscard]] bool move_slot(
    LoadedFiles& files, std::size_t from_slot_number, std::size_t to_slot_number);
[[nodiscard]] bool erase_slot(LoadedFiles& files, std::size_t slot_number);

} // namespace plotcore
