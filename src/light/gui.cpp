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

namespace plotcore {
namespace {

constexpr std::uintmax_t large_file_threshold_bytes = 100ULL * 1024ULL * 1024ULL;
constexpr double nanoseconds_per_second = 1'000'000'000.0;
constexpr std::array position_unit_labels{"km", "m", "mm"};
constexpr std::array position_unit_scale_m{1000.0, 1.0, 0.001};

[[nodiscard]] const char* severity_name(DiagnosticSeverity severity) noexcept
{
    switch (severity) {
    case DiagnosticSeverity::Info:
        return "Info";
    case DiagnosticSeverity::Warning:
        return "Warning";
    case DiagnosticSeverity::RequiresDecision:
        return "Decision";
    case DiagnosticSeverity::Fatal:
        return "Fatal";
    }
    return "Unknown";
}

[[nodiscard]] ImVec4 rgba(Rgba8 value) noexcept
{
    constexpr float scale = 1.0F / 255.0F;
    return ImVec4{value.red * scale, value.green * scale, value.blue * scale,
        value.alpha * scale};
}

[[nodiscard]] std::string slot_label(std::size_t slot, bool reference)
{
    return reference ? "R" + std::to_string(slot) : std::to_string(slot);
}

[[nodiscard]] NmeaDate file_date(const std::filesystem::path& path)
{
    using namespace std::chrono;
    std::error_code error;
    const std::filesystem::file_time_type modified =
        std::filesystem::last_write_time(path, error);
    system_clock::time_point system_time = system_clock::now();
    if (!error) {
        system_time = time_point_cast<system_clock::duration>(
            modified - std::filesystem::file_time_type::clock::now()
            + system_clock::now());
    }
    const year_month_day date{floor<days>(system_time)};
    return NmeaDate{static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day())};
}

[[nodiscard]] std::optional<GpsTime> seconds_to_gps_time(double seconds) noexcept
{
    const long double nanoseconds =
        static_cast<long double>(seconds) * nanoseconds_per_second;
    if (!std::isfinite(seconds)
        || nanoseconds < static_cast<long double>(std::numeric_limits<std::int64_t>::min())
        || nanoseconds > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return GpsTime{static_cast<std::int64_t>(std::llround(nanoseconds))};
}

[[nodiscard]] double gps_seconds(GpsTime time) noexcept
{
    return static_cast<double>(time.nanoseconds_since_gps_epoch) / nanoseconds_per_second;
}

[[nodiscard]] bool valid_numeric_range(double minimum, double maximum) noexcept
{
    return std::isfinite(minimum) && std::isfinite(maximum) && minimum < maximum;
}

[[nodiscard]] bool input_range_value(
    const char* label, double* value, bool range_is_valid, const char* format)
{
    if (!range_is_valid) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.45F, 0.08F, 0.08F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
            ImVec4{0.58F, 0.10F, 0.10F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
            ImVec4{0.68F, 0.12F, 0.12F, 1.0F});
    }
    const bool entered = ImGui::InputDouble(label, value, 0.0, 0.0, format,
        ImGuiInputTextFlags_EnterReturnsTrue);
    if (!range_is_valid) {
        ImGui::PopStyleColor(3);
    }
    return entered;
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
    const double next_scale =
        position_unit_scale_m[static_cast<std::size_t>(next_unit_index)];
    for (double& value : displayed_values) {
        value *= previous_scale / next_scale;
    }
}

} // namespace

void LightGui::enqueue_file(std::filesystem::path path)
{
    if (!path.empty()) {
        PendingLoad load;
        load.path = std::move(path);
        load_queue_.push_back(std::move(load));
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

void LightGui::file_dialog_callback(
    void* userdata, const char* const* filelist, int)
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
            status_message_ = "File dialog failed: " + dialog_error_;
            dialog_error_.clear();
        }
    }
    for (std::filesystem::path& path : paths) {
        enqueue_file(std::move(path));
    }
}

void LightGui::process_load_queue()
{
    if (modal_load_.has_value() || nmea_decision_.has_value()
        || load_queue_.empty()) {
        return;
    }
    PendingLoad load = std::move(load_queue_.front());
    load_queue_.pop_front();
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(load.path, error);
    if (!load.size_confirmed && !error && size >= large_file_threshold_bytes) {
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
        options.fallback_date = NmeaDate{load.date[0],
            static_cast<unsigned>(load.date[1]),
            static_cast<unsigned>(load.date[2])};
    }

    const bool first_file = state_.files().empty();
    FileLoadResult result = state_.load_file(load.path, load.format, options);
    switch (result.status) {
    case FileLoadStatus::Loaded: {
        const std::size_t warnings = static_cast<std::size_t>(std::count_if(
            result.diagnostics.begin(), result.diagnostics.end(),
            [](const Diagnostic& diagnostic) {
                return diagnostic.severity == DiagnosticSeverity::Warning;
            }));
        status_message_ = "Loaded " + load.path.filename().string();
        if (warnings != 0) {
            status_message_ += " with " + std::to_string(warnings) + " warning(s)";
        }
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
            load.needs_missing_geoid_decision =
                load.needs_missing_geoid_decision
                || diagnostic.code == DiagnosticCode::MissingGeoidSeparation;
            load.needs_date = load.needs_date
                || diagnostic.code == DiagnosticCode::MissingDate;
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
        status_message_ = "Rejected " + load.path.filename().string();
        diagnostics_open_ = true;
        break;
    case FileLoadStatus::IoError:
        status_message_ = "Could not open " + load.path.string();
        diagnostics_open_ = true;
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
    if (ImGui::Button("Time Range")) {
        time_dialog_open_requested_ = true;
        time_dialog_initialized_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("ENU Reference")) {
        enu_dialog_open_requested_ = true;
        enu_dialog_initialized_ = false;
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
    ImGui::SetNextItemWidth(105.0F);
    int vertical = plot_options_.vertical_component == PositionComponent::Up ? 0 : 1;
    if (ImGui::Combo("##vertical", &vertical, "Up\0Height\0")) {
        plot_options_.vertical_component = vertical == 0
            ? PositionComponent::Up
            : PositionComponent::EllipsoidalHeight;
        ++plot_settings_revision_;
        normal_plot_.request_time_series_fit();
        relative_plot_.request_time_series_fit();
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
    if (ImGui::SliderFloat("##marker-size", &plot_options_.marker_size_px,
            1.0F, 10.0F, "%.0f px")) {
        ++plot_settings_revision_;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Bridge", &plot_options_.batch.bridge_hidden_quality_samples)) {
        ++plot_settings_revision_;
    }

    for (std::size_t quality = 0; quality < solution_quality_count; ++quality) {
        ImGui::SameLine();
        const ImVec4 quality_color = rgba(rtkplot_file1_quality_colors[quality]);
        ImGui::PushStyleColor(ImGuiCol_Button,
            quality_filter_.visible[quality] ? quality_color
                                             : ImVec4{0.2F, 0.2F, 0.2F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, quality_color);
        const std::string label = "Q" + std::to_string(quality);
        if (ImGui::SmallButton(label.c_str())) {
            quality_filter_.visible[quality] = !quality_filter_.visible[quality];
            ++plot_settings_revision_;
        }
        ImGui::PopStyleColor(2);
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
            ImGui::SetDragDropPayload("PLOTCORE_SLOT", &slot, sizeof(slot));
            ImGui::Text("Move slot %zu", slot);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("PLOTCORE_SLOT")) {
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
        ImGui::Text("%zu  %s", index + 1,
            file.source_path.filename().string().c_str());
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
    if (ImGui::Button("Diagnostics")) {
        diagnostics_open_ = true;
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

void LightGui::render_both(ImPlotComponent& component, std::string_view id_prefix)
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    constexpr float splitter_width = 6.0F;
    const float left_width = std::max(100.0F,
        (available.x - splitter_width) * both_fraction_);
    const std::string left_id = std::string{id_prefix} + " trajectory pane";
    ImGui::BeginChild(left_id.c_str(), ImVec2{left_width, available.y}, false);
    const std::string trajectory_id = std::string{id_prefix} + " trajectory";
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
                + ImGui::GetIO().MouseDelta.x
                    / std::max(available.x - splitter_width, 1.0F),
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
}

void LightGui::render_plot_content()
{
    ImGui::BeginChild("Plot content", ImVec2{0.0F, -112.0F}, true);
    const bool reference = view_mode_ == ViewMode::ReferenceTrajectory
        || view_mode_ == ViewMode::ReferenceTimeSeries
        || view_mode_ == ViewMode::ReferenceBoth;
    ImPlotComponent& selected_component = reference ? relative_plot_ : normal_plot_;
    if (selected_component.empty()) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2{std::max(12.0F, available.x * 0.5F - 95.0F),
            std::max(12.0F, available.y * 0.5F)});
        ImGui::TextDisabled(reference && state_.files().size() < 2
                ? "Reference view needs two files"
                : "Open POS or NMEA files");
        ImGui::EndChild();
        return;
    }
    switch (view_mode_) {
    case ViewMode::NormalTrajectory:
        normal_plot_.render_trajectory("Normal trajectory", PlotAreaSize{-1.0, -1.0});
        break;
    case ViewMode::NormalTimeSeries:
        normal_plot_.render_time_series("Normal time series", PlotAreaSize{-1.0, -1.0});
        break;
    case ViewMode::NormalBoth:
        render_both(normal_plot_, "Normal");
        break;
    case ViewMode::ReferenceTrajectory:
        relative_plot_.render_trajectory("Reference trajectory", PlotAreaSize{-1.0, -1.0});
        break;
    case ViewMode::ReferenceTimeSeries:
        relative_plot_.render_time_series("Reference time series", PlotAreaSize{-1.0, -1.0});
        break;
    case ViewMode::ReferenceBoth:
        render_both(relative_plot_, "Reference");
        break;
    }
    ImGui::EndChild();
}

void LightGui::render_summary()
{
    ImGui::BeginChild("Data summary", ImVec2{0.0F, 108.0F}, true,
        ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::SmallButton(
            statistics_mode_ == StatisticsMode::Recorded ? "Recorded" : "Expected")) {
        statistics_mode_ = statistics_mode_ == StatisticsMode::Recorded
            ? StatisticsMode::Expected
            : StatisticsMode::Recorded;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", status_message_.c_str());
    const std::vector<RecordedStatistics> statistics = state_.recorded_statistics();
    const std::optional<TimeRange> range = state_.effective_range();
    for (std::size_t index = 0; index < statistics.size(); ++index) {
        const RecordedStatistics& item = statistics[index];
        const std::size_t recorded = recorded_sample_count(item);
        const std::optional<std::size_t> expected = range.has_value()
            ? calculate_expected_sample_count(
                *range, state_.files()[index].effective_hz())
            : std::nullopt;
        const std::size_t denominator = statistics_mode_ == StatisticsMode::Recorded
            ? recorded
            : expected.value_or(0);
        ImGui::Text("%zu %s  %.3f .. %.3f GPST  N=%zu",
            index + 1, state_.files()[index].source_path.filename().string().c_str(),
            item.first_sample_time.has_value() ? gps_seconds(*item.first_sample_time) : 0.0,
            item.last_sample_time.has_value() ? gps_seconds(*item.last_sample_time) : 0.0,
            denominator);
        for (std::size_t quality = 0; quality < solution_quality_count; ++quality) {
            ImGui::SameLine();
            const std::optional<double> percentage =
                quality_percentage(item.quality_counts[quality], denominator);
            if (percentage.has_value()) {
                ImGui::TextColored(rgba(rtkplot_file1_quality_colors[quality]),
                    "Q%zu:%zu/%.1f%%", quality, item.quality_counts[quality], *percentage);
            } else {
                ImGui::TextDisabled("Q%zu:%zu/-", quality, item.quality_counts[quality]);
            }
        }
    }
    ImGui::EndChild();
}

void LightGui::render_diagnostics_window()
{
    if (!diagnostics_open_) {
        return;
    }
    if (ImGui::Begin("Diagnostic history", &diagnostics_open_)) {
        for (const Diagnostic& diagnostic : state_.diagnostic_history()) {
            ImGui::Text("[%s] %s", severity_name(diagnostic.severity),
                diagnostic.file_name.c_str());
            ImGui::SameLine();
            if (diagnostic.source_line_number.has_value()) {
                ImGui::Text("line %zu:", *diagnostic.source_line_number);
                ImGui::SameLine();
            }
            ImGui::TextWrapped("%s", diagnostic.message.c_str());
        }
        if (state_.diagnostic_history().empty()) {
            ImGui::TextDisabled("No diagnostics recorded.");
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
    if (ImGui::BeginPopupModal("Large input file", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
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
            modal_load_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (format_popup_requested_) {
        ImGui::OpenPopup("Choose input format");
        format_popup_requested_ = false;
    }
    if (ImGui::BeginPopupModal("Choose input format", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
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
            modal_load_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (nmea_popup_requested_) {
        ImGui::OpenPopup("NMEA input decision");
        nmea_popup_requested_ = false;
    }
    if (ImGui::BeginPopupModal("NMEA input decision", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        PendingLoad& load = *nmea_decision_;
        ImGui::TextUnformatted(load.path.filename().string().c_str());
        if (load.needs_missing_geoid_decision) {
            ImGui::Checkbox("Use GGA altitude as ellipsoidal height",
                &load.use_altitude_as_height);
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
        const bool can_continue = !load.needs_missing_geoid_decision
            || load.use_altitude_as_height;
        ImGui::BeginDisabled(!can_continue);
        if (ImGui::Button("Continue")) {
            PendingLoad accepted = std::move(load);
            nmea_decision_.reset();
            ImGui::CloseCurrentPopup();
            attempt_load(std::move(accepted));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            nmea_decision_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void LightGui::render_time_range_dialog()
{
    if (time_dialog_open_requested_) {
        ImGui::OpenPopup("Common time range");
        time_dialog_open_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Common time range", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (!time_dialog_initialized_) {
        const CommonTimeRange& configured = state_.configured_time_range();
        const std::optional<TimeRange> effective = state_.effective_range();
        time_start_enabled_ = configured.start_enabled;
        time_end_enabled_ = configured.end_enabled;
        time_start_seconds_ = configured.entered_start.has_value()
            ? gps_seconds(*configured.entered_start)
            : effective.has_value() ? gps_seconds(effective->start) : 0.0;
        time_end_seconds_ = configured.entered_end.has_value()
            ? gps_seconds(*configured.entered_end)
            : effective.has_value() ? gps_seconds(effective->end) : 0.0;
        time_dialog_initialized_ = true;
    }
    ImGui::Checkbox("Start", &time_start_enabled_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!time_start_enabled_);
    ImGui::InputDouble("##start-gpst", &time_start_seconds_, 1.0, 60.0, "%.9f GPST s");
    ImGui::EndDisabled();
    ImGui::Checkbox("End", &time_end_enabled_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!time_end_enabled_);
    ImGui::InputDouble("##end-gpst", &time_end_seconds_, 1.0, 60.0, "%.9f GPST s");
    ImGui::EndDisabled();
    if (ImGui::Button("Use intersection")) {
        const std::optional<TimeRange> intersection = intersection_time_range(state_.files());
        if (intersection.has_value()) {
            time_start_seconds_ = gps_seconds(intersection->start);
            time_end_seconds_ = gps_seconds(intersection->end);
            time_start_enabled_ = time_end_enabled_ = true;
        } else {
            status_message_ = "No common intersection";
        }
    }
    if (ImGui::Button("OK")) {
        CommonTimeRange range;
        range.start_enabled = time_start_enabled_;
        range.end_enabled = time_end_enabled_;
        range.entered_start = seconds_to_gps_time(time_start_seconds_);
        range.entered_end = seconds_to_gps_time(time_end_seconds_);
        if (state_.set_common_time_range(range)) {
            mark_plot_data_changed(false);
            ImGui::CloseCurrentPopup();
            time_dialog_initialized_ = false;
        } else {
            status_message_ = "Invalid common time range";
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
        ImGui::OpenPopup("ENU reference");
        enu_dialog_open_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("ENU reference", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (!enu_dialog_initialized_) {
        const EnuReferenceConfiguration& configuration = state_.enu_configuration();
        enu_method_index_ = static_cast<int>(configuration.method);
        if (configuration.user_position.has_value()) {
            if (const UserSpecifiedLlh* llh =
                    std::get_if<UserSpecifiedLlh>(&*configuration.user_position)) {
                enu_coordinate_kind_ = 0;
                enu_values_[0] = llh->latitude_deg;
                enu_values_[1] = llh->longitude_deg;
                enu_values_[2] = llh->ellipsoidal_height_m;
            } else if (const Ecef* ecef =
                           std::get_if<Ecef>(&*configuration.user_position)) {
                enu_coordinate_kind_ = 1;
                enu_values_[0] = ecef->x_m;
                enu_values_[1] = ecef->y_m;
                enu_values_[2] = ecef->z_m;
            }
        }
        enu_dialog_initialized_ = true;
    }
    ImGui::Combo("Method", &enu_method_index_,
        "Slot 1 start\0Slot 1 end\0Slot 1 ECEF average\0User specified\0");
    if (enu_method_index_ == static_cast<int>(EnuReferenceMethod::UserSpecified)) {
        ImGui::Combo("Coordinates", &enu_coordinate_kind_, "LLH\0ECEF\0");
        ImGui::InputDouble(enu_coordinate_kind_ == 0 ? "Latitude" : "X",
            &enu_values_[0], 0.0, 0.0, "%.10g");
        ImGui::InputDouble(enu_coordinate_kind_ == 0 ? "Longitude" : "Y",
            &enu_values_[1], 0.0, 0.0, "%.10g");
        ImGui::InputDouble(enu_coordinate_kind_ == 0 ? "Height" : "Z",
            &enu_values_[2], 0.0, 0.0, "%.10g");
    }
    if (ImGui::Button("OK")) {
        EnuReferenceConfiguration configuration;
        configuration.method = static_cast<EnuReferenceMethod>(enu_method_index_);
        if (configuration.method == EnuReferenceMethod::UserSpecified) {
            configuration.user_position = enu_coordinate_kind_ == 0
                ? UserSpecifiedEnuPosition{UserSpecifiedLlh{
                      enu_values_[0], enu_values_[1], enu_values_[2]}}
                : UserSpecifiedEnuPosition{Ecef{
                      enu_values_[0], enu_values_[1], enu_values_[2]}};
        }
        if (state_.set_enu_reference_configuration(configuration)) {
            mark_plot_data_changed(false);
            ImGui::CloseCurrentPopup();
            enu_dialog_initialized_ = false;
        } else {
            status_message_ = "ENU reference is unavailable";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        enu_dialog_initialized_ = false;
    }
    ImGui::EndPopup();
}

void LightGui::render_match_dialog()
{
    if (match_dialog_open_requested_) {
        ImGui::OpenPopup("Reference matching");
        match_dialog_open_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Reference matching", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (!match_dialog_initialized_) {
        const ReferenceMatchConfiguration configuration = state_.match_configuration();
        match_tolerance_enabled_ = configuration.tolerance_check_enabled;
        match_tolerance_seconds_ =
            static_cast<double>(configuration.maximum_time_difference_ns)
            / nanoseconds_per_second;
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
            status_message_ = "Invalid reference matching tolerance";
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
    if (!ImGui::BeginPopupModal("Plot ranges", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const bool reference = view_mode_ == ViewMode::ReferenceTrajectory
        || view_mode_ == ViewMode::ReferenceTimeSeries
        || view_mode_ == ViewMode::ReferenceBoth;
    ImPlotComponent& component = reference ? relative_plot_ : normal_plot_;
    if (!plot_range_dialog_initialized_) {
        if (component.trajectory_metrics().has_value()) {
            const TrajectoryPlotMetrics& metrics = *component.trajectory_metrics();
            trajectory_range_values_[0] = displayed_position(
                metrics.east.minimum, plot_position_unit_index_);
            trajectory_range_values_[1] = displayed_position(
                metrics.east.maximum, plot_position_unit_index_);
            trajectory_range_values_[2] = displayed_position(
                metrics.north.minimum, plot_position_unit_index_);
            trajectory_range_values_[3] = displayed_position(
                metrics.north.maximum, plot_position_unit_index_);
            trajectory_scale_value_ = displayed_position(
                metrics.meters_per_pixel, plot_scale_unit_index_);
        }
        if (const std::optional<TimeRange> time = component.time_series_time_range()) {
            plot_time_values_[0] = gps_seconds(time->start);
            plot_time_values_[1] = gps_seconds(time->end);
        }
        plot_position_present_.fill(false);
        for (const TimeSeriesPanelMetrics& metrics : component.time_series_metrics()) {
            const std::size_t index = static_cast<std::size_t>(metrics.component);
            if (index < plot_position_present_.size()) {
                plot_position_present_[index] = true;
                plot_position_minimum_[index] = displayed_position(
                    metrics.position.minimum, plot_position_unit_index_);
                plot_position_maximum_[index] = displayed_position(
                    metrics.position.maximum, plot_position_unit_index_);
            }
        }
        plot_range_copy_source_ = -1;
        plot_range_copy_targets_.fill(false);
        plot_range_dialog_initialized_ = true;
    }

    ImGui::SeparatorText("Trajectory");
    ImGui::TextUnformatted("Position unit");
    ImGui::SameLine();
    if (ImGui::SmallButton(
            position_unit_labels[static_cast<std::size_t>(plot_position_unit_index_)])) {
        const int next = (plot_position_unit_index_ + 1)
            % static_cast<int>(position_unit_scale_m.size());
        rescale_displayed_positions(
            plot_position_unit_index_, next, trajectory_range_values_);
        rescale_displayed_positions(
            plot_position_unit_index_, next, plot_position_minimum_);
        rescale_displayed_positions(
            plot_position_unit_index_, next, plot_position_maximum_);
        plot_position_unit_index_ = next;
    }
    const bool east_valid = valid_numeric_range(
        trajectory_range_values_[0], trajectory_range_values_[1]);
    const bool north_valid = valid_numeric_range(
        trajectory_range_values_[2], trajectory_range_values_[3]);
    bool trajectory_entered = input_range_value("East minimum",
        &trajectory_range_values_[0], east_valid, "%.10g");
    trajectory_entered = input_range_value("East maximum",
        &trajectory_range_values_[1], east_valid, "%.10g") || trajectory_entered;
    trajectory_entered = input_range_value("North minimum",
        &trajectory_range_values_[2], north_valid, "%.10g") || trajectory_entered;
    trajectory_entered = input_range_value("North maximum",
        &trajectory_range_values_[3], north_valid, "%.10g") || trajectory_entered;
    const bool trajectory_valid = valid_numeric_range(
        trajectory_range_values_[0], trajectory_range_values_[1])
        && valid_numeric_range(
            trajectory_range_values_[2], trajectory_range_values_[3]);
    ImGui::BeginDisabled(!trajectory_valid);
    if (ImGui::Button("Apply trajectory ranges") || trajectory_entered) {
        if (!component.set_trajectory_ranges(
                NumericRange{
                    position_in_meters(
                        trajectory_range_values_[0], plot_position_unit_index_),
                    position_in_meters(
                        trajectory_range_values_[1], plot_position_unit_index_)},
                NumericRange{
                    position_in_meters(
                        trajectory_range_values_[2], plot_position_unit_index_),
                    position_in_meters(
                        trajectory_range_values_[3], plot_position_unit_index_)})) {
            status_message_ = "Invalid trajectory range";
        }
    }
    ImGui::EndDisabled();
    const bool scale_valid = std::isfinite(trajectory_scale_value_)
        && trajectory_scale_value_ > 0.0;
    const bool scale_entered = input_range_value("Scale##trajectory-scale",
        &trajectory_scale_value_, scale_valid, "%.10g");
    ImGui::SameLine();
    std::string scale_unit = position_unit_labels[
        static_cast<std::size_t>(plot_scale_unit_index_)];
    scale_unit += "/px##scale-unit";
    if (ImGui::SmallButton(scale_unit.c_str())) {
        const int next = (plot_scale_unit_index_ + 1)
            % static_cast<int>(position_unit_scale_m.size());
        rescale_displayed_positions(plot_scale_unit_index_, next,
            std::span<double>{&trajectory_scale_value_, 1});
        plot_scale_unit_index_ = next;
    }
    ImGui::BeginDisabled(!scale_valid);
    if ((ImGui::Button("Apply scale") || scale_entered)
        && !component.set_trajectory_meters_per_pixel(position_in_meters(
            trajectory_scale_value_, plot_scale_unit_index_))) {
        status_message_ = "Invalid trajectory scale";
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Time series");
    std::optional<GpsTime> start = seconds_to_gps_time(plot_time_values_[0]);
    std::optional<GpsTime> end = seconds_to_gps_time(plot_time_values_[1]);
    const bool time_valid = start.has_value() && end.has_value() && *start <= *end;
    bool time_entered = input_range_value("GPST start (s)",
        &plot_time_values_[0], time_valid, "%.9f");
    time_entered = input_range_value("GPST end (s)",
        &plot_time_values_[1], time_valid, "%.9f") || time_entered;
    start = seconds_to_gps_time(plot_time_values_[0]);
    end = seconds_to_gps_time(plot_time_values_[1]);
    const bool entered_time_valid = start.has_value() && end.has_value()
        && *start <= *end;
    ImGui::BeginDisabled(!entered_time_valid);
    const bool apply_time = ImGui::Button("Apply time range") || time_entered;
    if (entered_time_valid && apply_time) {
        if (!component.set_time_series_time_range(TimeRange{*start, *end})) {
            status_message_ = "Invalid time-series time range";
        }
    }
    ImGui::EndDisabled();
    constexpr std::array labels{
        "East", "North", "Up", "Height", "Distance"};
    for (std::size_t index = 0; index < plot_position_present_.size(); ++index) {
        if (!plot_position_present_[index]) {
            continue;
        }
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextUnformatted(labels[index]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0F);
        const bool position_valid = valid_numeric_range(
            plot_position_minimum_[index], plot_position_maximum_[index]);
        bool position_entered = input_range_value("##minimum",
            &plot_position_minimum_[index], position_valid, "%.10g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0F);
        position_entered = input_range_value("##maximum",
            &plot_position_maximum_[index], position_valid, "%.10g")
            || position_entered;
        ImGui::SameLine();
        const bool entered_position_valid = valid_numeric_range(
            plot_position_minimum_[index], plot_position_maximum_[index]);
        ImGui::BeginDisabled(!entered_position_valid);
        if (ImGui::Button("Apply") || position_entered) {
            if (!component.set_time_series_position_range(
                    static_cast<PositionComponent>(index),
                    NumericRange{
                        position_in_meters(
                            plot_position_minimum_[index], plot_position_unit_index_),
                        position_in_meters(
                            plot_position_maximum_[index], plot_position_unit_index_)})) {
                status_message_ = "Invalid time-series position range";
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
        const NumericRange source_display_range{plot_position_minimum_[source],
            plot_position_maximum_[source]};
        const bool source_valid = valid_numeric_range(
            source_display_range.minimum, source_display_range.maximum);
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
            }
            if (!applied) {
                status_message_ = "Could not copy the time-series position range";
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
    drain_dialog_paths();
    process_load_queue();
    prepare_plots_if_needed();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("plotcore light", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings);
    render_toolbar(window);
    ImGui::Separator();

    const bool rail_hovered = render_slot_rail();
    const bool sidebar_hovered = render_expanded_sidebar();
    ImGui::SameLine(0.0F, 4.0F);
    ImGui::BeginGroup();
    render_view_tabs();
    render_plot_content();
    render_summary();
    ImGui::EndGroup();

    if (sidebar_expanded_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !rail_hovered && !sidebar_hovered) {
        sidebar_expanded_ = false;
    }

    render_file_workflow_modals();
    render_time_range_dialog();
    render_enu_dialog();
    render_match_dialog();
    render_plot_range_dialog();
    ImGui::End();
    render_diagnostics_window();
}

} // namespace plotcore
