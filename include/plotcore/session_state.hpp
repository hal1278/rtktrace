#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "plotcore/analysis/statistics.hpp"
#include "plotcore/io/nmea_parser.hpp"
#include "plotcore/io/pos_parser.hpp"
#include "plotcore/plot/data_view.hpp"

namespace plotcore {

enum class FileLoadStatus : std::uint8_t {
    Loaded,
    NeedsInputFormat,
    NeedsNmeaDecision,
    Rejected,
    IoError,
};

struct FileLoadResult {
    FileLoadStatus status;
    std::filesystem::path source_path;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] std::optional<InputFormat> infer_input_format(
    const std::filesystem::path& path);

class PlotSessionState {
public:
    [[nodiscard]] FileLoadResult load_file(const std::filesystem::path& path,
        std::optional<InputFormat> format = std::nullopt,
        NmeaParseOptions nmea_options = {});
    [[nodiscard]] bool add_loaded_file(LoadedFile file);

    [[nodiscard]] bool set_file_visible(std::size_t slot_number, bool visible);
    [[nodiscard]] bool move_file(
        std::size_t from_slot_number, std::size_t to_slot_number);
    [[nodiscard]] bool erase_file(std::size_t slot_number);
    [[nodiscard]] bool set_file_override_hz(
        std::size_t slot_number, std::optional<double> hz);

    [[nodiscard]] bool set_common_time_range(CommonTimeRange range);
    [[nodiscard]] bool use_intersection_time_range();
    [[nodiscard]] bool set_enu_reference_configuration(
        EnuReferenceConfiguration configuration);
    [[nodiscard]] bool set_reference_match_configuration(
        ReferenceMatchConfiguration configuration);

    [[nodiscard]] std::optional<PlotDataView> normal_plot_data_view() const;
    [[nodiscard]] std::optional<PlotDataView> relative_plot_data_view() const;
    [[nodiscard]] std::optional<TimeRange> effective_range() const noexcept;
    [[nodiscard]] std::vector<RecordedStatistics> recorded_statistics() const;

    [[nodiscard]] const LoadedFiles& files() const noexcept;
    [[nodiscard]] const CommonTimeRange& configured_time_range() const noexcept;
    [[nodiscard]] const EnuReferenceConfiguration& enu_configuration() const noexcept;
    [[nodiscard]] const ReferenceMatchConfiguration& match_configuration() const noexcept;
    [[nodiscard]] const std::vector<Diagnostic>& diagnostic_history() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] bool enu_available() const noexcept;

private:
    [[nodiscard]] bool rebuild_processing_state();
    void record_diagnostics(const LoadedFile& file);

    LoadedFiles files_;
    CommonTimeRange configured_time_range_;
    CommonTimeRangeIndex time_index_;
    EnuReferenceConfiguration enu_configuration_;
    EnuCache enu_cache_;
    ReferenceMatchConfiguration match_configuration_{false, 0};
    RelativeCache relative_cache_;
    std::optional<TimeRange> effective_range_;
    std::vector<Diagnostic> diagnostic_history_;
    std::uint64_t revision_{0};
};

} // namespace plotcore
