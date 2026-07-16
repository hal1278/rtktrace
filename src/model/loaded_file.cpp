#include "plotcore/model/loaded_file.hpp"

#include <cmath>
#include <utility>

namespace plotcore {
namespace {

[[nodiscard]] bool is_valid_hz(double hz) noexcept
{
    return std::isfinite(hz) && hz > 0.0;
}

[[nodiscard]] bool is_valid_slot(const LoadedFiles& files, std::size_t slot_number) noexcept
{
    return slot_number > 0 && slot_number <= files.size();
}

} // namespace

LoadedFile::LoadedFile(std::filesystem::path source_path_value, InputFormat input_format_value)
    : source_path(std::move(source_path_value))
    , input_format(input_format_value)
{
}

std::optional<double> LoadedFile::estimated_hz() const noexcept
{
    return estimated_hz_;
}

std::optional<double> LoadedFile::override_hz() const noexcept
{
    return override_hz_;
}

std::optional<double> LoadedFile::effective_hz() const noexcept
{
    return override_hz_.has_value() ? override_hz_ : estimated_hz_;
}

bool LoadedFile::set_estimated_hz(double hz) noexcept
{
    if (!is_valid_hz(hz)) {
        return false;
    }
    estimated_hz_ = hz;
    return true;
}

void LoadedFile::clear_estimated_hz() noexcept
{
    estimated_hz_.reset();
}

bool LoadedFile::set_override_hz(double hz) noexcept
{
    if (!is_valid_hz(hz)) {
        return false;
    }
    override_hz_ = hz;
    return true;
}

void LoadedFile::clear_override_hz() noexcept
{
    override_hz_.reset();
}

LoadedFile* file_at_slot(LoadedFiles& files, std::size_t slot_number) noexcept
{
    return is_valid_slot(files, slot_number) ? &files[slot_number - 1] : nullptr;
}

const LoadedFile* file_at_slot(const LoadedFiles& files, std::size_t slot_number) noexcept
{
    return is_valid_slot(files, slot_number) ? &files[slot_number - 1] : nullptr;
}

LoadedFile* reference_file(LoadedFiles& files) noexcept
{
    return file_at_slot(files, 1);
}

const LoadedFile* reference_file(const LoadedFiles& files) noexcept
{
    return file_at_slot(files, 1);
}

bool move_slot(LoadedFiles& files, std::size_t from_slot_number, std::size_t to_slot_number)
{
    if (!is_valid_slot(files, from_slot_number) || !is_valid_slot(files, to_slot_number)) {
        return false;
    }
    if (from_slot_number == to_slot_number) {
        return true;
    }

    LoadedFile moved_file = std::move(files[from_slot_number - 1]);
    files.erase(files.begin() + static_cast<LoadedFiles::difference_type>(from_slot_number - 1));
    files.insert(
        files.begin() + static_cast<LoadedFiles::difference_type>(to_slot_number - 1),
        std::move(moved_file));
    return true;
}

bool erase_slot(LoadedFiles& files, std::size_t slot_number)
{
    if (!is_valid_slot(files, slot_number)) {
        return false;
    }
    files.erase(files.begin() + static_cast<LoadedFiles::difference_type>(slot_number - 1));
    return true;
}

} // namespace plotcore
