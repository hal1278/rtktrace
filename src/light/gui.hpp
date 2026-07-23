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

#include "rtktrace/model/notification.hpp"
#include "rtktrace/plot/implot_component.hpp"
#include "rtktrace/session_state.hpp"

namespace rtktrace {

inline constexpr int light_minimum_window_width = 800;
inline constexpr int light_minimum_window_height = 600;

namespace detail {

struct TrajectoryAxisResizeProgress {
    double response{1.0};
    std::optional<int> previous_outer_size;
    std::optional<double> previous_axis_size;
    std::optional<int> requested_outer_size;
    std::uint8_t no_progress_observations{0};
};

struct TrajectoryResizeController {
    TrajectoryAxisResizeProgress east;
    TrajectoryAxisResizeProgress north;
    std::optional<double> previous_total_error;
    std::optional<std::array<int, 2>> previous_target;
    std::optional<std::array<int, 2>> target_before_previous;
    std::uint8_t non_decreasing_error_observations{0};
    std::uint16_t total_steps{0};
};

struct TrajectoryWindowResize {
    int width;
    int height;
    bool axis_size_satisfied;
    bool east_constrained;
    bool north_constrained;
    bool window_manager_no_progress;
    bool controller_termination;
};

[[nodiscard]] TrajectoryWindowResize trajectory_window_resize(int width, int height,
    int maximum_width, int maximum_height, const TrajectoryPlotMetrics& metrics,
    const TrajectoryResizeRequest& request, TrajectoryResizeController& controller) noexcept;

[[nodiscard]] std::optional<TrajectoryResizeRequest> feasible_axis_range_request(int width,
    int height, int maximum_width, int maximum_height, const TrajectoryPlotMetrics& metrics,
    const TrajectoryResizeRequest& request, const TrajectoryResizeController& controller) noexcept;

} // namespace detail

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

    struct PendingTrajectoryResize {
        enum class Phase : std::uint8_t {
            Resize,
            Rollback,
            VerifySettle,
        };

        bool reference;
        TrajectoryResizeRequest request;
        TrajectoryResizeRequest original_request;
        detail::TrajectoryResizeController controller;
        int initial_width{0};
        int initial_height{0};
        Phase phase{Phase::Resize};
        std::uint8_t rollback_attempts{0};
        std::uint8_t settle_attempts{0};
        std::uint8_t geometry_failure_observations{0};
        bool scale_warning_sent{false};
        bool rollback_started{false};
        bool rollback_failed{false};
    };

    static void SDLCALL file_dialog_callback(
        void* userdata, const char* const* filelist, int filter);

    void drain_dialog_paths();
    void process_load_queue();
    void attempt_load(PendingLoad load);
    void notify(NotificationLevel level, std::string message, bool open = false);
    void record_diagnostics(const std::vector<Diagnostic>& diagnostics);
    void prepare_plots_if_needed();
    void mark_plot_data_changed(bool fit_axes);
    bool synchronize_window_maximum(SDL_Window* window);

    void render_toolbar(SDL_Window* window);
    bool render_slot_rail();
    bool render_expanded_sidebar();
    void render_view_tabs();
    void render_plot_content();
    void apply_window_resize_request(SDL_Window* window);
    bool render_both(ImPlotComponent& component, std::string_view id_prefix);
    void render_summary();
    [[nodiscard]] float summary_height() const noexcept;
    void render_notification_window();
    void render_file_workflow_modals();
    void render_time_range_dialog();
    void render_enu_dialog();
    void render_match_dialog();
    void render_plot_range_dialog();

    PlotSessionState state_;
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
    bool notifications_open_{false};
    NotificationHistory notifications_;
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
    std::optional<PendingTrajectoryResize> pending_trajectory_resize_;
    bool normal_trajectory_rendered_this_frame_{false};
    bool relative_trajectory_rendered_this_frame_{false};
    SDL_DisplayID synchronized_maximum_display_{0};
    int synchronized_maximum_width_{0};
    int synchronized_maximum_height_{0};
    SDL_DisplayID maximum_sync_failure_display_{0};
    bool unknown_display_notification_sent_{false};
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
    double trajectory_range_backup_[4]{-1.0, 1.0, -1.0, 1.0};
    double trajectory_scale_value_{1.0};
    double trajectory_scale_backup_{1.0};
    double plot_time_values_[2]{0.0, 1.0};
    double plot_time_backup_[2]{0.0, 1.0};
    std::array<double, 5> plot_position_minimum_{};
    std::array<double, 5> plot_position_maximum_{};
    std::array<double, 5> plot_position_minimum_backup_{};
    std::array<double, 5> plot_position_maximum_backup_{};
    std::array<bool, 5> plot_position_present_{};
    std::array<bool, 5> plot_range_copy_targets_{};
    int plot_range_copy_source_{-1};
    int plot_position_unit_index_{1};
    int plot_scale_unit_index_{1};
};

} // namespace rtktrace
