#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "gui_runtime.hpp"
#include "rtktrace/model/notification.hpp"

namespace rtktrace {

inline constexpr int full_minimum_window_width = 800;
inline constexpr int full_minimum_window_height = 600;

namespace full_detail {

struct WindowRect {
    float x;
    float y;
    float width;
    float height;
};

struct InitialWindowLayout {
    WindowRect file_slots;
    WindowRect window_manager;
    WindowRect shared_controls;
    WindowRect initial_plot;
};

[[nodiscard]] InitialWindowLayout initial_window_layout(
    float x, float y, float width, float height) noexcept;
[[nodiscard]] WindowRect cascaded_plot_rect(
    const WindowRect& plot_region, std::size_t cascade_index) noexcept;

} // namespace full_detail

class FullGui {
public:
    FullGui();

    void enqueue_file(std::filesystem::path path);
    [[nodiscard]] bool add_loaded_file(LoadedFile file);
    void open_file_dialog(SDL_Window* window);
    void render(SDL_Window* window);

    [[nodiscard]] bool exit_requested() const noexcept;
    [[nodiscard]] const FullApplicationState& application_state() const noexcept;
    [[nodiscard]] std::size_t runtime_count() const noexcept;

private:
    struct PendingLoad {
        std::filesystem::path path;
        std::optional<InputFormat> format;
        bool size_confirmed{false};
        bool use_altitude_as_height{false};
        bool needs_missing_geoid_decision{false};
        bool needs_date{false};
        int date[3]{2026, 1, 1};
    };

    struct PlotLocalState {
        ImPlotComponentOptions options;
        std::uint64_t revision{1};
        bool placement_pending{true};
        std::size_t cascade_index{0};
        full_detail::WindowRect initial_rect{};
        std::array<char, 128> title_edit{};
        std::string title_backup;
        bool title_editing{false};
        bool ranges_initialized{false};
        double trajectory_ranges[4]{-1.0, 1.0, -1.0, 1.0};
        double trajectory_scale{1.0};
        std::array<char, 24> time_start_text{};
        std::array<char, 24> time_end_text{};
        std::optional<GpsTime> time_start_original;
        std::optional<GpsTime> time_end_original;
        bool time_start_edited{false};
        bool time_end_edited{false};
        std::array<double, 5> position_minimum{};
        std::array<double, 5> position_maximum{};
        std::array<bool, 5> position_present{};
        int range_copy_source{-1};
        std::array<bool, 5> range_copy_targets{};
    };

    static void SDLCALL file_dialog_callback(
        void* userdata, const char* const* filelist, int filter);

    [[nodiscard]] std::optional<PlotWindowId> create_plot(PlotType type);
    [[nodiscard]] PlotLocalState& local_state(PlotWindowId id);
    void erase_plot(PlotWindowId id);
    void synchronize_local_options();

    void drain_dialog_paths();
    void process_load_queue();
    void attempt_load(PendingLoad load);
    void record_diagnostics(const std::vector<Diagnostic>& diagnostics);
    void notify(NotificationLevel level, std::string message);
    [[nodiscard]] bool consume_escape_cancel();

    void render_main_menu(SDL_Window* window);
    void render_file_slots(SDL_Window* window, const full_detail::InitialWindowLayout& layout);
    void render_window_manager(const full_detail::InitialWindowLayout& layout);
    void render_shared_controls(const full_detail::InitialWindowLayout& layout);
    void render_plot_windows();
    void render_plot_window(
        const PlotInstanceState& plot, ImPlotComponent& component, PlotLocalState& local);
    void handle_plot_window_resize(ImPlotComponent& component, PlotLocalState& local);
    void render_plot_ranges(
        const PlotInstanceState& plot, ImPlotComponent& component, PlotLocalState& local);
    void render_notifications();
    void render_file_workflow_modals();
    void render_common_time_dialog();
    void render_enu_dialog();
    void render_reference_match_dialog();
    void render_options_dialog();
    void render_delete_confirmation();

    FullApplicationState state_;
    FullGuiRuntime runtime_;
    ImPlotComponentOptions shared_options_;
    std::uint64_t shared_options_revision_{1};
    std::map<PlotWindowId, PlotLocalState> plot_local_;

    bool exit_requested_{false};
    bool file_slots_visible_{true};
    bool window_manager_visible_{true};
    bool shared_controls_visible_{true};
    bool notifications_visible_{false};
    bool statistics_expected_{false};
    bool caution_present_{false};
    bool initial_layout_pending_{true};
    std::size_t cascade_index_{0};
    std::size_t hz_edit_slot_{0};
    double hz_edit_value_{1.0};

    std::mutex dialog_mutex_;
    std::vector<std::filesystem::path> dialog_paths_;
    std::string dialog_error_;
    std::deque<PendingLoad> load_queue_;
    std::optional<PendingLoad> format_decision_;
    std::optional<PendingLoad> nmea_decision_;
    std::optional<PendingLoad> large_file_decision_;
    bool large_file_popup_requested_{false};
    bool format_popup_requested_{false};
    bool nmea_popup_requested_{false};

    NotificationHistory notifications_;
    bool escape_cancel_consumed_{false};
    std::optional<PlotWindowId> delete_candidate_;
    bool delete_popup_requested_{false};
    std::optional<PlotWindowId> ranges_plot_;

    bool common_time_popup_requested_{false};
    bool common_time_initialized_{false};
    bool common_start_enabled_{false};
    bool common_end_enabled_{false};
    std::array<char, 24> common_start_text_{};
    std::array<char, 24> common_end_text_{};
    std::optional<GpsTime> common_start_original_;
    std::optional<GpsTime> common_end_original_;
    bool common_start_edited_{false};
    bool common_end_edited_{false};

    bool enu_popup_requested_{false};
    bool enu_initialized_{false};
    int enu_coordinate_kind_{0};
    double enu_values_[3]{0.0, 0.0, 0.0};

    bool match_popup_requested_{false};
    bool match_initialized_{false};
    bool match_enabled_{false};
    double match_seconds_{0.0};

    bool options_popup_requested_{false};
    bool options_initialized_{false};
    double trajectory_fit_ratio_{1.0};
    double time_series_fit_ratio_{1.0};
    float default_point_size_px_{2.0F};
    int zoom_modifier_index_{0};
    int resize_modifier_index_{2};
    int slot_order_index_{0};
    int quality_order_index_{0};
};

} // namespace rtktrace
