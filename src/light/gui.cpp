#include "gui.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <SDL3/SDL_dialog.h>

#include "imgui.h"

namespace rtktrace {
namespace {

constexpr std::uintmax_t large_file_threshold_bytes = 100ULL * 1024ULL * 1024ULL;
constexpr double nanoseconds_per_second = 1'000'000'000.0;
constexpr double trajectory_axis_size_tolerance_px = 1.0;
constexpr std::uint8_t maximum_window_manager_no_progress_observations = 2;
constexpr double minimum_trajectory_resize_response = 0.01;
constexpr std::uint8_t maximum_non_decreasing_resize_observations = 4;
constexpr std::uint16_t maximum_trajectory_resize_steps = 24;
constexpr std::uint8_t maximum_trajectory_rollback_attempts = 2;
constexpr std::uint8_t maximum_trajectory_settle_attempts = 2;
constexpr std::uint8_t maximum_trajectory_geometry_failures = 3;
constexpr std::array position_unit_labels{"km", "m", "mm"};
constexpr std::array position_unit_scale_m{1000.0, 1.0, 0.001};
constexpr std::array modifier_choices{ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt};

[[nodiscard]] ImVec4 rgba(Rgba8 value) noexcept
{
    constexpr float scale = 1.0F / 255.0F;
    return ImVec4{value.red * scale, value.green * scale, value.blue * scale, value.alpha * scale};
}

[[nodiscard]] ImVec4 darker_hover_color(ImVec4 value) noexcept
{
    constexpr float hover_brightness = 0.85F;
    return ImVec4{value.x * hover_brightness, value.y * hover_brightness,
        value.z * hover_brightness, value.w};
}

[[nodiscard]] std::string slot_label(std::size_t slot, bool reference)
{
    return reference ? "R" + std::to_string(slot) : std::to_string(slot);
}

[[nodiscard]] NmeaDate file_date(const std::filesystem::path& path)
{
    using namespace std::chrono;
    std::error_code error;
    const std::filesystem::file_time_type modified = std::filesystem::last_write_time(path, error);
    system_clock::time_point system_time = system_clock::now();
    if (!error) {
        system_time = time_point_cast<system_clock::duration>(
            modified - std::filesystem::file_time_type::clock::now() + system_clock::now());
    }
    const year_month_day date{floor<days>(system_time)};
    return NmeaDate{static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day())};
}

[[nodiscard]] std::optional<GpsTime> seconds_to_gps_time(double seconds) noexcept
{
    const long double nanoseconds = static_cast<long double>(seconds) * nanoseconds_per_second;
    if (!std::isfinite(seconds)
        || nanoseconds < static_cast<long double>(std::numeric_limits<std::int64_t>::min())
        || nanoseconds > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return GpsTime{static_cast<std::int64_t>(std::llround(nanoseconds))};
}

[[nodiscard]] bool valid_numeric_range(double minimum, double maximum) noexcept
{
    return std::isfinite(minimum) && std::isfinite(maximum) && minimum < maximum;
}

struct NumericInputResult {
    bool entered;
    bool deactivated_after_edit;
};

[[nodiscard]] NumericInputResult input_range_value(
    const char* label, double* value, bool range_is_valid, const char* format)
{
    static ImGuiID editing_input = 0;
    if (!range_is_valid) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.45F, 0.08F, 0.08F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.58F, 0.10F, 0.10F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0.68F, 0.12F, 0.12F, 1.0F});
    }
    const ImGuiID input_id = ImGui::GetID(label);
    const bool active = editing_input == input_id;
    const bool entered = ImGui::InputDouble(
        label, value, 0.0, 0.0, active ? format : "%.3g", ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActive()) {
        editing_input = input_id;
    } else if (editing_input == input_id) {
        editing_input = 0;
    }
    const bool deactivated_after_edit = ImGui::IsItemDeactivatedAfterEdit();
    if (!range_is_valid) {
        ImGui::PopStyleColor(3);
    }
    return NumericInputResult{entered, deactivated_after_edit};
}

[[nodiscard]] NumericInputResult input_absolute_gps_time(
    const char* label, detail::AbsoluteGpsTimeEdit& edit, bool range_is_valid)
{
    if (!range_is_valid) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.45F, 0.08F, 0.08F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.58F, 0.10F, 0.10F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0.68F, 0.12F, 0.12F, 1.0F});
    }
    const bool entered = ImGui::InputText(
        label, edit.text.data(), edit.text.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited()) {
        edit.edited = true;
    }
    const bool deactivated_after_edit = ImGui::IsItemDeactivatedAfterEdit();
    if (!range_is_valid) {
        ImGui::PopStyleColor(3);
    }
    return NumericInputResult{entered, deactivated_after_edit};
}

[[nodiscard]] double displayed_position(double meters, int unit_index) noexcept
{
    return meters / position_unit_scale_m[static_cast<std::size_t>(unit_index)];
}

[[nodiscard]] double position_in_meters(double value, int unit_index) noexcept
{
    return value * position_unit_scale_m[static_cast<std::size_t>(unit_index)];
}

void rescale_displayed_positions(
    int previous_unit_index, int next_unit_index, std::span<double> displayed_values)
{
    const double previous_scale =
        position_unit_scale_m[static_cast<std::size_t>(previous_unit_index)];
    const double next_scale = position_unit_scale_m[static_cast<std::size_t>(next_unit_index)];
    for (double& value : displayed_values) {
        value *= previous_scale / next_scale;
    }
}

[[nodiscard]] ImGuiKeyChord modifier_choice_at(int index) noexcept
{
    const int clamped = std::clamp(index, 0, static_cast<int>(modifier_choices.size()) - 1);
    return modifier_choices[static_cast<std::size_t>(clamped)];
}

[[nodiscard]] int modifier_choice_index(ImGuiKeyChord modifier) noexcept
{
    for (std::size_t index = 0; index < modifier_choices.size(); ++index) {
        if (modifier_choices[index] == modifier) {
            return static_cast<int>(index);
        }
    }
    return 0;
}

} // namespace

bool detail::valid_fit_ratio(double ratio) noexcept
{
    return std::isfinite(ratio) && ratio > 0.0 && ratio <= 1.0;
}

bool detail::valid_modifier_choice(ImGuiKeyChord modifier) noexcept
{
    return std::find(modifier_choices.begin(), modifier_choices.end(), modifier)
        != modifier_choices.end();
}

bool detail::valid_distinct_modifier_choices(
    ImGuiKeyChord zoom_center_modifier, ImGuiKeyChord window_resize_modifier) noexcept
{
    return valid_modifier_choice(zoom_center_modifier)
        && valid_modifier_choice(window_resize_modifier)
        && zoom_center_modifier != window_resize_modifier;
}

ImPlotComponentOptions detail::initial_plot_options(const LightOptionsState& options) noexcept
{
    ImPlotComponentOptions plot_options;
    if (valid_fit_ratio(options.trajectory_fit_ratio)) {
        plot_options.trajectory_fit_ratio = options.trajectory_fit_ratio;
    }
    if (valid_fit_ratio(options.time_series_fit_ratio)) {
        plot_options.time_series_fit_ratio = options.time_series_fit_ratio;
    }
    if (std::isfinite(options.default_point_size_px) && options.default_point_size_px >= 1.0F) {
        plot_options.marker_size_px = options.default_point_size_px;
    }
    if (valid_distinct_modifier_choices(
            options.zoom_center_modifier, options.window_resize_modifier)) {
        plot_options.zoom_center_modifier = options.zoom_center_modifier;
        plot_options.window_resize_modifier = options.window_resize_modifier;
    }
    return plot_options;
}

bool detail::apply_light_options(
    ImPlotComponentOptions& plot_options, const LightOptionsState& options) noexcept
{
    if (!valid_fit_ratio(options.trajectory_fit_ratio)
        || !valid_fit_ratio(options.time_series_fit_ratio)
        || !valid_distinct_modifier_choices(
            options.zoom_center_modifier, options.window_resize_modifier)) {
        return false;
    }
    plot_options.trajectory_fit_ratio = options.trajectory_fit_ratio;
    plot_options.time_series_fit_ratio = options.time_series_fit_ratio;
    plot_options.zoom_center_modifier = options.zoom_center_modifier;
    plot_options.window_resize_modifier = options.window_resize_modifier;
    return true;
}

EnuReferenceConfiguration detail::enu_configuration_for_method(
    const EnuReferenceConfiguration& current, EnuReferenceMethod method) noexcept
{
    EnuReferenceConfiguration next = current;
    next.method = method;
    return next;
}

bool detail::initialize_absolute_gps_time_edit(AbsoluteGpsTimeEdit& edit, GpsTime value)
{
    const std::optional<std::string> formatted = format_absolute_gps_time(value);
    if (!formatted.has_value() || formatted->size() + 1 > edit.text.size()) {
        edit = {};
        return false;
    }
    edit.text.fill('\0');
    std::copy(formatted->begin(), formatted->end(), edit.text.begin());
    edit.original = value;
    edit.edited = false;
    return true;
}

std::optional<GpsTime> detail::resolve_absolute_gps_time_edit(
    const AbsoluteGpsTimeEdit& edit) noexcept
{
    if (!edit.edited) {
        return edit.original;
    }
    const auto end = std::find(edit.text.begin(), edit.text.end(), '\0');
    return parse_absolute_gps_time(
        std::string_view{edit.text.data(), static_cast<std::size_t>(end - edit.text.begin())});
}

detail::TrajectoryWindowResize detail::trajectory_window_resize(int width, int height,
    int maximum_width, int maximum_height, const TrajectoryPlotMetrics& metrics,
    const TrajectoryResizeRequest& request, TrajectoryResizeController& controller) noexcept
{
    const auto advance_axis = [](TrajectoryAxisResizeProgress& progress, int outer_size,
                                  double axis_size, double desired_axis_size,
                                  int minimum_outer_size, int maximum_outer_size) {
        if (progress.previous_outer_size.has_value() && progress.previous_axis_size.has_value()) {
            const double outer_delta = outer_size - *progress.previous_outer_size;
            const double axis_delta = axis_size - *progress.previous_axis_size;
            if (std::abs(outer_delta) >= 0.5 && std::abs(axis_delta) >= 0.25
                && outer_delta * axis_delta > 0.0) {
                progress.response = std::clamp(
                    std::abs(axis_delta / outer_delta), minimum_trajectory_resize_response, 1.0);
            }
            if (progress.requested_outer_size.has_value()
                && outer_size != *progress.requested_outer_size && std::abs(outer_delta) < 0.5) {
                if (progress.no_progress_observations
                    < maximum_window_manager_no_progress_observations) {
                    ++progress.no_progress_observations;
                }
            } else {
                progress.no_progress_observations = 0;
            }
        }
        progress.previous_outer_size = outer_size;
        progress.previous_axis_size = axis_size;

        const double error = desired_axis_size - axis_size;
        const bool satisfied = std::abs(error) <= trajectory_axis_size_tolerance_px;
        const bool constrained = !satisfied
            && ((error > 0.0 && outer_size >= maximum_outer_size)
                || (error < 0.0 && outer_size <= minimum_outer_size));
        int requested_outer_size = outer_size;
        if (!satisfied && !constrained) {
            const double requested = std::clamp(
                static_cast<double>(outer_size) + error / progress.response,
                static_cast<double>(minimum_outer_size), static_cast<double>(maximum_outer_size));
            requested_outer_size = static_cast<int>(std::lround(requested));
        }
        progress.requested_outer_size = requested_outer_size == outer_size
            ? std::nullopt
            : std::optional<int>{requested_outer_size};
        if (satisfied || constrained) {
            progress.no_progress_observations = 0;
        }
        return std::array{requested_outer_size, static_cast<int>(satisfied),
            static_cast<int>(constrained),
            static_cast<int>(progress.no_progress_observations
                >= maximum_window_manager_no_progress_observations)};
    };

    const std::array east = advance_axis(controller.east, width, metrics.east_axis_length_px,
        request.desired_east_axis_length_px, light_minimum_window_width, maximum_width);
    const std::array north = advance_axis(controller.north, height, metrics.north_axis_length_px,
        request.desired_north_axis_length_px, light_minimum_window_height, maximum_height);
    const bool axis_size_satisfied = east[1] != 0 && north[1] != 0;
    const bool constrained = east[2] != 0 || north[2] != 0;
    const double total_error =
        std::abs(request.desired_east_axis_length_px - metrics.east_axis_length_px)
        + std::abs(request.desired_north_axis_length_px - metrics.north_axis_length_px);
    if (!axis_size_satisfied && !constrained) {
        ++controller.total_steps;
        const double progress_epsilon = controller.previous_total_error.has_value()
            ? std::numeric_limits<double>::epsilon()
                * std::max(std::abs(*controller.previous_total_error), 1.0) * 16.0
            : 0.0;
        if (controller.previous_total_error.has_value()
            && total_error >= *controller.previous_total_error - progress_epsilon) {
            if (controller.non_decreasing_error_observations
                < maximum_non_decreasing_resize_observations) {
                ++controller.non_decreasing_error_observations;
            }
        } else {
            controller.non_decreasing_error_observations = 0;
        }
    } else {
        controller.non_decreasing_error_observations = 0;
    }
    controller.previous_total_error = total_error;
    const std::array target{east[0], north[0]};
    const bool cycle = controller.target_before_previous.has_value()
        && target == *controller.target_before_previous
        && (!controller.previous_target.has_value() || target != *controller.previous_target);
    controller.target_before_previous = controller.previous_target;
    controller.previous_target = target;
    const bool controller_termination = !axis_size_satisfied && !constrained
        && (cycle || controller.total_steps >= maximum_trajectory_resize_steps
            || controller.non_decreasing_error_observations
                >= maximum_non_decreasing_resize_observations);
    return detail::TrajectoryWindowResize{east[0], north[0], east[1] != 0 && north[1] != 0,
        east[2] != 0, north[2] != 0, east[3] != 0 || north[3] != 0, controller_termination};
}

std::optional<TrajectoryResizeRequest> detail::feasible_axis_range_request(int width, int height,
    int maximum_width, int maximum_height, const TrajectoryPlotMetrics& metrics,
    const TrajectoryResizeRequest& request, const TrajectoryResizeController& controller) noexcept
{
    struct Candidate {
        TrajectoryResizeRequest request;
        double distance;
    };
    std::optional<Candidate> best;
    const auto consider = [&](double meters_per_pixel) {
        if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0) {
            return;
        }
        TrajectoryResizeRequest candidate = request;
        candidate.meters_per_pixel = meters_per_pixel;
        candidate.desired_east_axis_length_px = request.east.length() / meters_per_pixel;
        candidate.desired_north_axis_length_px = request.north.length() / meters_per_pixel;
        const double target_width = width
            + (candidate.desired_east_axis_length_px - metrics.east_axis_length_px)
                / controller.east.response;
        const double target_height = height
            + (candidate.desired_north_axis_length_px - metrics.north_axis_length_px)
                / controller.north.response;
        if (target_width < light_minimum_window_width - trajectory_axis_size_tolerance_px
            || target_width > maximum_width + trajectory_axis_size_tolerance_px
            || target_height < light_minimum_window_height - trajectory_axis_size_tolerance_px
            || target_height > maximum_height + trajectory_axis_size_tolerance_px) {
            return;
        }
        const double distance = std::abs(std::log(meters_per_pixel / request.meters_per_pixel));
        if (!best.has_value() || distance < best->distance) {
            best = Candidate{candidate, distance};
        }
    };
    consider(request.east.length() / std::max(metrics.east_axis_length_px, 1.0));
    consider(request.north.length() / std::max(metrics.north_axis_length_px, 1.0));
    return best.has_value() ? std::optional<TrajectoryResizeRequest>{best->request} : std::nullopt;
}

LightGui::LightGui() : plot_options_(detail::initial_plot_options(options_)) {}

void LightGui::enqueue_file(std::filesystem::path path)
{
    if (!path.empty()) {
        PendingLoad load;
        load.path = std::move(path);
        load_queue_.push_back(std::move(load));
    }
}

bool LightGui::add_loaded_file(LoadedFile file)
{
    const bool first_file = state_.files().empty();
    if (!state_.add_loaded_file(std::move(file))) {
        return false;
    }
    mark_plot_data_changed(first_file);
    return true;
}

void LightGui::notify(NotificationLevel level, std::string message, bool open)
{
    status_message_ = message;
    notifications_.add(level, std::move(message));
    notifications_open_ = notifications_open_ || open;
}

void LightGui::record_diagnostics(const std::vector<Diagnostic>& diagnostics)
{
    for (const Diagnostic& diagnostic : diagnostics) {
        notifications_.add(diagnostic);
        if (diagnostic.code == DiagnosticCode::DateValidationMismatch) {
            notifications_open_ = true;
        }
    }
}

void LightGui::open_file_dialog(SDL_Window* window)
{
    static constexpr std::array filters{
        SDL_DialogFileFilter{"GNSS position", "pos;nmea;gga"},
        SDL_DialogFileFilter{"All files", "*"},
    };
    SDL_ShowOpenFileDialog(file_dialog_callback, this, window, filters.data(),
        static_cast<int>(filters.size()), nullptr, true);
}

void LightGui::file_dialog_callback(void* userdata, const char* const* filelist, int)
{
    LightGui& gui = *static_cast<LightGui*>(userdata);
    if (filelist == nullptr) {
        std::scoped_lock lock{gui.dialog_mutex_};
        gui.dialog_error_ = SDL_GetError();
        return;
    }
    std::scoped_lock lock{gui.dialog_mutex_};
    for (std::size_t index = 0; filelist[index] != nullptr; ++index) {
        gui.dialog_paths_.emplace_back(filelist[index]);
    }
}

void LightGui::drain_dialog_paths()
{
    std::vector<std::filesystem::path> paths;
    {
        std::scoped_lock lock{dialog_mutex_};
        paths.swap(dialog_paths_);
        if (!dialog_error_.empty()) {
            notify(NotificationLevel::Error, "File dialog failed: " + dialog_error_, true);
            dialog_error_.clear();
        }
    }
    for (std::filesystem::path& path : paths) {
        enqueue_file(std::move(path));
    }
}

void LightGui::process_load_queue()
{
    if (modal_load_.has_value() || nmea_decision_.has_value() || load_queue_.empty()) {
        return;
    }
    PendingLoad load = std::move(load_queue_.front());
    load_queue_.pop_front();
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(load.path, error);
    if (!load.size_confirmed && !error && size >= large_file_threshold_bytes) {
        notify(NotificationLevel::Info,
            "Awaiting large-file confirmation for " + load.path.filename().string());
        modal_load_ = std::move(load);
        large_file_popup_requested_ = true;
        return;
    }
    attempt_load(std::move(load));
}

void LightGui::attempt_load(PendingLoad load)
{
    NmeaParseOptions options;
    if (load.needs_missing_geoid_decision) {
        options.missing_geoid_policy = load.use_altitude_as_height
            ? MissingGeoidPolicy::UseAltitudeAsEllipsoidalHeight
            : MissingGeoidPolicy::RejectFile;
    }
    if (load.needs_date) {
        options.fallback_date = NmeaDate{
            load.date[0], static_cast<unsigned>(load.date[1]), static_cast<unsigned>(load.date[2])};
    }

    const bool first_file = state_.files().empty();
    FileLoadResult result = state_.load_file(load.path, load.format, options);
    switch (result.status) {
    case FileLoadStatus::Loaded: {
        record_diagnostics(result.diagnostics);
        const std::size_t warnings = static_cast<std::size_t>(std::count_if(
            result.diagnostics.begin(), result.diagnostics.end(), [](const Diagnostic& diagnostic) {
                return diagnostic.severity == DiagnosticSeverity::Warning;
            }));
        std::string message = "Loaded " + load.path.filename().string();
        if (warnings != 0) {
            message += " with " + std::to_string(warnings) + " warning(s)";
        }
        notify(NotificationLevel::Info, std::move(message));
        mark_plot_data_changed(first_file);
        break;
    }
    case FileLoadStatus::NeedsInputFormat:
        modal_load_ = std::move(load);
        format_popup_requested_ = true;
        break;
    case FileLoadStatus::NeedsNmeaDecision: {
        load.format = InputFormat::Nmea;
        for (const Diagnostic& diagnostic : result.diagnostics) {
            load.needs_missing_geoid_decision = load.needs_missing_geoid_decision
                || diagnostic.code == DiagnosticCode::MissingGeoidSeparation;
            load.needs_date = load.needs_date || diagnostic.code == DiagnosticCode::MissingDate;
        }
        const NmeaDate date = file_date(load.path);
        load.date[0] = date.year;
        load.date[1] = static_cast<int>(date.month);
        load.date[2] = static_cast<int>(date.day);
        nmea_decision_ = std::move(load);
        nmea_popup_requested_ = true;
        break;
    }
    case FileLoadStatus::Rejected:
        record_diagnostics(result.diagnostics);
        notify(NotificationLevel::Error, "Rejected " + load.path.filename().string(), true);
        break;
    case FileLoadStatus::IoError:
        record_diagnostics(result.diagnostics);
        notify(NotificationLevel::Error, "Could not open " + load.path.string(), true);
        break;
    }
}

void LightGui::prepare_plots_if_needed()
{
    if (prepared_state_revision_ == state_.revision()
        && prepared_settings_revision_ == plot_settings_revision_) {
        return;
    }
    const std::optional<PlotDataView> normal = state_.normal_plot_data_view();
    if (normal.has_value()) {
        normal_plot_.prepare(*normal, quality_filter_, plot_options_, fit_on_prepare_);
    } else {
        normal_plot_.clear();
    }
    const std::optional<PlotDataView> relative = state_.relative_plot_data_view();
    if (relative.has_value()) {
        relative_plot_.prepare(*relative, quality_filter_, plot_options_, fit_on_prepare_);
    } else {
        relative_plot_.clear();
    }
    prepared_state_revision_ = state_.revision();
    prepared_settings_revision_ = plot_settings_revision_;
    fit_on_prepare_ = false;
}

void LightGui::mark_plot_data_changed(bool fit_axes)
{
    fit_on_prepare_ = fit_on_prepare_ || fit_axes;
    if (!state_.files().empty() && !state_.enu_available()) {
        notify(NotificationLevel::Warning,
            "ENU reference is unavailable for slot 1 in the common time range");
    }
}

bool LightGui::synchronize_window_maximum(SDL_Window* window)
{
    if (window == nullptr) {
        return false;
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (display == 0) {
        synchronized_maximum_display_ = 0;
        if (!unknown_display_notification_sent_) {
            notify(NotificationLevel::Error, "Could not determine the window display");
            unknown_display_notification_sent_ = true;
        }
        return false;
    }
    unknown_display_notification_sent_ = false;
    SDL_Rect usable_bounds;
    if (!SDL_GetDisplayUsableBounds(display, &usable_bounds)) {
        if (maximum_sync_failure_display_ != display) {
            notify(NotificationLevel::Error, "Could not read the display usable bounds");
            maximum_sync_failure_display_ = display;
        }
        return false;
    }
    const int maximum_width = std::max(usable_bounds.w, light_minimum_window_width);
    const int maximum_height = std::max(usable_bounds.h, light_minimum_window_height);
    if (display == synchronized_maximum_display_ && maximum_width == synchronized_maximum_width_
        && maximum_height == synchronized_maximum_height_) {
        return true;
    }
    if (!SDL_SetWindowMaximumSize(window, maximum_width, maximum_height)) {
        if (maximum_sync_failure_display_ != display) {
            notify(NotificationLevel::Error,
                "Could not synchronize the window maximum size with the display usable bounds");
            maximum_sync_failure_display_ = display;
        }
        return false;
    }
    synchronized_maximum_display_ = display;
    synchronized_maximum_width_ = maximum_width;
    synchronized_maximum_height_ = maximum_height;
    maximum_sync_failure_display_ = 0;
    return true;
}

void LightGui::render_toolbar(SDL_Window* window)
{
    if (ImGui::Button("Open...")) {
        open_file_dialog(window);
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
        normal_plot_.request_fit();
        relative_plot_.request_fit();
    }
    ImGui::SameLine();
    if (ImGui::Button("Options")) {
        options_dialog_open_requested_ = true;
        options_dialog_initialized_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Time Range")) {
        time_dialog_open_requested_ = true;
        time_dialog_initialized_ = false;
    }
    ImGui::SameLine();
    const EnuReferenceConfiguration& enu_configuration = state_.enu_configuration();
    int enu_method_index = enu_user_specified_pending_
        ? static_cast<int>(EnuReferenceMethod::UserSpecified)
        : static_cast<int>(enu_configuration.method);
    ImGui::SetNextItemWidth(165.0F);
    if (ImGui::Combo("ENU reference", &enu_method_index,
            "Slot 1 start\0Slot 1 end\0Slot 1 ECEF average\0User specified\0")) {
        const EnuReferenceMethod selected_method =
            static_cast<EnuReferenceMethod>(enu_method_index);
        if (selected_method == EnuReferenceMethod::UserSpecified
            && !enu_configuration.user_position.has_value()) {
            enu_user_specified_pending_ = true;
            enu_dialog_open_requested_ = true;
            enu_dialog_initialized_ = false;
        } else if (state_.set_enu_reference_configuration(
                       detail::enu_configuration_for_method(enu_configuration, selected_method))) {
            enu_user_specified_pending_ = false;
            mark_plot_data_changed(false);
        } else {
            notify(NotificationLevel::Warning, "ENU reference is unavailable");
        }
    }
    if (enu_user_specified_pending_
        || state_.enu_configuration().method == EnuReferenceMethod::UserSpecified) {
        ImGui::SameLine();
        if (ImGui::Button("Edit...")) {
            enu_dialog_open_requested_ = true;
            enu_dialog_initialized_ = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reference Match")) {
        match_dialog_open_requested_ = true;
        match_dialog_initialized_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Ranges")) {
        plot_range_dialog_open_requested_ = true;
        plot_range_dialog_initialized_ = false;
    }

    ImGui::SameLine();
    const float notification_width = notifications_.has_caution() ? 104.0F : 70.0F;
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - notification_width));
    if (notifications_.has_caution()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.65F, 0.42F, 0.05F, 1.0F});
        if (ImGui::SmallButton("!")) {
            notifications_open_ = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Warning notifications are present");
        }
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Info")) {
        notifications_open_ = true;
    }

    ImGui::SetNextItemWidth(105.0F);
    int vertical = plot_options_.vertical_component == PositionComponent::Up ? 0 : 1;
    if (ImGui::Combo("##vertical", &vertical, "Up\0Height\0")) {
        plot_options_.vertical_component =
            vertical == 0 ? PositionComponent::Up : PositionComponent::EllipsoidalHeight;
        ++plot_settings_revision_;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("E", &plot_options_.show_east)) {
        ++plot_settings_revision_;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("N", &plot_options_.show_north)) {
        ++plot_settings_revision_;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("V", &plot_options_.show_vertical)) {
        ++plot_settings_revision_;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0F);
    int draw_mode = static_cast<int>(plot_options_.batch.draw_mode);
    if (ImGui::Combo("##draw-mode", &draw_mode, "Line\0Point\0Line + Point\0")) {
        plot_options_.batch.draw_mode = static_cast<DrawMode>(draw_mode);
        ++plot_settings_revision_;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0F);
    if (ImGui::SliderFloat(
            "##marker-size", &plot_options_.marker_size_px, 1.0F, 10.0F, "%.0f px")) {
        ++plot_settings_revision_;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Bridge", &plot_options_.batch.bridge_hidden_quality_samples)) {
        ++plot_settings_revision_;
    }

    for (std::size_t quality = 0; quality < solution_quality_count; ++quality) {
        ImGui::SameLine();
        const ImVec4 quality_color = rgba(rtkplot_file1_quality_colors[quality]);
        const ImVec4 state_color = quality_filter_.visible[quality]
            ? quality_color
            : ImVec4{0.2F, 0.2F, 0.2F, 1.0F};
        ImGui::PushStyleColor(ImGuiCol_Button, state_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, darker_hover_color(state_color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, state_color);
        const std::string label = "Q" + std::to_string(quality);
        if (ImGui::SmallButton(label.c_str())) {
            quality_filter_.visible[quality] = !quality_filter_.visible[quality];
            ++plot_settings_revision_;
        }
        ImGui::PopStyleColor(3);
    }
}

bool LightGui::render_slot_rail()
{
    ImGui::BeginChild("Slot rail", ImVec2{48.0F, 0.0F}, true);
    if (ImGui::Button(sidebar_expanded_ ? "<" : ">", ImVec2{32.0F, 28.0F})) {
        sidebar_expanded_ = !sidebar_expanded_;
    }
    std::optional<std::size_t> erase;
    std::optional<std::pair<std::size_t, std::size_t>> move;
    for (std::size_t index = 0; index < state_.files().size(); ++index) {
        const std::size_t slot = index + 1;
        const LoadedFile& file = state_.files()[index];
        ImGui::PushID(static_cast<int>(slot));
        if (!file.visible) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35F);
        }
        const std::string label = slot_label(slot, slot == 1);
        if (ImGui::Button(label.c_str(), ImVec2{32.0F, 30.0F})) {
            static_cast<void>(state_.set_file_visible(slot, !file.visible));
        }
        if (!file.visible) {
            ImGui::PopStyleVar();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", file.source_path.filename().string().c_str());
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("RTKTRACE_SLOT", &slot, sizeof(slot));
            ImGui::Text("Move slot %zu", slot);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RTKTRACE_SLOT")) {
                const std::size_t from = *static_cast<const std::size_t*>(payload->Data);
                move = std::pair{from, slot};
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("Slot actions")) {
            if (ImGui::IsWindowAppearing()) {
                hz_edit_slot_ = slot;
                hz_edit_value_ = file.effective_hz().value_or(1.0);
            }
            ImGui::TextUnformatted(file.source_path.filename().string().c_str());
            ImGui::InputDouble("Hz", &hz_edit_value_, 0.1, 1.0, "%.6g");
            if (ImGui::Button("Apply Hz")) {
                static_cast<void>(state_.set_file_override_hz(slot, hz_edit_value_));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Estimated")) {
                static_cast<void>(state_.set_file_override_hz(slot, std::nullopt));
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Remove")) {
                erase = slot;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::EndChild();
    if (move.has_value() && state_.move_file(move->first, move->second)) {
        mark_plot_data_changed(false);
    }
    if (erase.has_value() && state_.erase_file(*erase)) {
        mark_plot_data_changed(false);
    }
    return hovered;
}

bool LightGui::render_expanded_sidebar()
{
    if (!sidebar_expanded_) {
        return false;
    }
    ImGui::SameLine(0.0F, 4.0F);
    ImGui::BeginChild("File slots", ImVec2{270.0F, 0.0F}, true);
    ImGui::SeparatorText("File / Slots");
    for (std::size_t index = 0; index < state_.files().size(); ++index) {
        const LoadedFile& file = state_.files()[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::Text("%zu  %s", index + 1, file.source_path.filename().string().c_str());
        ImGui::SameLine();
        if (file.override_hz().has_value()) {
            ImGui::TextDisabled("%.6g Hz (manual)", *file.override_hz());
        } else if (file.estimated_hz().has_value()) {
            ImGui::TextDisabled("%.6g Hz", *file.estimated_hz());
        } else {
            ImGui::TextDisabled("rate unavailable");
        }
        ImGui::PopID();
    }
    if (state_.files().empty()) {
        ImGui::TextDisabled("Open POS or NMEA files to begin.");
    }
    ImGui::Separator();
    if (ImGui::Button("Notifications")) {
        notifications_open_ = true;
    }
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::EndChild();
    return hovered;
}

void LightGui::render_view_tabs()
{
    const auto tab = [this](const char* label, ViewMode mode) {
        const bool selected = view_mode_ == mode;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(label, ImVec2{105.0F, 0.0F})) {
            view_mode_ = mode;
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
    };
    ImGui::TextUnformatted("Normal");
    ImGui::SameLine(90.0F);
    tab("Trajectory##normal", ViewMode::NormalTrajectory);
    ImGui::SameLine();
    tab("Time Series##normal", ViewMode::NormalTimeSeries);
    ImGui::SameLine();
    tab("Both##normal", ViewMode::NormalBoth);
    ImGui::TextUnformatted("Reference");
    ImGui::SameLine(90.0F);
    tab("Trajectory##reference", ViewMode::ReferenceTrajectory);
    ImGui::SameLine();
    tab("Time Series##reference", ViewMode::ReferenceTimeSeries);
    ImGui::SameLine();
    tab("Both##reference", ViewMode::ReferenceBoth);
}

bool LightGui::render_both(ImPlotComponent& component, std::string_view id_prefix)
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    constexpr float splitter_width = 6.0F;
    const float left_width = std::max(100.0F, (available.x - splitter_width) * both_fraction_);
    const std::string left_id = std::string{id_prefix} + " trajectory pane";
    ImGui::BeginChild(left_id.c_str(), ImVec2{left_width, available.y}, false);
    const std::string trajectory_id = std::string{id_prefix} + " trajectory";
    const bool trajectory_rendered =
        component.render_trajectory(trajectory_id, PlotAreaSize{-1.0, -1.0});
    ImGui::EndChild();
    ImGui::SameLine(0.0F, 0.0F);
    const std::string splitter_id = std::string{id_prefix} + " splitter";
    ImGui::InvisibleButton(splitter_id.c_str(), ImVec2{splitter_width, available.y});
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(
        minimum, maximum, ImGui::GetColorU32(ImGuiCol_Separator));
    if (ImGui::IsItemActive()) {
        both_fraction_ = std::clamp(both_fraction_
                + ImGui::GetIO().MouseDelta.x / std::max(available.x - splitter_width, 1.0F),
            0.15F, 0.85F);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        both_fraction_ = 0.5F;
    }
    ImGui::SameLine(0.0F, 0.0F);
    const std::string right_id = std::string{id_prefix} + " time pane";
    ImGui::BeginChild(right_id.c_str(), ImVec2{0.0F, available.y}, false);
    const std::string time_id = std::string{id_prefix} + " time series";
    component.render_time_series(time_id, PlotAreaSize{-1.0, -1.0});
    ImGui::EndChild();
    return trajectory_rendered;
}

void LightGui::render_plot_content()
{
    normal_trajectory_rendered_this_frame_ = false;
    relative_trajectory_rendered_this_frame_ = false;
    ImGui::BeginChild("Plot content", ImVec2{0.0F, -(summary_height() + 4.0F)}, true);
    const bool reference = view_mode_ == ViewMode::ReferenceTrajectory
        || view_mode_ == ViewMode::ReferenceTimeSeries || view_mode_ == ViewMode::ReferenceBoth;
    ImPlotComponent& selected_component = reference ? relative_plot_ : normal_plot_;
    if (selected_component.empty()) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2{
            std::max(12.0F, available.x * 0.5F - 95.0F), std::max(12.0F, available.y * 0.5F)});
        ImGui::TextDisabled(reference && state_.files().size() < 2
                ? "Reference view needs two files"
                : "Open POS or NMEA files");
        ImGui::EndChild();
        return;
    }
    switch (view_mode_) {
    case ViewMode::NormalTrajectory:
        normal_trajectory_rendered_this_frame_ =
            normal_plot_.render_trajectory("Normal trajectory", PlotAreaSize{-1.0, -1.0});
        break;
    case ViewMode::NormalTimeSeries:
        normal_plot_.render_time_series("Normal time series", PlotAreaSize{-1.0, -1.0});
        break;
    case ViewMode::NormalBoth:
        normal_trajectory_rendered_this_frame_ = render_both(normal_plot_, "Normal");
        break;
    case ViewMode::ReferenceTrajectory:
        relative_trajectory_rendered_this_frame_ =
            relative_plot_.render_trajectory("Reference trajectory", PlotAreaSize{-1.0, -1.0});
        break;
    case ViewMode::ReferenceTimeSeries:
        relative_plot_.render_time_series("Reference time series", PlotAreaSize{-1.0, -1.0});
        break;
    case ViewMode::ReferenceBoth:
        relative_trajectory_rendered_this_frame_ = render_both(relative_plot_, "Reference");
        break;
    }
    ImGui::EndChild();
}

float LightGui::summary_height() const noexcept
{
    const std::size_t visible_rows = std::min<std::size_t>(state_.files().size(), 5);
    return ImGui::GetStyle().WindowPadding.y * 2.0F + ImGui::GetFrameHeightWithSpacing()
        + static_cast<float>(visible_rows) * ImGui::GetTextLineHeightWithSpacing()
        + ImGui::GetStyle().ScrollbarSize;
}

void LightGui::render_summary()
{
    ImGui::BeginChild(
        "Data summary", ImVec2{0.0F, summary_height()}, true, ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::BeginTable("Summary header", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 88.0F);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", status_message_.c_str());
        ImGui::TableSetColumnIndex(1);
        if (ImGui::SmallButton(
                statistics_mode_ == StatisticsMode::Recorded ? "Recorded" : "Expected")) {
            statistics_mode_ = statistics_mode_ == StatisticsMode::Recorded
                ? StatisticsMode::Expected
                : StatisticsMode::Recorded;
        }
        ImGui::EndTable();
    }
    const std::vector<RecordedStatistics> statistics = state_.recorded_statistics();
    const std::optional<TimeRange> range = state_.effective_range();
    for (std::size_t index = 0; index < statistics.size(); ++index) {
        const RecordedStatistics& item = statistics[index];
        const std::size_t recorded = recorded_sample_count(item);
        const std::optional<std::size_t> expected = range.has_value()
            ? calculate_expected_sample_count(*range, state_.files()[index].effective_hz())
            : std::nullopt;
        const std::size_t denominator =
            statistics_mode_ == StatisticsMode::Recorded ? recorded : expected.value_or(0);
        const std::optional<std::string> first = item.first_sample_time.has_value()
            ? format_absolute_gps_time(*item.first_sample_time)
            : std::nullopt;
        const std::optional<std::string> last = item.last_sample_time.has_value()
            ? format_absolute_gps_time(*item.last_sample_time)
            : std::nullopt;
        ImGui::Text("%zu %s  %s .. %s GPST  N=%zu", index + 1,
            state_.files()[index].source_path.filename().string().c_str(),
            first.value_or("unavailable").c_str(), last.value_or("unavailable").c_str(),
            denominator);
        if (statistics_mode_ == StatisticsMode::Expected && !expected.has_value()) {
            ImGui::SameLine();
            ImGui::TextDisabled("Expected unavailable");
        }
        for (std::size_t quality = 0; quality < solution_quality_count; ++quality) {
            ImGui::SameLine();
            const std::optional<double> percentage =
                quality_percentage(item.quality_counts[quality], denominator);
            if (percentage.has_value()) {
                ImGui::TextColored(rgba(rtkplot_file1_quality_colors[quality]), "Q%zu:%zu/%.1f%%",
                    quality, item.quality_counts[quality], *percentage);
            } else {
                ImGui::TextDisabled("Q%zu:%zu/-", quality, item.quality_counts[quality]);
            }
        }
    }
    ImGui::EndChild();
}

void LightGui::render_notification_window()
{
    if (!notifications_open_) {
        return;
    }
    if (ImGui::Begin("Notification history", &notifications_open_)) {
        if (ImGui::Button("Clear")) {
            notifications_.clear();
        }
        ImGui::Separator();
        for (const UserNotification& notification : notifications_.entries()) {
            const ImVec4 color = notification.level == NotificationLevel::Error
                ? ImVec4{1.0F, 0.35F, 0.35F, 1.0F}
                : notification.level == NotificationLevel::Warning
                ? ImVec4{1.0F, 0.72F, 0.25F, 1.0F}
                : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TextColored(color, "[%s]", notification_level_name(notification.level));
            ImGui::SameLine();
            ImGui::TextWrapped("%s", notification.message.c_str());
        }
        if (notifications_.entries().empty()) {
            ImGui::TextDisabled("No notifications recorded.");
        }
    }
    ImGui::End();
}

void LightGui::render_file_workflow_modals()
{
    if (large_file_popup_requested_) {
        ImGui::OpenPopup("Large input file");
        large_file_popup_requested_ = false;
    }
    if (ImGui::BeginPopupModal("Large input file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s is at least 100 MiB. Continue loading?",
            modal_load_->path.filename().string().c_str());
        if (ImGui::Button("Continue")) {
            PendingLoad load = std::move(*modal_load_);
            modal_load_.reset();
            load.size_confirmed = true;
            ImGui::CloseCurrentPopup();
            attempt_load(std::move(load));
        }
        ImGui::SameLine();
        if (ImGui::Button("Skip")) {
            notify(NotificationLevel::Info, "Skipped " + modal_load_->path.filename().string());
            modal_load_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (format_popup_requested_) {
        ImGui::OpenPopup("Choose input format");
        format_popup_requested_ = false;
    }
    if (ImGui::BeginPopupModal("Choose input format", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (consume_escape_cancel()) {
            notify(NotificationLevel::Info,
                "Cancelled format selection for " + modal_load_->path.filename().string());
            modal_load_.reset();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::Text("Format for %s", modal_load_->path.filename().string().c_str());
        if (ImGui::Button("POS")) {
            PendingLoad load = std::move(*modal_load_);
            modal_load_.reset();
            load.format = InputFormat::Pos;
            ImGui::CloseCurrentPopup();
            attempt_load(std::move(load));
        }
        ImGui::SameLine();
        if (ImGui::Button("NMEA")) {
            PendingLoad load = std::move(*modal_load_);
            modal_load_.reset();
            load.format = InputFormat::Nmea;
            ImGui::CloseCurrentPopup();
            attempt_load(std::move(load));
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            notify(NotificationLevel::Info,
                "Cancelled format selection for " + modal_load_->path.filename().string());
            modal_load_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (nmea_popup_requested_) {
        ImGui::OpenPopup("NMEA input decision");
        nmea_popup_requested_ = false;
    }
    if (ImGui::BeginPopupModal("NMEA input decision", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (consume_escape_cancel()) {
            notify(NotificationLevel::Info,
                "Cancelled NMEA load for " + nmea_decision_->path.filename().string());
            nmea_decision_.reset();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        PendingLoad& load = *nmea_decision_;
        ImGui::TextUnformatted(load.path.filename().string().c_str());
        if (load.needs_missing_geoid_decision) {
            ImGui::Checkbox("Use GGA altitude as ellipsoidal height", &load.use_altitude_as_height);
        }
        if (load.needs_date) {
            ImGui::TextUnformatted("No RMC/ZDA date. Confirm the file date:");
            ImGui::InputInt3("Y / M / D", load.date);
            if (ImGui::Button("Use file timestamp date")) {
                const NmeaDate date = file_date(load.path);
                load.date[0] = date.year;
                load.date[1] = static_cast<int>(date.month);
                load.date[2] = static_cast<int>(date.day);
            }
        }
        const bool can_continue = !load.needs_missing_geoid_decision || load.use_altitude_as_height;
        ImGui::BeginDisabled(!can_continue);
        if (ImGui::Button("OK")) {
            PendingLoad accepted = std::move(load);
            nmea_decision_.reset();
            ImGui::CloseCurrentPopup();
            attempt_load(std::move(accepted));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            notify(NotificationLevel::Info,
                "Cancelled NMEA load for " + nmea_decision_->path.filename().string());
            nmea_decision_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

bool LightGui::consume_escape_cancel()
{
    if (escape_cancel_consumed_ || !ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        return false;
    }
    escape_cancel_consumed_ = true;
    return true;
}

void LightGui::render_options_dialog()
{
    if (options_dialog_open_requested_) {
        ImGui::OpenPopup("Options");
        options_dialog_open_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (consume_escape_cancel()) {
        ImGui::CloseCurrentPopup();
        options_dialog_initialized_ = false;
        ImGui::EndPopup();
        return;
    }
    if (!options_dialog_initialized_) {
        options_trajectory_fit_ratio_ = options_.trajectory_fit_ratio;
        options_time_series_fit_ratio_ = options_.time_series_fit_ratio;
        options_default_point_size_px_ = options_.default_point_size_px;
        options_zoom_center_modifier_index_ = modifier_choice_index(options_.zoom_center_modifier);
        options_window_resize_modifier_index_ =
            modifier_choice_index(options_.window_resize_modifier);
        options_dialog_initialized_ = true;
    }

    ImGui::SeparatorText("Fit ratio");
    ImGui::InputDouble("Trajectory", &options_trajectory_fit_ratio_, 0.01, 0.1, "%.3f");
    ImGui::InputDouble("Time series", &options_time_series_fit_ratio_, 0.01, 0.1, "%.3f");
    const bool ratios_valid = detail::valid_fit_ratio(options_trajectory_fit_ratio_)
        && detail::valid_fit_ratio(options_time_series_fit_ratio_);
    if (!ratios_valid) {
        ImGui::TextColored(
            ImVec4{1.0F, 0.35F, 0.35F, 1.0F}, "Fit ratios must be greater than 0 and at most 1.");
    }

    ImGui::SeparatorText("Point size");
    ImGui::SliderFloat(
        "Default point size", &options_default_point_size_px_, 1.0F, 10.0F, "%.0f px");
    ImGui::TextDisabled("Used for a new session only; the toolbar controls this session.");
    const bool default_point_size_valid = std::isfinite(options_default_point_size_px_)
        && options_default_point_size_px_ >= 1.0F && options_default_point_size_px_ <= 10.0F;

    ImGui::SeparatorText("Mouse-wheel modifiers");
    ImGui::Combo("Center-fixed zoom", &options_zoom_center_modifier_index_,
        "Ctrl\0Shift\0Alt\0");
    ImGui::Combo("Window resize", &options_window_resize_modifier_index_, "Ctrl\0Shift\0Alt\0");
    const ImGuiKeyChord zoom_center_modifier =
        modifier_choice_at(options_zoom_center_modifier_index_);
    const ImGuiKeyChord window_resize_modifier =
        modifier_choice_at(options_window_resize_modifier_index_);
    const bool modifiers_valid = detail::valid_distinct_modifier_choices(
        zoom_center_modifier, window_resize_modifier);
    if (!modifiers_valid) {
        ImGui::TextColored(ImVec4{1.0F, 0.35F, 0.35F, 1.0F},
            "Center-fixed zoom and window resize must use different modifiers.");
    }

    ImGui::BeginDisabled(!ratios_valid || !default_point_size_valid || !modifiers_valid);
    if (ImGui::Button("Apply")) {
        const bool fit_ratios_changed = options_.trajectory_fit_ratio != options_trajectory_fit_ratio_
            || options_.time_series_fit_ratio != options_time_series_fit_ratio_;
        const bool modifiers_changed = options_.zoom_center_modifier != zoom_center_modifier
            || options_.window_resize_modifier != window_resize_modifier;
        options_.trajectory_fit_ratio = options_trajectory_fit_ratio_;
        options_.time_series_fit_ratio = options_time_series_fit_ratio_;
        options_.default_point_size_px = options_default_point_size_px_;
        options_.zoom_center_modifier = zoom_center_modifier;
        options_.window_resize_modifier = window_resize_modifier;
        if ((fit_ratios_changed || modifiers_changed)
            && detail::apply_light_options(plot_options_, options_)) {
            ++plot_settings_revision_;
        }
        ImGui::CloseCurrentPopup();
        options_dialog_initialized_ = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        options_dialog_initialized_ = false;
    }
    ImGui::EndPopup();
}

void LightGui::apply_window_resize_request(SDL_Window* window)
{
    struct WindowGeometry {
        int width;
        int height;
        int maximum_width;
        int maximum_height;
    };
    const auto window_geometry = [this, window](
                                     bool notify_errors = true) -> std::optional<WindowGeometry> {
        if (window == nullptr) {
            return std::nullopt;
        }
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSize(window, &width, &height)) {
            if (notify_errors) {
                notify(NotificationLevel::Error, "Could not read the window size");
            }
            return std::nullopt;
        }
        SDL_Rect usable_bounds;
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        if (display == 0) {
            if (notify_errors) {
                notify(NotificationLevel::Error, "Could not determine the window display");
            }
            return std::nullopt;
        }
        if (!SDL_GetDisplayUsableBounds(display, &usable_bounds)) {
            if (notify_errors) {
                notify(NotificationLevel::Error, "Could not read the display usable bounds");
            }
            return std::nullopt;
        }
        const int maximum_width = std::max(usable_bounds.w, light_minimum_window_width);
        const int maximum_height = std::max(usable_bounds.h, light_minimum_window_height);
        return WindowGeometry{width, height, maximum_width, maximum_height};
    };
    const auto trajectory_width_response = [this](bool reference) {
        const bool both =
            reference ? view_mode_ == ViewMode::ReferenceBoth : view_mode_ == ViewMode::NormalBoth;
        return both ? static_cast<double>(both_fraction_) : 1.0;
    };
    const auto trajectory_rendered = [this](bool reference) {
        return reference ? relative_trajectory_rendered_this_frame_
                         : normal_trajectory_rendered_this_frame_;
    };
    const auto apply_target = [](ImPlotComponent& component, const TrajectoryResizeRequest& request,
                                  const TrajectoryPlotMetrics& metrics) {
        NumericRange east = request.east;
        NumericRange north = request.north;
        if (request.fixed_target == TrajectoryResizeFixedTarget::DisplayScale) {
            const double east_center = (east.minimum + east.maximum) * 0.5;
            const double north_center = (north.minimum + north.maximum) * 0.5;
            const double east_span = request.meters_per_pixel * metrics.east_axis_length_px;
            const double north_span = request.meters_per_pixel * metrics.north_axis_length_px;
            east = NumericRange{east_center - east_span * 0.5, east_center + east_span * 0.5};
            north = NumericRange{north_center - north_span * 0.5, north_center + north_span * 0.5};
        }
        return component.set_trajectory_ranges(east, north);
    };
    const auto fixed_target_satisfied = [](const TrajectoryResizeRequest& request,
                                            const TrajectoryPlotMetrics& metrics) {
        if (request.fixed_target == TrajectoryResizeFixedTarget::DisplayScale) {
            const double tolerance =
                std::max(request.meters_per_pixel * 1.0e-6, std::numeric_limits<double>::epsilon());
            return std::abs(metrics.meters_per_pixel - request.meters_per_pixel) <= tolerance;
        }
        const double tolerance = std::max(request.meters_per_pixel, 1.0) * 1.0e-6
            + request.meters_per_pixel * trajectory_axis_size_tolerance_px;
        return std::abs(metrics.east.minimum - request.east.minimum) <= tolerance
            && std::abs(metrics.east.maximum - request.east.maximum) <= tolerance
            && std::abs(metrics.north.minimum - request.north.minimum) <= tolerance
            && std::abs(metrics.north.maximum - request.north.maximum) <= tolerance;
    };
    const auto request_window_size = [this, window](int width, int height) {
        if (!synchronize_window_maximum(window)) {
            return false;
        }
        if (!SDL_SetWindowSize(window, width, height)) {
            notify(NotificationLevel::Error, "Could not resize the window");
            return false;
        }
        if (!SDL_SyncWindow(window)) {
            notify(NotificationLevel::Warning,
                "The window manager did not confirm the trajectory resize");
        }
        return true;
    };
    if (pending_trajectory_resize_.has_value()) {
        PendingTrajectoryResize& pending = *pending_trajectory_resize_;
        if (!trajectory_rendered(pending.reference)) {
            return;
        }
        ImPlotComponent& component = pending.reference ? relative_plot_ : normal_plot_;
        const std::optional<WindowGeometry> geometry = window_geometry(false);
        if (!geometry.has_value()) {
            ++pending.geometry_failure_observations;
            if (pending.geometry_failure_observations == 1) {
                notify(NotificationLevel::Warning,
                    "Window geometry is temporarily unavailable; retrying the trajectory resize");
            }
            if (pending.geometry_failure_observations >= maximum_trajectory_geometry_failures) {
                notify(NotificationLevel::Error,
                    "Window geometry remained unavailable for three rendered frames; the pending "
                    "trajectory resize was terminated");
                pending_trajectory_resize_.reset();
            }
            return;
        }
        pending.geometry_failure_observations = 0;
        if (!component.trajectory_metrics().has_value()) {
            return;
        }
        const TrajectoryPlotMetrics& metrics = *component.trajectory_metrics();
        if (pending.phase == PendingTrajectoryResize::Phase::Rollback) {
            pending.rollback_started = true;
            const bool restored = geometry->width == pending.initial_width
                && geometry->height == pending.initial_height;
            if (!restored && pending.rollback_attempts < maximum_trajectory_rollback_attempts) {
                ++pending.rollback_attempts;
                if (request_window_size(pending.initial_width, pending.initial_height)) {
                    return;
                }
                pending.rollback_failed = true;
            } else if (!restored) {
                pending.rollback_failed = true;
            }
            if (pending.rollback_failed) {
                notify(NotificationLevel::Error,
                    "Trajectory resize rollback could not restore the initial window geometry; "
                    "settling at the reached geometry");
            }
            pending.request = pending.original_request;
            if (!apply_target(component, pending.original_request, metrics)) {
                notify(NotificationLevel::Error,
                    "Could not settle the trajectory fixed target after resize rollback");
                pending_trajectory_resize_.reset();
                return;
            }
            pending.phase = PendingTrajectoryResize::Phase::VerifySettle;
            pending.settle_attempts = 1;
            return;
        }
        if (pending.phase == PendingTrajectoryResize::Phase::VerifySettle) {
            if (fixed_target_satisfied(pending.request, metrics)) {
                if (pending.rollback_started && !pending.rollback_failed) {
                    notify(NotificationLevel::Warning,
                        "Trajectory resize did not converge; the initial window geometry and fixed "
                        "target were restored");
                }
                pending_trajectory_resize_.reset();
                return;
            }
            if (pending.settle_attempts < maximum_trajectory_settle_attempts
                && apply_target(component, pending.request, metrics)) {
                ++pending.settle_attempts;
                return;
            }
            notify(NotificationLevel::Error,
                pending.rollback_started
                    ? "Trajectory resize rollback completed, but the fixed target could not be "
                      "verified"
                    : "Trajectory resize reached a window constraint, but the fixed target could "
                      "not be verified");
            pending_trajectory_resize_.reset();
            return;
        }
        const detail::TrajectoryWindowResize resize = detail::trajectory_window_resize(
            geometry->width, geometry->height, geometry->maximum_width, geometry->maximum_height,
            metrics, pending.request, pending.controller);

        if (resize.window_manager_no_progress || resize.controller_termination) {
            notify(NotificationLevel::Warning,
                resize.window_manager_no_progress
                    ? "The window manager made no progress; rolling back the trajectory resize"
                    : "Trajectory resize stopped making progress; restoring the initial window "
                      "geometry");
            pending.phase = PendingTrajectoryResize::Phase::Rollback;
            pending.rollback_started = true;
            if (request_window_size(pending.initial_width, pending.initial_height)) {
                pending.rollback_attempts = 1;
            } else {
                pending.rollback_failed = true;
            }
            return;
        } else if (resize.east_constrained || resize.north_constrained) {
            if (pending.request.fixed_target == TrajectoryResizeFixedTarget::AxisRange) {
                const std::optional<TrajectoryResizeRequest> feasible =
                    detail::feasible_axis_range_request(geometry->width, geometry->height,
                        geometry->maximum_width, geometry->maximum_height, metrics,
                        pending.original_request, pending.controller);
                if (!feasible.has_value()) {
                    notify(NotificationLevel::Warning,
                        "No common trajectory scale can preserve both axis ranges within the "
                        "window bounds; restoring the initial window geometry");
                    pending.phase = PendingTrajectoryResize::Phase::Rollback;
                    pending.rollback_started = true;
                    if (request_window_size(pending.initial_width, pending.initial_height)) {
                        pending.rollback_attempts = 1;
                    } else {
                        pending.rollback_failed = true;
                    }
                    return;
                }
                const bool scale_changed =
                    std::abs(feasible->meters_per_pixel - pending.request.meters_per_pixel)
                    > pending.request.meters_per_pixel * 1.0e-9;
                pending.request = *feasible;
                const double east_response = pending.controller.east.response;
                const double north_response = pending.controller.north.response;
                pending.controller = {};
                pending.controller.east.response = east_response;
                pending.controller.north.response = north_response;
                if (scale_changed && !pending.scale_warning_sent) {
                    notify(NotificationLevel::Warning,
                        "The requested trajectory scale could not be maintained within the window "
                        "bounds");
                    pending.scale_warning_sent = true;
                }
                return;
            }
            notify(NotificationLevel::Warning,
                "Window or layout constraints prevented the requested trajectory axis range");
            pending.request = pending.original_request;
            if (!apply_target(component, pending.original_request, metrics)) {
                notify(NotificationLevel::Error,
                    "Could not settle the trajectory display scale at the constrained window "
                    "geometry");
                pending.phase = PendingTrajectoryResize::Phase::Rollback;
                pending.rollback_started = true;
                pending.rollback_failed = true;
                return;
            }
            pending.phase = PendingTrajectoryResize::Phase::VerifySettle;
            pending.settle_attempts = 1;
            return;
        } else if (!resize.axis_size_satisfied) {
            if (!request_window_size(resize.width, resize.height)) {
                pending.phase = PendingTrajectoryResize::Phase::Rollback;
                pending.rollback_started = true;
                pending.rollback_failed = true;
            }
            return;
        } else {
            if (!apply_target(component, pending.request, metrics)) {
                notify(NotificationLevel::Warning,
                    "Could not apply the requested trajectory fixed target; restoring the initial "
                    "window geometry");
                pending.phase = PendingTrajectoryResize::Phase::Rollback;
                pending.rollback_started = true;
                if (request_window_size(pending.initial_width, pending.initial_height)) {
                    pending.rollback_attempts = 1;
                } else {
                    pending.rollback_failed = true;
                }
                return;
            }
            pending.phase = PendingTrajectoryResize::Phase::VerifySettle;
            pending.settle_attempts = 1;
            return;
        }
    }

    bool reference_resize = false;
    std::optional<TrajectoryResizeRequest> trajectory_resize =
        normal_plot_.consume_trajectory_resize_request();
    if (!trajectory_resize.has_value()) {
        trajectory_resize = relative_plot_.consume_trajectory_resize_request();
        reference_resize = trajectory_resize.has_value();
    }
    std::optional<double> factor = normal_plot_.consume_window_resize_factor();
    if (!factor.has_value()) {
        factor = relative_plot_.consume_window_resize_factor();
    }
    if (trajectory_resize.has_value()) {
        const std::optional<WindowGeometry> geometry = window_geometry();
        if (!geometry.has_value()) {
            return;
        }
        PendingTrajectoryResize pending{reference_resize, *trajectory_resize, *trajectory_resize,
            {}, geometry->width, geometry->height};
        pending.controller.east.response = trajectory_width_response(reference_resize);
        pending_trajectory_resize_ = std::move(pending);
        return;
    }
    if (!factor.has_value() || window == nullptr) {
        return;
    }
    const std::optional<WindowGeometry> geometry = window_geometry();
    if (!geometry.has_value()) {
        return;
    }
    double requested_width_value = static_cast<double>(geometry->width) * *factor;
    double requested_height_value = static_cast<double>(geometry->height) * *factor;
    requested_width_value =
        std::clamp(requested_width_value, static_cast<double>(light_minimum_window_width),
            static_cast<double>(geometry->maximum_width));
    requested_height_value =
        std::clamp(requested_height_value, static_cast<double>(light_minimum_window_height),
            static_cast<double>(geometry->maximum_height));
    const int requested_width = static_cast<int>(std::lround(requested_width_value));
    const int requested_height = static_cast<int>(std::lround(requested_height_value));
    if (!synchronize_window_maximum(window)) {
        return;
    }
    if (!SDL_SetWindowSize(window, requested_width, requested_height)) {
        notify(NotificationLevel::Error, "Could not resize the window");
        return;
    }
}

void LightGui::render_time_range_dialog()
{
    if (time_dialog_open_requested_) {
        ImGui::OpenPopup("Common time range");
        time_dialog_open_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Common time range", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (consume_escape_cancel()) {
        ImGui::CloseCurrentPopup();
        time_dialog_initialized_ = false;
        ImGui::EndPopup();
        return;
    }
    if (!time_dialog_initialized_) {
        const CommonTimeRange& configured = state_.configured_time_range();
        const std::optional<TimeRange> effective = state_.effective_range();
        time_start_enabled_ = configured.start_enabled;
        time_end_enabled_ = configured.end_enabled;
        static_cast<void>(detail::initialize_absolute_gps_time_edit(time_start_edit_,
            configured.entered_start.value_or(
                effective.has_value() ? effective->start : GpsTime{0})));
        static_cast<void>(detail::initialize_absolute_gps_time_edit(time_end_edit_,
            configured.entered_end.value_or(effective.has_value() ? effective->end : GpsTime{0})));
        time_dialog_initialized_ = true;
    }
    std::optional<GpsTime> start = detail::resolve_absolute_gps_time_edit(time_start_edit_);
    std::optional<GpsTime> end = detail::resolve_absolute_gps_time_edit(time_end_edit_);
    ImGui::Checkbox("Start", &time_start_enabled_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!time_start_enabled_);
    ImGui::SetNextItemWidth(210.0F);
    static_cast<void>(
        input_absolute_gps_time("##start-gpst", time_start_edit_, start.has_value()));
    ImGui::EndDisabled();
    ImGui::Checkbox("End", &time_end_enabled_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!time_end_enabled_);
    ImGui::SetNextItemWidth(210.0F);
    static_cast<void>(input_absolute_gps_time("##end-gpst", time_end_edit_, end.has_value()));
    ImGui::EndDisabled();
    ImGui::TextDisabled("GPST: YYYY-MM-DD hh:mm:ss.sss");
    if (ImGui::Button("Use intersection")) {
        const std::optional<TimeRange> intersection = intersection_time_range(state_.files());
        if (intersection.has_value()) {
            static_cast<void>(
                detail::initialize_absolute_gps_time_edit(time_start_edit_, intersection->start));
            static_cast<void>(
                detail::initialize_absolute_gps_time_edit(time_end_edit_, intersection->end));
            time_start_enabled_ = time_end_enabled_ = true;
        } else {
            notify(NotificationLevel::Warning, "No common intersection");
        }
    }
    if (ImGui::Button("OK")) {
        start = detail::resolve_absolute_gps_time_edit(time_start_edit_);
        end = detail::resolve_absolute_gps_time_edit(time_end_edit_);
        CommonTimeRange range;
        range.start_enabled = time_start_enabled_;
        range.end_enabled = time_end_enabled_;
        range.entered_start = start;
        range.entered_end = end;
        if (state_.set_common_time_range(range)) {
            mark_plot_data_changed(false);
            ImGui::CloseCurrentPopup();
            time_dialog_initialized_ = false;
        } else {
            notify(NotificationLevel::Warning, "Invalid common time range");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        time_dialog_initialized_ = false;
    }
    ImGui::EndPopup();
}

void LightGui::render_enu_dialog()
{
    if (enu_dialog_open_requested_) {
        ImGui::OpenPopup("Edit ENU reference");
        enu_dialog_open_requested_ = false;
    }
    if (!ImGui::BeginPopupModal(
            "Edit ENU reference", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (consume_escape_cancel()) {
        ImGui::CloseCurrentPopup();
        enu_dialog_initialized_ = false;
        enu_user_specified_pending_ = false;
        ImGui::EndPopup();
        return;
    }
    if (!enu_dialog_initialized_) {
        const EnuReferenceConfiguration& configuration = state_.enu_configuration();
        if (configuration.user_position.has_value()) {
            if (const UserSpecifiedLlh* llh =
                    std::get_if<UserSpecifiedLlh>(&*configuration.user_position)) {
                enu_coordinate_kind_ = 0;
                enu_values_[0] = llh->latitude_deg;
                enu_values_[1] = llh->longitude_deg;
                enu_values_[2] = llh->ellipsoidal_height_m;
            } else if (const Ecef* ecef = std::get_if<Ecef>(&*configuration.user_position)) {
                enu_coordinate_kind_ = 1;
                enu_values_[0] = ecef->x_m;
                enu_values_[1] = ecef->y_m;
                enu_values_[2] = ecef->z_m;
            }
        } else {
            enu_coordinate_kind_ = 0;
            enu_values_[0] = 0.0;
            enu_values_[1] = 0.0;
            enu_values_[2] = 0.0;
        }
        enu_dialog_initialized_ = true;
    }
    ImGui::Combo("Coordinates", &enu_coordinate_kind_, "LLH\0ECEF\0");
    ImGui::InputDouble(
        enu_coordinate_kind_ == 0 ? "Latitude" : "X", &enu_values_[0], 0.0, 0.0, "%.10g");
    ImGui::InputDouble(
        enu_coordinate_kind_ == 0 ? "Longitude" : "Y", &enu_values_[1], 0.0, 0.0, "%.10g");
    ImGui::InputDouble(
        enu_coordinate_kind_ == 0 ? "Height" : "Z", &enu_values_[2], 0.0, 0.0, "%.10g");
    if (ImGui::Button("Apply")) {
        EnuReferenceConfiguration configuration = detail::enu_configuration_for_method(
            state_.enu_configuration(), EnuReferenceMethod::UserSpecified);
        configuration.user_position = enu_coordinate_kind_ == 0
            ? UserSpecifiedEnuPosition{UserSpecifiedLlh{
                  enu_values_[0], enu_values_[1], enu_values_[2]}}
            : UserSpecifiedEnuPosition{Ecef{enu_values_[0], enu_values_[1], enu_values_[2]}};
        if (state_.set_enu_reference_configuration(configuration)) {
            mark_plot_data_changed(false);
            ImGui::CloseCurrentPopup();
            enu_dialog_initialized_ = false;
            enu_user_specified_pending_ = false;
        } else {
            notify(NotificationLevel::Warning, "ENU reference is unavailable");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        enu_dialog_initialized_ = false;
        enu_user_specified_pending_ = false;
    }
    ImGui::EndPopup();
}

void LightGui::render_match_dialog()
{
    if (match_dialog_open_requested_) {
        ImGui::OpenPopup("Reference matching");
        match_dialog_open_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Reference matching", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (consume_escape_cancel()) {
        ImGui::CloseCurrentPopup();
        match_dialog_initialized_ = false;
        ImGui::EndPopup();
        return;
    }
    if (!match_dialog_initialized_) {
        const ReferenceMatchConfiguration configuration = state_.match_configuration();
        match_tolerance_enabled_ = configuration.tolerance_check_enabled;
        match_tolerance_seconds_ =
            static_cast<double>(configuration.maximum_time_difference_ns) / nanoseconds_per_second;
        match_dialog_initialized_ = true;
    }
    ImGui::Checkbox("Maximum age enabled", &match_tolerance_enabled_);
    ImGui::BeginDisabled(!match_tolerance_enabled_);
    ImGui::InputDouble("Maximum age (s)", &match_tolerance_seconds_, 0.001, 0.1, "%.9g");
    ImGui::EndDisabled();
    if (ImGui::Button("OK")) {
        const std::optional<GpsTime> tolerance = seconds_to_gps_time(match_tolerance_seconds_);
        if (tolerance.has_value() && tolerance->nanoseconds_since_gps_epoch >= 0
            && state_.set_reference_match_configuration(ReferenceMatchConfiguration{
                match_tolerance_enabled_, tolerance->nanoseconds_since_gps_epoch})) {
            mark_plot_data_changed(false);
            ImGui::CloseCurrentPopup();
            match_dialog_initialized_ = false;
        } else {
            notify(NotificationLevel::Warning, "Invalid reference matching tolerance");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        match_dialog_initialized_ = false;
    }
    ImGui::EndPopup();
}

void LightGui::render_plot_range_dialog()
{
    if (plot_range_dialog_open_requested_) {
        ImGui::OpenPopup("Plot ranges");
        plot_range_dialog_open_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Plot ranges", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const bool reference = view_mode_ == ViewMode::ReferenceTrajectory
        || view_mode_ == ViewMode::ReferenceTimeSeries || view_mode_ == ViewMode::ReferenceBoth;
    ImPlotComponent& component = reference ? relative_plot_ : normal_plot_;
    if (!plot_range_dialog_initialized_) {
        if (component.trajectory_metrics().has_value()) {
            const TrajectoryPlotMetrics& metrics = *component.trajectory_metrics();
            trajectory_range_values_[0] =
                displayed_position(metrics.east.minimum, plot_position_unit_index_);
            trajectory_range_values_[1] =
                displayed_position(metrics.east.maximum, plot_position_unit_index_);
            trajectory_range_values_[2] =
                displayed_position(metrics.north.minimum, plot_position_unit_index_);
            trajectory_range_values_[3] =
                displayed_position(metrics.north.maximum, plot_position_unit_index_);
            trajectory_scale_value_ =
                displayed_position(metrics.meters_per_pixel, plot_scale_unit_index_);
        }
        if (const std::optional<TimeRange> time = component.time_series_time_range()) {
            static_cast<void>(
                detail::initialize_absolute_gps_time_edit(plot_time_edits_[0], time->start));
            static_cast<void>(
                detail::initialize_absolute_gps_time_edit(plot_time_edits_[1], time->end));
        } else {
            static_cast<void>(
                detail::initialize_absolute_gps_time_edit(plot_time_edits_[0], GpsTime{0}));
            static_cast<void>(
                detail::initialize_absolute_gps_time_edit(plot_time_edits_[1], GpsTime{0}));
        }
        plot_position_present_.fill(false);
        for (const TimeSeriesPanelMetrics& metrics : component.time_series_metrics()) {
            const std::size_t index = static_cast<std::size_t>(metrics.component);
            if (index < plot_position_present_.size()) {
                plot_position_present_[index] = true;
                plot_position_minimum_[index] =
                    displayed_position(metrics.position.minimum, plot_position_unit_index_);
                plot_position_maximum_[index] =
                    displayed_position(metrics.position.maximum, plot_position_unit_index_);
            }
        }
        for (std::size_t index = 0; index < 4; ++index) {
            trajectory_range_backup_[index] = trajectory_range_values_[index];
        }
        trajectory_scale_backup_ = trajectory_scale_value_;
        plot_time_backups_ = plot_time_edits_;
        plot_position_minimum_backup_ = plot_position_minimum_;
        plot_position_maximum_backup_ = plot_position_maximum_;
        plot_range_copy_source_ = -1;
        plot_range_copy_targets_.fill(false);
        plot_range_dialog_initialized_ = true;
    }

    ImGui::SeparatorText("Trajectory");
    int range_priority = static_cast<int>(plot_options_.trajectory_range_priority);
    constexpr const char* range_priority_labels[] = {"Axis range", "Display scale"};
    if (ImGui::Combo("Range priority", &range_priority, range_priority_labels,
            std::size(range_priority_labels))) {
        plot_options_.trajectory_range_priority =
            static_cast<TrajectoryRangePriority>(range_priority);
        ++plot_settings_revision_;
    }
    int scale_fixed_target = static_cast<int>(plot_options_.trajectory_scale_fixed_target);
    constexpr const char* scale_fixed_target_labels[] = {"Drawing area", "Axis range"};
    if (ImGui::Combo("Scale fixed target", &scale_fixed_target, scale_fixed_target_labels,
            std::size(scale_fixed_target_labels))) {
        plot_options_.trajectory_scale_fixed_target =
            static_cast<TrajectoryScaleFixedTarget>(scale_fixed_target);
        ++plot_settings_revision_;
    }
    ImGui::TextUnformatted("Position unit");
    ImGui::SameLine();
    if (ImGui::SmallButton(
            position_unit_labels[static_cast<std::size_t>(plot_position_unit_index_)])) {
        const int next =
            (plot_position_unit_index_ + 1) % static_cast<int>(position_unit_scale_m.size());
        rescale_displayed_positions(plot_position_unit_index_, next, trajectory_range_values_);
        rescale_displayed_positions(plot_position_unit_index_, next, trajectory_range_backup_);
        rescale_displayed_positions(plot_position_unit_index_, next, plot_position_minimum_);
        rescale_displayed_positions(plot_position_unit_index_, next, plot_position_maximum_);
        rescale_displayed_positions(plot_position_unit_index_, next, plot_position_minimum_backup_);
        rescale_displayed_positions(plot_position_unit_index_, next, plot_position_maximum_backup_);
        plot_position_unit_index_ = next;
    }
    const bool east_valid =
        valid_numeric_range(trajectory_range_values_[0], trajectory_range_values_[1]);
    const bool north_valid =
        valid_numeric_range(trajectory_range_values_[2], trajectory_range_values_[3]);
    const NumericInputResult east_minimum =
        input_range_value("East minimum", &trajectory_range_values_[0], east_valid, "%.10g");
    const NumericInputResult east_maximum =
        input_range_value("East maximum", &trajectory_range_values_[1], east_valid, "%.10g");
    const NumericInputResult north_minimum =
        input_range_value("North minimum", &trajectory_range_values_[2], north_valid, "%.10g");
    const NumericInputResult north_maximum =
        input_range_value("North maximum", &trajectory_range_values_[3], north_valid, "%.10g");
    if (!valid_numeric_range(trajectory_range_values_[0], trajectory_range_values_[1])
        && (east_minimum.deactivated_after_edit || east_maximum.deactivated_after_edit)) {
        trajectory_range_values_[0] = trajectory_range_backup_[0];
        trajectory_range_values_[1] = trajectory_range_backup_[1];
    }
    if (!valid_numeric_range(trajectory_range_values_[2], trajectory_range_values_[3])
        && (north_minimum.deactivated_after_edit || north_maximum.deactivated_after_edit)) {
        trajectory_range_values_[2] = trajectory_range_backup_[2];
        trajectory_range_values_[3] = trajectory_range_backup_[3];
    }
    const bool entered_east_valid =
        valid_numeric_range(trajectory_range_values_[0], trajectory_range_values_[1]);
    const bool entered_north_valid =
        valid_numeric_range(trajectory_range_values_[2], trajectory_range_values_[3]);
    ImGui::BeginDisabled(!entered_east_valid);
    if (ImGui::Button("Apply East range") || east_minimum.entered || east_maximum.entered) {
        if (component.apply_trajectory_axis_range(TrajectoryAxis::East,
                NumericRange{
                    position_in_meters(trajectory_range_values_[0], plot_position_unit_index_),
                    position_in_meters(trajectory_range_values_[1], plot_position_unit_index_)})) {
            trajectory_range_backup_[0] = trajectory_range_values_[0];
            trajectory_range_backup_[1] = trajectory_range_values_[1];
        } else {
            notify(NotificationLevel::Warning, "Invalid East trajectory range");
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!entered_north_valid);
    if (ImGui::Button("Apply North range") || north_minimum.entered || north_maximum.entered) {
        if (component.apply_trajectory_axis_range(TrajectoryAxis::North,
                NumericRange{
                    position_in_meters(trajectory_range_values_[2], plot_position_unit_index_),
                    position_in_meters(trajectory_range_values_[3], plot_position_unit_index_)})) {
            trajectory_range_backup_[2] = trajectory_range_values_[2];
            trajectory_range_backup_[3] = trajectory_range_values_[3];
        } else {
            notify(NotificationLevel::Warning, "Invalid North trajectory range");
        }
    }
    ImGui::EndDisabled();
    bool scale_valid = std::isfinite(trajectory_scale_value_) && trajectory_scale_value_ > 0.0;
    const NumericInputResult scale_input = input_range_value(
        "Scale##trajectory-scale", &trajectory_scale_value_, scale_valid, "%.10g");
    scale_valid = std::isfinite(trajectory_scale_value_) && trajectory_scale_value_ > 0.0;
    if (!scale_valid && scale_input.deactivated_after_edit) {
        trajectory_scale_value_ = trajectory_scale_backup_;
        scale_valid = true;
    }
    ImGui::SameLine();
    std::string scale_unit = position_unit_labels[static_cast<std::size_t>(plot_scale_unit_index_)];
    scale_unit += "/px##scale-unit";
    if (ImGui::SmallButton(scale_unit.c_str())) {
        const int next =
            (plot_scale_unit_index_ + 1) % static_cast<int>(position_unit_scale_m.size());
        rescale_displayed_positions(
            plot_scale_unit_index_, next, std::span<double>{&trajectory_scale_value_, 1});
        rescale_displayed_positions(
            plot_scale_unit_index_, next, std::span<double>{&trajectory_scale_backup_, 1});
        plot_scale_unit_index_ = next;
    }
    ImGui::BeginDisabled(!scale_valid);
    if (ImGui::Button("Apply scale") || scale_input.entered) {
        if (component.apply_trajectory_meters_per_pixel(
                position_in_meters(trajectory_scale_value_, plot_scale_unit_index_))) {
            trajectory_scale_backup_ = trajectory_scale_value_;
        } else {
            notify(NotificationLevel::Warning, "Invalid trajectory scale");
        }
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Time series");
    std::optional<GpsTime> start =
        detail::resolve_absolute_gps_time_edit(plot_time_edits_[0]);
    std::optional<GpsTime> end = detail::resolve_absolute_gps_time_edit(plot_time_edits_[1]);
    const bool time_valid = start.has_value() && end.has_value() && *start <= *end;
    ImGui::SetNextItemWidth(210.0F);
    const NumericInputResult time_start =
        input_absolute_gps_time("GPST start", plot_time_edits_[0], time_valid);
    ImGui::SetNextItemWidth(210.0F);
    const NumericInputResult time_end =
        input_absolute_gps_time("GPST end", plot_time_edits_[1], time_valid);
    start = detail::resolve_absolute_gps_time_edit(plot_time_edits_[0]);
    end = detail::resolve_absolute_gps_time_edit(plot_time_edits_[1]);
    bool entered_time_valid = start.has_value() && end.has_value() && *start <= *end;
    if (!entered_time_valid
        && (time_start.deactivated_after_edit || time_end.deactivated_after_edit)) {
        plot_time_edits_ = plot_time_backups_;
        start = detail::resolve_absolute_gps_time_edit(plot_time_edits_[0]);
        end = detail::resolve_absolute_gps_time_edit(plot_time_edits_[1]);
        entered_time_valid = true;
    }
    const bool time_entered = time_start.entered || time_end.entered;
    ImGui::BeginDisabled(!entered_time_valid);
    const bool apply_time = ImGui::Button("Apply time range") || time_entered;
    if (entered_time_valid && apply_time) {
        if (component.set_time_series_time_range(TimeRange{*start, *end})) {
            static_cast<void>(
                detail::initialize_absolute_gps_time_edit(plot_time_edits_[0], *start));
            static_cast<void>(
                detail::initialize_absolute_gps_time_edit(plot_time_edits_[1], *end));
            plot_time_backups_ = plot_time_edits_;
        } else {
            notify(NotificationLevel::Warning, "Invalid time-series time range");
        }
    }
    ImGui::EndDisabled();
    constexpr std::array labels{"East", "North", "Up", "Height", "Distance"};
    for (std::size_t index = 0; index < plot_position_present_.size(); ++index) {
        if (!plot_position_present_[index]) {
            continue;
        }
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextUnformatted(labels[index]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0F);
        const bool position_valid =
            valid_numeric_range(plot_position_minimum_[index], plot_position_maximum_[index]);
        const NumericInputResult position_minimum =
            input_range_value("##minimum", &plot_position_minimum_[index], position_valid, "%.10g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0F);
        const NumericInputResult position_maximum =
            input_range_value("##maximum", &plot_position_maximum_[index], position_valid, "%.10g");
        ImGui::SameLine();
        bool entered_position_valid =
            valid_numeric_range(plot_position_minimum_[index], plot_position_maximum_[index]);
        if (!entered_position_valid
            && (position_minimum.deactivated_after_edit
                || position_maximum.deactivated_after_edit)) {
            plot_position_minimum_[index] = plot_position_minimum_backup_[index];
            plot_position_maximum_[index] = plot_position_maximum_backup_[index];
            entered_position_valid = true;
        }
        const bool position_entered = position_minimum.entered || position_maximum.entered;
        ImGui::BeginDisabled(!entered_position_valid);
        if (ImGui::Button("Apply") || position_entered) {
            if (component.set_time_series_position_range(static_cast<PositionComponent>(index),
                    NumericRange{position_in_meters(
                                     plot_position_minimum_[index], plot_position_unit_index_),
                        position_in_meters(
                            plot_position_maximum_[index], plot_position_unit_index_)})) {
                plot_position_minimum_backup_[index] = plot_position_minimum_[index];
                plot_position_maximum_backup_[index] = plot_position_maximum_[index];
            } else {
                notify(NotificationLevel::Warning, "Invalid time-series position range");
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Copy...")) {
            plot_range_copy_source_ = static_cast<int>(index);
            plot_range_copy_targets_.fill(false);
        }
        ImGui::PopID();
    }
    if (plot_range_copy_source_ >= 0) {
        const std::size_t source = static_cast<std::size_t>(plot_range_copy_source_);
        ImGui::SeparatorText("One-shot vertical range copy");
        ImGui::Text("Copy %s range to:", labels[source]);
        bool any_target = false;
        for (std::size_t index = 0; index < plot_position_present_.size(); ++index) {
            if (!plot_position_present_[index] || index == source) {
                continue;
            }
            ImGui::PushID(static_cast<int>(index));
            ImGui::Checkbox(labels[index], &plot_range_copy_targets_[index]);
            ImGui::PopID();
            any_target = any_target || plot_range_copy_targets_[index];
            ImGui::SameLine();
        }
        ImGui::NewLine();
        const NumericRange source_display_range{
            plot_position_minimum_[source], plot_position_maximum_[source]};
        const bool source_valid =
            valid_numeric_range(source_display_range.minimum, source_display_range.maximum);
        const NumericRange source_range{
            position_in_meters(source_display_range.minimum, plot_position_unit_index_),
            position_in_meters(source_display_range.maximum, plot_position_unit_index_)};
        ImGui::BeginDisabled(!source_valid || !any_target);
        if (ImGui::Button("Apply to selected")) {
            bool applied = true;
            for (std::size_t index = 0; index < plot_position_present_.size(); ++index) {
                if (!plot_range_copy_targets_[index]) {
                    continue;
                }
                applied = component.set_time_series_position_range(
                              static_cast<PositionComponent>(index), source_range)
                    && applied;
                plot_position_minimum_[index] = source_display_range.minimum;
                plot_position_maximum_[index] = source_display_range.maximum;
                plot_position_minimum_backup_[index] = source_display_range.minimum;
                plot_position_maximum_backup_[index] = source_display_range.maximum;
            }
            if (!applied) {
                notify(NotificationLevel::Warning, "Could not copy the time-series position range");
            }
            plot_range_copy_source_ = -1;
            plot_range_copy_targets_.fill(false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel copy")) {
            plot_range_copy_source_ = -1;
            plot_range_copy_targets_.fill(false);
        }
    }
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
        plot_range_dialog_initialized_ = false;
    }
    ImGui::EndPopup();
}

void LightGui::render(SDL_Window* window)
{
    escape_cancel_consumed_ = false;
    drain_dialog_paths();
    process_load_queue();
    prepare_plots_if_needed();
    if (window != nullptr) {
        static_cast<void>(synchronize_window_maximum(window));
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("rtktrace light", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    render_toolbar(window);
    ImGui::Separator();

    const bool rail_hovered = render_slot_rail();
    const bool sidebar_hovered = render_expanded_sidebar();
    ImGui::SameLine(0.0F, 4.0F);
    ImGui::BeginGroup();
    render_view_tabs();
    render_plot_content();
    apply_window_resize_request(window);
    render_summary();
    ImGui::EndGroup();

    if (sidebar_expanded_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !rail_hovered
        && !sidebar_hovered) {
        sidebar_expanded_ = false;
    }

    render_file_workflow_modals();
    render_options_dialog();
    render_time_range_dialog();
    render_enu_dialog();
    render_match_dialog();
    render_plot_range_dialog();
    ImGui::End();
    render_notification_window();
}

} // namespace rtktrace
