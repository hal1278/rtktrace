#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "plotcore/light/application_state.hpp"
#include "plotcore/plot/implot_component.hpp"

namespace plotcore {

class LightGui {
public:
    void enqueue_file(std::filesystem::path path);
    [[nodiscard]] bool add_loaded_file(LoadedFile file);
    void open_file_dialog(SDL_Window* window);
    void render(SDL_Window* window);

private:
    enum class ViewMode : std::uint8_t {
        NormalTrajectory,
        NormalTimeSeries,
        NormalBoth,
        ReferenceTrajectory,
        ReferenceTimeSeries,
        ReferenceBoth,
    };

    enum class StatisticsMode : std::uint8_t {
        Recorded,
        Expected,
    };

    struct PendingLoad {
        std::filesystem::path path;
        std::optional<InputFormat> format;
        bool size_confirmed{false};
        bool needs_missing_geoid_decision{false};
        bool needs_date{false};
        bool use_altitude_as_height{false};
        int date[3]{2026, 1, 1};
    };

    static void SDLCALL file_dialog_callback(
        void* userdata, const char* const* filelist, int filter);

    void drain_dialog_paths();
    void process_load_queue();
    void attempt_load(PendingLoad load);
    void prepare_plots_if_needed();
    void mark_plot_data_changed(bool fit_axes);

    void render_toolbar(SDL_Window* window);
    bool render_slot_rail();
    bool render_expanded_sidebar();
    void render_view_tabs();
    void render_plot_content();
    void render_both(ImPlotComponent& component, std::string_view id_prefix);
    void render_summary();
    void render_diagnostics_window();
    void render_file_workflow_modals();
    void render_time_range_dialog();
    void render_enu_dialog();
    void render_match_dialog();
    void render_plot_range_dialog();

    LightApplicationState state_;
    QualityFilter quality_filter_;
    ImPlotComponentOptions plot_options_;
    ImPlotComponent normal_plot_;
    ImPlotComponent relative_plot_;
    std::uint64_t prepared_state_revision_{0};
    std::uint64_t plot_settings_revision_{1};
    std::uint64_t prepared_settings_revision_{0};
    bool fit_on_prepare_{true};

    ViewMode view_mode_{ViewMode::NormalTrajectory};
    StatisticsMode statistics_mode_{StatisticsMode::Recorded};
    bool sidebar_expanded_{false};
    bool diagnostics_open_{false};
    float both_fraction_{0.5F};
    std::string status_message_;
    std::size_t hz_edit_slot_{0};
    double hz_edit_value_{1.0};

    std::mutex dialog_mutex_;
    std::vector<std::filesystem::path> dialog_paths_;
    std::string dialog_error_;
    std::deque<PendingLoad> load_queue_;
    std::optional<PendingLoad> modal_load_;
    std::optional<PendingLoad> nmea_decision_;
    bool format_popup_requested_{false};
    bool large_file_popup_requested_{false};
    bool nmea_popup_requested_{false};

    bool time_dialog_open_requested_{false};
    bool time_dialog_initialized_{false};
    bool time_start_enabled_{false};
    bool time_end_enabled_{false};
    double time_start_seconds_{0.0};
    double time_end_seconds_{0.0};

    bool enu_dialog_open_requested_{false};
    bool enu_dialog_initialized_{false};
    int enu_method_index_{0};
    int enu_coordinate_kind_{0};
    double enu_values_[3]{0.0, 0.0, 0.0};

    bool match_dialog_open_requested_{false};
    bool match_dialog_initialized_{false};
    bool match_tolerance_enabled_{false};
    double match_tolerance_seconds_{0.0};

    bool plot_range_dialog_open_requested_{false};
    bool plot_range_dialog_initialized_{false};
    double trajectory_range_values_[4]{-1.0, 1.0, -1.0, 1.0};
    double trajectory_scale_value_{1.0};
    double plot_time_values_[2]{0.0, 1.0};
    std::array<double, 5> plot_position_minimum_{};
    std::array<double, 5> plot_position_maximum_{};
    std::array<bool, 5> plot_position_present_{};
    std::array<bool, 5> plot_range_copy_targets_{};
    int plot_range_copy_source_{-1};
    int plot_position_unit_index_{1};
    int plot_scale_unit_index_{1};
};

} // namespace plotcore
