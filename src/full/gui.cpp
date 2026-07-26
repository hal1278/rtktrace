#include "gui.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include <SDL3/SDL_dialog.h>

#include "imgui.h"
#include "rtktrace/analysis/gps_time.hpp"

namespace rtktrace {
namespace {

constexpr double nanoseconds_per_second = 1'000'000'000.0;
constexpr std::uintmax_t large_file_threshold_bytes = 100ULL * 1024ULL * 1024ULL;
constexpr std::array modifier_choices{ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt};

[[nodiscard]] const char* full_plot_type_name(PlotType type) noexcept
{
    switch (type) {
    case PlotType::NormalTrajectory:
        return "Normal 2D";
    case PlotType::NormalTimeSeries:
        return "Normal Time Series";
    case PlotType::RelativeTrajectory:
        return "Relative 2D";
    case PlotType::RelativeTimeSeries:
        return "Relative Time Series";
    }
    return "Plot";
}

[[nodiscard]] ImVec4 rgba(Rgba8 value) noexcept
{
    constexpr float scale = 1.0F / 255.0F;
    return {value.red * scale, value.green * scale, value.blue * scale, value.alpha * scale};
}

[[nodiscard]] bool is_trajectory(PlotType type) noexcept
{
    return type == PlotType::NormalTrajectory || type == PlotType::RelativeTrajectory;
}

[[nodiscard]] std::string plot_window_label(const PlotInstanceState& plot)
{
    return plot.title + " [#" + std::to_string(plot.id.value) + "]###full-plot-"
        + std::to_string(plot.id.value);
}

void copy_text(std::span<char> target, std::string_view source)
{
    std::fill(target.begin(), target.end(), '\0');
    const std::size_t count = std::min(source.size(), target.size() - 1);
    std::copy_n(source.begin(), count, target.begin());
}

[[nodiscard]] NmeaDate file_date(const std::filesystem::path& path)
{
    using namespace std::chrono;
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    system_clock::time_point system_time = system_clock::now();
    if (!error) {
        system_time = time_point_cast<system_clock::duration>(
            modified - std::filesystem::file_time_type::clock::now() + system_clock::now());
    }
    const year_month_day date{floor<days>(system_time)};
    return NmeaDate{static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day())};
}

[[nodiscard]] bool valid_nmea_date(const int (&date)[3]) noexcept
{
    using namespace std::chrono;
    return year_month_day{
        year{date[0]}, month{static_cast<unsigned>(date[1])}, day{static_cast<unsigned>(date[2])}}
        .ok();
}

[[nodiscard]] bool write_gps_text(
    std::array<char, 24>& target, std::optional<GpsTime>& original, GpsTime value)
{
    const std::optional<std::string> text = format_absolute_gps_time(value);
    if (!text.has_value() || text->size() + 1 > target.size()) {
        return false;
    }
    copy_text(target, *text);
    original = value;
    return true;
}

[[nodiscard]] std::optional<GpsTime> read_gps_text(
    const std::array<char, 24>& text, const std::optional<GpsTime>& original, bool edited) noexcept
{
    if (!edited) {
        return original;
    }
    return parse_absolute_gps_time(std::string_view{text.data()});
}

[[nodiscard]] int modifier_index(ImGuiKeyChord modifier) noexcept
{
    for (std::size_t index = 0; index < modifier_choices.size(); ++index) {
        if (modifier_choices[index] == modifier) {
            return static_cast<int>(index);
        }
    }
    return 0;
}

[[nodiscard]] ImGuiKeyChord modifier_at(int index) noexcept
{
    return modifier_choices[static_cast<std::size_t>(std::clamp(index, 0, 2))];
}

[[nodiscard]] bool valid_range(double minimum, double maximum) noexcept
{
    return std::isfinite(minimum) && std::isfinite(maximum) && minimum < maximum;
}

[[nodiscard]] const char* component_name(PositionComponent component) noexcept
{
    switch (component) {
    case PositionComponent::East:
        return "East";
    case PositionComponent::North:
        return "North";
    case PositionComponent::Up:
        return "Up";
    case PositionComponent::EllipsoidalHeight:
        return "Ellipsoidal height";
    case PositionComponent::ReferenceRelativeDistance3d:
        return "Relative distance";
    }
    return "Position";
}

} // namespace

full_detail::InitialWindowLayout full_detail::initial_window_layout(
    float x, float y, float width, float height) noexcept
{
    constexpr float gap = 8.0F;
    const float usable_width = std::max(width, 600.0F);
    const float usable_height = std::max(height, 420.0F);
    const float left_width = std::clamp(usable_width * 0.23F, 190.0F, 300.0F);
    const float center_width = std::clamp(usable_width * 0.25F, 210.0F, 330.0F);
    const float right_x = x + left_width + center_width + 2.0F * gap;
    const float right_width = std::max(240.0F, x + usable_width - right_x);
    const float manager_height = std::max(180.0F, usable_height * 0.43F);
    return InitialWindowLayout{
        .file_slots = {x, y, left_width, usable_height},
        .window_manager = {x + left_width + gap, y, center_width, manager_height},
        .shared_controls = {x + left_width + gap, y + manager_height + gap, center_width,
            std::max(180.0F, usable_height - manager_height - gap)},
        .initial_plot = {right_x, y, right_width, usable_height},
    };
}

full_detail::WindowRect full_detail::cascaded_plot_rect(
    const WindowRect& plot_region, std::size_t cascade_index) noexcept
{
    constexpr float offset = 24.0F;
    const float minimum_width = std::min(360.0F, plot_region.width);
    const float minimum_height = std::min(280.0F, plot_region.height);
    const float available_x = std::max(0.0F, plot_region.width - minimum_width);
    const float available_y = std::max(0.0F, plot_region.height - minimum_height);
    const std::size_t steps = static_cast<std::size_t>(
        std::max(1.0F, std::floor(std::min(available_x, available_y) / offset) + 1.0F));
    const float delta = offset * static_cast<float>(cascade_index % steps);
    return WindowRect{plot_region.x + delta, plot_region.y + delta,
        std::max(minimum_width, plot_region.width - delta),
        std::max(minimum_height, plot_region.height - delta)};
}

FullGui::FullGui()
{
    shared_options_.marker_size_px = default_point_size_px_;
    static_cast<void>(create_plot(PlotType::NormalTrajectory));
}

void FullGui::enqueue_file(std::filesystem::path path)
{
    load_queue_.push_back(
        PendingLoad{.path = std::move(path), .format = std::nullopt, .size_confirmed = false});
}

bool FullGui::add_loaded_file(LoadedFile file)
{
    const std::string name = file.source_path.filename().string();
    record_diagnostics(file.diagnostics);
    if (!state_.session().add_loaded_file(std::move(file))) {
        notify(NotificationLevel::Error, "Could not add " + name);
        return false;
    }
    notify(NotificationLevel::Info, "Loaded " + name);
    return true;
}

bool FullGui::exit_requested() const noexcept
{
    return exit_requested_;
}

const FullApplicationState& FullGui::application_state() const noexcept
{
    return state_;
}

std::size_t FullGui::runtime_count() const noexcept
{
    return runtime_.runtime_count();
}

std::optional<PlotWindowId> FullGui::create_plot(PlotType type)
{
    const std::optional<PlotWindowId> id = state_.create_plot(type);
    if (!id.has_value()) {
        notify(NotificationLevel::Error, "Plot ID space is exhausted");
        return std::nullopt;
    }
    const std::string title =
        std::string{full_plot_type_name(type)} + " " + std::to_string(id->value);
    static_cast<void>(state_.set_plot_title(*id, title));
    PlotLocalState local;
    local.options = shared_options_;
    local.options.marker_size_px = default_point_size_px_;
    local.cascade_index = cascade_index_++;
    copy_text(local.title_edit, title);
    plot_local_.emplace(*id, std::move(local));
    runtime_.synchronize(state_, shared_options_, shared_options_revision_);
    return id;
}

FullGui::PlotLocalState& FullGui::local_state(PlotWindowId id)
{
    auto [position, inserted] = plot_local_.try_emplace(id);
    if (inserted) {
        position->second.options = shared_options_;
        position->second.options.marker_size_px = default_point_size_px_;
        position->second.cascade_index = cascade_index_++;
        if (const PlotInstanceState* plot = state_.find_plot(id)) {
            copy_text(position->second.title_edit, plot->title);
        }
    }
    return position->second;
}

void FullGui::erase_plot(PlotWindowId id)
{
    static_cast<void>(state_.erase_plot(id));
    plot_local_.erase(id);
    runtime_.synchronize(state_);
}

void FullGui::synchronize_local_options()
{
    runtime_.synchronize(state_);
    for (const PlotInstanceState& plot : state_.plots()) {
        PlotLocalState& local = local_state(plot.id);
        static_cast<void>(runtime_.set_current_point_size(plot.id, local.options.marker_size_px));
        static_cast<void>(runtime_.set_draw_mode(plot.id, local.options.batch.draw_mode));
        static_cast<void>(runtime_.set_time_series_components(plot.id, local.options.show_east,
            local.options.show_north, local.options.show_vertical));
        static_cast<void>(
            runtime_.set_vertical_component(plot.id, local.options.vertical_component));
    }
}

void FullGui::open_file_dialog(SDL_Window* window)
{
    static constexpr std::array filters{
        SDL_DialogFileFilter{"GNSS position", "pos;nmea;gga"},
        SDL_DialogFileFilter{"All files", "*"},
    };
    SDL_ShowOpenFileDialog(file_dialog_callback, this, window, filters.data(),
        static_cast<int>(filters.size()), nullptr, true);
}

void FullGui::file_dialog_callback(void* userdata, const char* const* filelist, int)
{
    FullGui& gui = *static_cast<FullGui*>(userdata);
    std::scoped_lock lock{gui.dialog_mutex_};
    if (filelist == nullptr) {
        gui.dialog_error_ = SDL_GetError();
        return;
    }
    for (std::size_t index = 0; filelist[index] != nullptr; ++index) {
        gui.dialog_paths_.emplace_back(filelist[index]);
    }
}

void FullGui::drain_dialog_paths()
{
    std::vector<std::filesystem::path> paths;
    {
        std::scoped_lock lock{dialog_mutex_};
        paths.swap(dialog_paths_);
        if (!dialog_error_.empty()) {
            notify(NotificationLevel::Error, "File dialog failed: " + dialog_error_);
            dialog_error_.clear();
        }
    }
    for (std::filesystem::path& path : paths) {
        enqueue_file(std::move(path));
    }
}

void FullGui::process_load_queue()
{
    if (large_file_decision_.has_value() || format_decision_.has_value()
        || nmea_decision_.has_value() || load_queue_.empty()) {
        return;
    }
    PendingLoad load = std::move(load_queue_.front());
    load_queue_.pop_front();
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(load.path, error);
    if (!load.size_confirmed && !error && size >= large_file_threshold_bytes) {
        large_file_decision_ = std::move(load);
        large_file_popup_requested_ = true;
        return;
    }
    attempt_load(std::move(load));
}

void FullGui::attempt_load(PendingLoad load)
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
    FileLoadResult result = state_.session().load_file(load.path, load.format, options);
    switch (result.status) {
    case FileLoadStatus::Loaded:
        record_diagnostics(result.diagnostics);
        notify(NotificationLevel::Info, "Loaded " + load.path.filename().string());
        break;
    case FileLoadStatus::NeedsInputFormat:
        format_decision_ = std::move(load);
        format_popup_requested_ = true;
        break;
    case FileLoadStatus::NeedsNmeaDecision: {
        load.format = InputFormat::Nmea;
        for (const Diagnostic& diagnostic : result.diagnostics) {
            load.needs_missing_geoid_decision |=
                diagnostic.code == DiagnosticCode::MissingGeoidSeparation;
            load.needs_date |= diagnostic.code == DiagnosticCode::MissingDate;
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
        notify(NotificationLevel::Error, "Rejected " + load.path.filename().string());
        break;
    case FileLoadStatus::IoError:
        record_diagnostics(result.diagnostics);
        notify(NotificationLevel::Error, "Could not open " + load.path.string());
        break;
    }
}

void FullGui::record_diagnostics(const std::vector<Diagnostic>& diagnostics)
{
    for (const Diagnostic& diagnostic : diagnostics) {
        notifications_.add(diagnostic);
    }
    caution_present_ = notifications_.has_caution();
}

void FullGui::notify(NotificationLevel level, std::string message)
{
    notifications_.add(level, std::move(message));
    caution_present_ = notifications_.has_caution();
}

bool FullGui::consume_escape_cancel()
{
    if (escape_cancel_consumed_ || !ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        return false;
    }
    escape_cancel_consumed_ = true;
    return true;
}

void FullGui::render_main_menu(SDL_Window* window)
{
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...")) {
            open_file_dialog(window);
        }
        if (ImGui::MenuItem("Exit")) {
            exit_requested_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Window Manager", nullptr, window_manager_visible_)) {
            window_manager_visible_ = !window_manager_visible_;
            static_cast<void>(state_.set_application_window_visible(
                ApplicationWindow::WindowManager, window_manager_visible_));
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void FullGui::render_file_slots(SDL_Window* window, const full_detail::InitialWindowLayout& layout)
{
    if (!file_slots_visible_) {
        return;
    }
    if (initial_layout_pending_) {
        ImGui::SetNextWindowPos({layout.file_slots.x, layout.file_slots.y});
        ImGui::SetNextWindowSize({layout.file_slots.width, layout.file_slots.height});
    }
    bool open = true;
    if (ImGui::Begin("File / Slots", &open)) {
        if (ImGui::Button("Open...")) {
            open_file_dialog(window);
        }
        ImGui::SameLine();
        if (ImGui::Button("Notifications")) {
            notifications_visible_ = true;
        }
        if (caution_present_) {
            ImGui::SameLine();
            ImGui::TextColored({1.0F, 0.72F, 0.2F, 1.0F}, "Caution");
        }

        std::optional<std::size_t> erase;
        std::optional<std::pair<std::size_t, std::size_t>> move;
        for (std::size_t index = 0; index < state_.session().files().size(); ++index) {
            const std::size_t slot = index + 1;
            const LoadedFile& file = state_.session().files()[index];
            ImGui::PushID(static_cast<int>(slot));
            bool visible = file.visible;
            if (ImGui::Checkbox("##visible", &visible)) {
                static_cast<void>(state_.session().set_file_visible(slot, visible));
            }
            ImGui::SameLine();
            ImGui::Text("%zu: %s", slot, file.source_path.filename().string().c_str());
            if (file.override_hz().has_value()) {
                ImGui::TextDisabled("%.6g Hz (manual)", *file.override_hz());
            } else if (file.estimated_hz().has_value()) {
                ImGui::TextDisabled("%.6g Hz", *file.estimated_hz());
            } else {
                ImGui::TextDisabled("rate unavailable");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Up") && slot > 1) {
                move = std::pair{slot, slot - 1};
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") && slot < state_.session().files().size()) {
                move = std::pair{slot, slot + 1};
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                erase = slot;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hz...")) {
                hz_edit_slot_ = slot;
                hz_edit_value_ = file.effective_hz().value_or(1.0);
                ImGui::OpenPopup("Rate");
            }
            if (ImGui::BeginPopup("Rate")) {
                ImGui::InputDouble("Hz", &hz_edit_value_, 0.1, 1.0, "%.6g");
                if (ImGui::Button("Apply override")) {
                    static_cast<void>(
                        state_.session().set_file_override_hz(hz_edit_slot_, hz_edit_value_));
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Use estimated")) {
                    static_cast<void>(
                        state_.session().set_file_override_hz(hz_edit_slot_, std::nullopt));
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ImGui::Separator();
        }
        if (state_.session().files().empty()) {
            ImGui::TextDisabled("Open POS or NMEA files to begin.");
        }
        if (move.has_value()) {
            static_cast<void>(state_.session().move_file(move->first, move->second));
        }
        if (erase.has_value()) {
            static_cast<void>(state_.session().erase_file(*erase));
        }
        ImGui::SeparatorText("Summary");
        ImGui::TextUnformatted("Statistics");
        ImGui::SameLine();
        const char* mode_label = statistics_expected_ ? "Expected" : "Recorded";
        const float button_width =
            ImGui::CalcTextSize(mode_label).x + 2.0F * ImGui::GetStyle().FramePadding.x;
        ImGui::SetCursorPosX(
            std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - button_width));
        if (ImGui::SmallButton(mode_label)) {
            statistics_expected_ = !statistics_expected_;
        }
        const auto statistics = state_.session().recorded_statistics();
        const std::optional<TimeRange> effective = state_.session().effective_range();
        const float summary_rows = static_cast<float>(std::min<std::size_t>(statistics.size(), 5));
        const float summary_height = statistics.empty()
            ? ImGui::GetFrameHeightWithSpacing() * 1.5F
            : summary_rows * ImGui::GetTextLineHeightWithSpacing()
                + 2.0F * ImGui::GetStyle().FramePadding.y;
        ImGui::BeginChild("File summary rows", {0.0F, summary_height}, true,
            ImGuiWindowFlags_HorizontalScrollbar);
        if (statistics.empty()) {
            ImGui::TextDisabled("No file summary available.");
        }
        for (std::size_t index = 0; index < statistics.size(); ++index) {
            const RecordedStatistics& recorded = statistics[index];
            const std::optional<std::string> first = recorded.first_sample_time.has_value()
                ? format_absolute_gps_time(*recorded.first_sample_time)
                : std::nullopt;
            const std::optional<std::string> last = recorded.last_sample_time.has_value()
                ? format_absolute_gps_time(*recorded.last_sample_time)
                : std::nullopt;
            const std::optional<std::size_t> expected =
                effective.has_value() && index < state_.session().files().size()
                ? calculate_expected_sample_count(
                      *effective, state_.session().files()[index].effective_hz())
                : std::nullopt;
            const std::size_t recorded_count = recorded_sample_count(recorded);
            const std::size_t denominator =
                statistics_expected_ ? expected.value_or(0) : recorded_count;
            ImGui::Text("Slot %zu  %s .. %s GPST", index + 1,
                first.has_value() ? first->c_str() : "unavailable",
                last.has_value() ? last->c_str() : "unavailable");
            ImGui::SameLine();
            if (!statistics_expected_) {
                ImGui::Text("Recorded N=%zu", denominator);
            } else if (expected.has_value()) {
                ImGui::Text("Expected N=%zu", denominator);
            } else {
                ImGui::TextDisabled("Expected unavailable");
            }
            for (std::size_t quality = 0; quality < solution_quality_count; ++quality) {
                ImGui::SameLine();
                const std::optional<double> percentage =
                    quality_percentage(recorded.quality_counts[quality], denominator);
                if (percentage.has_value()) {
                    ImGui::TextColored(rgba(rtkplot_file1_quality_colors[quality]),
                        "Q%zu:%zu/%.1f%%", quality, recorded.quality_counts[quality], *percentage);
                } else {
                    ImGui::TextDisabled("Q%zu:%zu/-", quality, recorded.quality_counts[quality]);
                }
            }
        }
        ImGui::EndChild();
        if (effective.has_value()) {
            const double duration =
                static_cast<double>(effective->end - effective->start) / nanoseconds_per_second;
            ImGui::Text("Common duration: %.3f s", duration);
        }
    }
    ImGui::End();
    if (!open) {
        file_slots_visible_ = false;
        static_cast<void>(
            state_.set_application_window_visible(ApplicationWindow::FileSlots, false));
    }
}

void FullGui::render_window_manager(const full_detail::InitialWindowLayout& layout)
{
    if (!window_manager_visible_) {
        return;
    }
    if (initial_layout_pending_) {
        ImGui::SetNextWindowPos({layout.window_manager.x, layout.window_manager.y});
        ImGui::SetNextWindowSize({layout.window_manager.width, layout.window_manager.height});
    }
    bool open = true;
    if (ImGui::Begin("Window Manager", &open)) {
        ImGui::SeparatorText("Application Windows");
        const auto application_toggle = [this](const char* label, ApplicationWindow window,
                                            bool& visible) {
            if (ImGui::Checkbox(label, &visible)) {
                static_cast<void>(state_.set_application_window_visible(window, visible));
            }
        };
        application_toggle("File / Slots", ApplicationWindow::FileSlots, file_slots_visible_);
        application_toggle(
            "Shared Controls", ApplicationWindow::SharedControls, shared_controls_visible_);
        ImGui::TextDisabled("Reopen Window Manager from the Window menu.");

        ImGui::SeparatorText("Create Plot Window");
        if (ImGui::Button("Normal 2D")) {
            static_cast<void>(create_plot(PlotType::NormalTrajectory));
        }
        ImGui::SameLine();
        if (ImGui::Button("Normal Time Series")) {
            static_cast<void>(create_plot(PlotType::NormalTimeSeries));
        }
        if (ImGui::Button("Relative 2D")) {
            static_cast<void>(create_plot(PlotType::RelativeTrajectory));
        }
        ImGui::SameLine();
        if (ImGui::Button("Relative Time Series")) {
            static_cast<void>(create_plot(PlotType::RelativeTimeSeries));
        }

        ImGui::SeparatorText("Plot Windows");
        for (const PlotInstanceState& plot : state_.plots()) {
            PlotLocalState& local = local_state(plot.id);
            ImGui::PushID(static_cast<int>(plot.id.value));
            bool visible = plot.visible;
            if (ImGui::Checkbox("##shown", &visible)) {
                static_cast<void>(state_.set_plot_visible(plot.id, visible));
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-62.0F);
            bool committed = false;
            if (ImGui::InputText("##title", local.title_edit.data(), local.title_edit.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (local.title_edit[0] != '\0') {
                    committed = state_.set_plot_title(plot.id, local.title_edit.data());
                }
                if (!committed) {
                    copy_text(local.title_edit, plot.title);
                }
                local.title_editing = false;
            }
            if (ImGui::IsItemActivated()) {
                local.title_backup = plot.title;
                local.title_editing = true;
            }
            if (local.title_editing && ImGui::IsItemActive()
                && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                copy_text(local.title_edit, local.title_backup);
                local.title_editing = false;
            } else if (!committed && local.title_editing && ImGui::IsItemDeactivated()) {
                copy_text(local.title_edit, local.title_backup);
                local.title_editing = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                delete_candidate_ = plot.id;
                delete_popup_requested_ = true;
            }
            ImGui::TextDisabled("%s  [#%llu]", full_plot_type_name(plot.type),
                static_cast<unsigned long long>(plot.id.value));
            ImGui::PopID();
        }
    }
    ImGui::End();
    if (!open) {
        window_manager_visible_ = false;
        static_cast<void>(
            state_.set_application_window_visible(ApplicationWindow::WindowManager, false));
    }
}

void FullGui::render_shared_controls(const full_detail::InitialWindowLayout& layout)
{
    if (!shared_controls_visible_) {
        return;
    }
    if (initial_layout_pending_) {
        ImGui::SetNextWindowPos({layout.shared_controls.x, layout.shared_controls.y});
        ImGui::SetNextWindowSize({layout.shared_controls.width, layout.shared_controls.height});
    }
    bool open = true;
    if (ImGui::Begin("Shared Controls", &open)) {
        ImGui::TextUnformatted("Solution quality");
        for (std::size_t index = 0; index < solution_quality_count; ++index) {
            const bool visible = state_.quality_filter().visible[index];
            const std::string label = "Q" + std::to_string(index);
            const ImVec4 base = visible ? rgba(rtkplot_file1_quality_colors[index])
                                        : ImVec4{0.20F, 0.20F, 0.20F, 1.0F};
            ImGui::PushStyleColor(ImGuiCol_Button, base);
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered, {base.x * 0.85F, base.y * 0.85F, base.z * 0.85F, base.w});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, base);
            if (ImGui::SmallButton(label.c_str())) {
                static_cast<void>(
                    state_.set_quality_visible(static_cast<SolutionQuality>(index), !visible));
            }
            ImGui::PopStyleColor(3);
            if (index + 1 != solution_quality_count) {
                ImGui::SameLine();
            }
        }
        if (ImGui::Button("Common time...")) {
            common_time_popup_requested_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reference matching...")) {
            match_popup_requested_ = true;
        }

        const EnuReferenceConfiguration current = state_.session().enu_configuration();
        ImGui::SeparatorText("ENU method");
        int method = static_cast<int>(current.method);
        if (ImGui::Combo("##enu-method", &method,
                "Slot 1 start\0Slot 1 end\0Slot 1 ECEF average\0User specified\0")) {
            EnuReferenceConfiguration next = current;
            next.method = static_cast<EnuReferenceMethod>(method);
            if (next.method == EnuReferenceMethod::UserSpecified
                && !next.user_position.has_value()) {
                enu_popup_requested_ = true;
            } else if (!state_.session().set_enu_reference_configuration(next)) {
                notify(NotificationLevel::Warning, "ENU reference is unavailable");
            }
        }
        if (method == static_cast<int>(EnuReferenceMethod::UserSpecified)) {
            if (ImGui::Button("Edit...")) {
                enu_popup_requested_ = true;
            }
        }
        if (method == static_cast<int>(EnuReferenceMethod::UserSpecified)) {
            ImGui::SameLine();
        }
        if (ImGui::Button("Options...")) {
            options_popup_requested_ = true;
        }
        if (caution_present_) {
            ImGui::TextColored({1.0F, 0.72F, 0.2F, 1.0F}, "Caution: review notifications.");
        } else {
            ImGui::TextDisabled("No cautions.");
        }
        if (ImGui::Button("Show notifications")) {
            notifications_visible_ = true;
        }
    }
    ImGui::End();
    if (!open) {
        shared_controls_visible_ = false;
        static_cast<void>(
            state_.set_application_window_visible(ApplicationWindow::SharedControls, false));
    }
}

void FullGui::render_plot_windows()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const full_detail::WindowRect plot_region = full_detail::initial_window_layout(
        viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, viewport->WorkSize.y)
                                                    .initial_plot;
    for (auto& [id, local] : plot_local_) {
        static_cast<void>(id);
        if (local.placement_pending) {
            local.initial_rect = full_detail::cascaded_plot_rect(plot_region, local.cascade_index);
        }
    }
    synchronize_local_options();
    runtime_.render_visible(state_, shared_options_, shared_options_revision_,
        [this](const PlotInstanceState& plot, ImPlotComponent& component) {
            render_plot_window(plot, component, local_state(plot.id));
        });
}

void FullGui::render_plot_window(
    const PlotInstanceState& plot, ImPlotComponent& component, PlotLocalState& local)
{
    if (local.placement_pending) {
        ImGui::SetNextWindowPos({local.initial_rect.x, local.initial_rect.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            {local.initial_rect.width, local.initial_rect.height}, ImGuiCond_Always);
        local.placement_pending = false;
    } else {
        if (plot.position.has_value()) {
            ImGui::SetNextWindowPos(
                {plot.position->x_px, plot.position->y_px}, ImGuiCond_Appearing);
        }
        if (plot.size.has_value()) {
            ImGui::SetNextWindowSize(
                {plot.size->width_px, plot.size->height_px}, ImGuiCond_Appearing);
        }
    }
    bool open = true;
    const std::string label = plot_window_label(plot);
    if (ImGui::Begin(label.c_str(), &open)) {
        if (ImGui::Button("Fit")) {
            component.request_fit();
        }
        ImGui::SameLine();
        int draw_mode = static_cast<int>(local.options.batch.draw_mode);
        ImGui::SetNextItemWidth(120.0F);
        if (ImGui::Combo("##draw-mode", &draw_mode, "Line\0Point\0Line + Point\0")) {
            local.options.batch.draw_mode = static_cast<DrawMode>(draw_mode);
            ++local.revision;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(85.0F);
        if (ImGui::SliderFloat(
                "##point-size", &local.options.marker_size_px, 1.0F, 10.0F, "%.0f px")) {
            ++local.revision;
        }
        ImGui::SameLine();
        if (ImGui::Button("Ranges...")) {
            local.ranges_initialized = false;
            ImGui::OpenPopup("Ranges");
        }
        if (!is_trajectory(plot.type)) {
            bool east = local.options.show_east;
            bool north = local.options.show_north;
            bool vertical = local.options.show_vertical;
            bool components_changed = ImGui::Checkbox("E", &east);
            ImGui::SameLine();
            components_changed = ImGui::Checkbox("N", &north) || components_changed;
            ImGui::SameLine();
            components_changed = ImGui::Checkbox("V", &vertical) || components_changed;
            if (components_changed) {
                local.options.show_east = east;
                local.options.show_north = north;
                local.options.show_vertical = vertical;
                ++local.revision;
            }
            ImGui::SameLine();
            int vertical_component =
                local.options.vertical_component == PositionComponent::Up ? 0 : 1;
            ImGui::SetNextItemWidth(145.0F);
            if (ImGui::Combo("##vertical-component", &vertical_component, "Up\0Height\0")) {
                local.options.vertical_component = vertical_component == 0
                    ? PositionComponent::Up
                    : PositionComponent::EllipsoidalHeight;
                ++local.revision;
            }
        }
        if (is_trajectory(plot.type)) {
            static_cast<void>(
                component.render_trajectory("trajectory", PlotAreaSize{-1.0F, -1.0F}));
            handle_plot_window_resize(component, local);
        } else {
            component.render_time_series("time-series", PlotAreaSize{-1.0F, -1.0F});
        }
        render_plot_ranges(plot, component, local);
    }
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    static_cast<void>(
        state_.set_plot_position(plot.id, FloatingWindowPosition{position.x, position.y}));
    static_cast<void>(state_.set_plot_size(plot.id, FloatingWindowSize{size.x, size.y}));
    ImGui::End();
    if (!open) {
        static_cast<void>(state_.set_plot_visible(plot.id, false));
    }
}

void FullGui::handle_plot_window_resize(ImPlotComponent& component, PlotLocalState&)
{
    const ImVec2 current_position = ImGui::GetWindowPos();
    const ImVec2 current_size = ImGui::GetWindowSize();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float maximum_width = std::max(
        style.WindowMinSize.x, viewport->WorkPos.x + viewport->WorkSize.x - current_position.x);
    const float maximum_height = std::max(
        style.WindowMinSize.y, viewport->WorkPos.y + viewport->WorkSize.y - current_position.y);
    const auto constrained_size = [&](double width, double height) {
        return ImVec2{
            std::clamp(static_cast<float>(width), style.WindowMinSize.x, maximum_width),
            std::clamp(static_cast<float>(height), style.WindowMinSize.y, maximum_height),
        };
    };

    if (const std::optional<double> factor = component.consume_window_resize_factor()) {
        ImGui::SetWindowSize(constrained_size(static_cast<double>(current_size.x) * *factor,
            static_cast<double>(current_size.y) * *factor));
    }
}

void FullGui::render_plot_ranges(
    const PlotInstanceState& plot, ImPlotComponent& component, PlotLocalState& local)
{
    if (!ImGui::BeginPopupModal("Ranges", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (consume_escape_cancel()) {
        local.ranges_initialized = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (!local.ranges_initialized) {
        if (is_trajectory(plot.type) && component.trajectory_metrics().has_value()) {
            const TrajectoryPlotMetrics& metrics = *component.trajectory_metrics();
            local.trajectory_ranges[0] = metrics.east.minimum;
            local.trajectory_ranges[1] = metrics.east.maximum;
            local.trajectory_ranges[2] = metrics.north.minimum;
            local.trajectory_ranges[3] = metrics.north.maximum;
            local.trajectory_scale = metrics.meters_per_pixel;
        } else if (!is_trajectory(plot.type)) {
            if (const std::optional<TimeRange> time = component.time_series_time_range()) {
                static_cast<void>(
                    write_gps_text(local.time_start_text, local.time_start_original, time->start));
                static_cast<void>(
                    write_gps_text(local.time_end_text, local.time_end_original, time->end));
                local.time_start_edited = local.time_end_edited = false;
            }
            local.position_present.fill(false);
            local.range_copy_source = -1;
            local.range_copy_targets.fill(false);
            for (const TimeSeriesPanelMetrics& metric : component.time_series_metrics()) {
                const std::size_t index = static_cast<std::size_t>(metric.component);
                local.position_present[index] = true;
                local.position_minimum[index] = metric.position.minimum;
                local.position_maximum[index] = metric.position.maximum;
            }
        }
        local.ranges_initialized = true;
    }
    if (is_trajectory(plot.type)) {
        ImGui::InputDouble("East minimum", &local.trajectory_ranges[0]);
        ImGui::InputDouble("East maximum", &local.trajectory_ranges[1]);
        ImGui::InputDouble("North minimum", &local.trajectory_ranges[2]);
        ImGui::InputDouble("North maximum", &local.trajectory_ranges[3]);
        ImGui::InputDouble("Meters / pixel", &local.trajectory_scale);
        const bool ranges_valid =
            valid_range(local.trajectory_ranges[0], local.trajectory_ranges[1])
            && valid_range(local.trajectory_ranges[2], local.trajectory_ranges[3]);
        ImGui::BeginDisabled(!ranges_valid);
        if (ImGui::Button("Apply axis ranges")) {
            static_cast<void>(component.set_trajectory_ranges(
                {local.trajectory_ranges[0], local.trajectory_ranges[1]},
                {local.trajectory_ranges[2], local.trajectory_ranges[3]}));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool scale_valid =
            std::isfinite(local.trajectory_scale) && local.trajectory_scale > 0.0;
        ImGui::BeginDisabled(!scale_valid);
        if (ImGui::Button("Apply scale")) {
            static_cast<void>(component.apply_trajectory_meters_per_pixel(local.trajectory_scale));
        }
        ImGui::EndDisabled();
    } else {
        if (ImGui::InputText(
                "Start (GPST)", local.time_start_text.data(), local.time_start_text.size())) {
            local.time_start_edited = true;
        }
        if (ImGui::InputText(
                "End (GPST)", local.time_end_text.data(), local.time_end_text.size())) {
            local.time_end_edited = true;
        }
        const std::optional<GpsTime> start = read_gps_text(
            local.time_start_text, local.time_start_original, local.time_start_edited);
        const std::optional<GpsTime> end =
            read_gps_text(local.time_end_text, local.time_end_original, local.time_end_edited);
        ImGui::BeginDisabled(!start.has_value() || !end.has_value() || *start >= *end);
        if (ImGui::Button("Apply time")) {
            static_cast<void>(component.set_time_series_time_range({*start, *end}));
        }
        ImGui::EndDisabled();
        for (std::size_t index = 0; index < local.position_present.size(); ++index) {
            if (!local.position_present[index]) {
                continue;
            }
            ImGui::PushID(static_cast<int>(index));
            ImGui::SeparatorText(component_name(static_cast<PositionComponent>(index)));
            ImGui::InputDouble("Minimum", &local.position_minimum[index]);
            ImGui::InputDouble("Maximum", &local.position_maximum[index]);
            ImGui::BeginDisabled(
                !valid_range(local.position_minimum[index], local.position_maximum[index]));
            if (ImGui::Button("Apply range")) {
                static_cast<void>(
                    component.set_time_series_position_range(static_cast<PositionComponent>(index),
                        {local.position_minimum[index], local.position_maximum[index]}));
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Copy...")) {
                local.range_copy_source = static_cast<int>(index);
                local.range_copy_targets.fill(false);
            }
            ImGui::PopID();
        }
        if (local.range_copy_source >= 0) {
            const std::size_t source = static_cast<std::size_t>(local.range_copy_source);
            ImGui::SeparatorText("One-shot vertical range copy");
            ImGui::Text(
                "Copy %s range to:", component_name(static_cast<PositionComponent>(source)));
            bool any_target = false;
            for (std::size_t index = 0; index < local.position_present.size(); ++index) {
                if (!local.position_present[index] || index == source) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(index));
                ImGui::Checkbox(component_name(static_cast<PositionComponent>(index)),
                    &local.range_copy_targets[index]);
                ImGui::PopID();
                any_target = any_target || local.range_copy_targets[index];
                ImGui::SameLine();
            }
            ImGui::NewLine();
            const NumericRange source_range{
                local.position_minimum[source], local.position_maximum[source]};
            ImGui::BeginDisabled(
                !valid_range(source_range.minimum, source_range.maximum) || !any_target);
            if (ImGui::Button("Apply to selected")) {
                bool applied = true;
                for (std::size_t index = 0; index < local.position_present.size(); ++index) {
                    if (!local.range_copy_targets[index]) {
                        continue;
                    }
                    applied = component.set_time_series_position_range(
                                  static_cast<PositionComponent>(index), source_range)
                        && applied;
                    local.position_minimum[index] = source_range.minimum;
                    local.position_maximum[index] = source_range.maximum;
                }
                if (!applied) {
                    notify(NotificationLevel::Warning,
                        "Could not copy the time-series position range");
                }
                local.range_copy_source = -1;
                local.range_copy_targets.fill(false);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel copy")) {
                local.range_copy_source = -1;
                local.range_copy_targets.fill(false);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        local.ranges_initialized = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FullGui::render_notifications()
{
    if (!notifications_visible_) {
        return;
    }
    if (ImGui::Begin("Notifications", &notifications_visible_)) {
        if (ImGui::Button("Clear")) {
            notifications_.clear();
            caution_present_ = false;
        }
        ImGui::Separator();
        for (const UserNotification& notification : notifications_.entries()) {
            ImGui::TextWrapped("[%s] %s", notification_level_name(notification.level),
                notification.message.c_str());
        }
        if (notifications_.entries().empty()) {
            ImGui::TextDisabled("No notifications recorded.");
        }
    }
    ImGui::End();
}

void FullGui::render_file_workflow_modals()
{
    if (large_file_popup_requested_) {
        ImGui::OpenPopup("Large input file");
        large_file_popup_requested_ = false;
    }
    if (ImGui::BeginPopupModal("Large input file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (consume_escape_cancel()) {
            large_file_decision_.reset();
            ImGui::CloseCurrentPopup();
        } else if (large_file_decision_.has_value()) {
            ImGui::TextWrapped("%s is at least 100 MiB. Continue loading?",
                large_file_decision_->path.filename().string().c_str());
            if (ImGui::Button("Continue")) {
                PendingLoad accepted = std::move(*large_file_decision_);
                large_file_decision_.reset();
                accepted.size_confirmed = true;
                ImGui::CloseCurrentPopup();
                attempt_load(std::move(accepted));
            }
            ImGui::SameLine();
            if (ImGui::Button("Skip")) {
                large_file_decision_.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    if (format_popup_requested_) {
        ImGui::OpenPopup("Choose input format");
        format_popup_requested_ = false;
    }
    if (ImGui::BeginPopupModal("Choose input format", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (consume_escape_cancel()) {
            format_decision_.reset();
            ImGui::CloseCurrentPopup();
        } else if (format_decision_.has_value()) {
            ImGui::Text("Format for %s", format_decision_->path.filename().string().c_str());
            if (ImGui::Button("POS")) {
                PendingLoad load = std::move(*format_decision_);
                format_decision_.reset();
                load.format = InputFormat::Pos;
                ImGui::CloseCurrentPopup();
                attempt_load(std::move(load));
            }
            ImGui::SameLine();
            if (ImGui::Button("NMEA")) {
                PendingLoad load = std::move(*format_decision_);
                format_decision_.reset();
                load.format = InputFormat::Nmea;
                ImGui::CloseCurrentPopup();
                attempt_load(std::move(load));
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                format_decision_.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    if (nmea_popup_requested_) {
        ImGui::OpenPopup("NMEA input decision");
        nmea_popup_requested_ = false;
    }
    if (ImGui::BeginPopupModal("NMEA input decision", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (consume_escape_cancel()) {
            nmea_decision_.reset();
            ImGui::CloseCurrentPopup();
        } else if (nmea_decision_.has_value()) {
            PendingLoad& load = *nmea_decision_;
            ImGui::TextUnformatted(load.path.filename().string().c_str());
            if (load.needs_missing_geoid_decision) {
                ImGui::Checkbox(
                    "Use GGA altitude as ellipsoidal height", &load.use_altitude_as_height);
            }
            if (load.needs_date) {
                ImGui::InputInt3("Y / M / D", load.date);
                if (ImGui::Button("Use file timestamp date")) {
                    const NmeaDate date = file_date(load.path);
                    load.date[0] = date.year;
                    load.date[1] = static_cast<int>(date.month);
                    load.date[2] = static_cast<int>(date.day);
                }
            }
            const bool valid = (!load.needs_missing_geoid_decision || load.use_altitude_as_height)
                && (!load.needs_date || valid_nmea_date(load.date));
            if (load.needs_date && !valid_nmea_date(load.date)) {
                ImGui::TextColored({1.0F, 0.35F, 0.35F, 1.0F}, "Enter a valid calendar date.");
            }
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button("OK")) {
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
        }
        ImGui::EndPopup();
    }
}

void FullGui::render_common_time_dialog()
{
    if (common_time_popup_requested_) {
        ImGui::OpenPopup("Common time");
        common_time_popup_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Common time", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (consume_escape_cancel()) {
        common_time_initialized_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (!common_time_initialized_) {
        const CommonTimeRange configured = state_.session().configured_time_range();
        const std::optional<TimeRange> effective = state_.session().effective_range();
        common_start_enabled_ = configured.start_enabled;
        common_end_enabled_ = configured.end_enabled;
        const GpsTime fallback_start = effective.has_value() ? effective->start : GpsTime{0};
        const GpsTime fallback_end = effective.has_value() ? effective->end : GpsTime{0};
        static_cast<void>(write_gps_text(common_start_text_, common_start_original_,
            configured.entered_start.value_or(fallback_start)));
        static_cast<void>(write_gps_text(
            common_end_text_, common_end_original_, configured.entered_end.value_or(fallback_end)));
        common_start_edited_ = common_end_edited_ = false;
        common_time_initialized_ = true;
    }
    ImGui::Checkbox("Start", &common_start_enabled_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!common_start_enabled_);
    if (ImGui::InputText("##common-start", common_start_text_.data(), common_start_text_.size())) {
        common_start_edited_ = true;
    }
    ImGui::EndDisabled();
    ImGui::Checkbox("End", &common_end_enabled_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!common_end_enabled_);
    if (ImGui::InputText("##common-end", common_end_text_.data(), common_end_text_.size())) {
        common_end_edited_ = true;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("GPST: YYYY-MM-DD hh:mm:ss.sss");
    if (ImGui::Button("Use intersection")) {
        if (const std::optional<TimeRange> intersection =
                intersection_time_range(state_.session().files())) {
            static_cast<void>(
                write_gps_text(common_start_text_, common_start_original_, intersection->start));
            static_cast<void>(
                write_gps_text(common_end_text_, common_end_original_, intersection->end));
            common_start_edited_ = common_end_edited_ = false;
            common_start_enabled_ = common_end_enabled_ = true;
        } else {
            notify(NotificationLevel::Warning, "No common intersection");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("OK")) {
        CommonTimeRange range;
        range.start_enabled = common_start_enabled_;
        range.end_enabled = common_end_enabled_;
        range.entered_start =
            read_gps_text(common_start_text_, common_start_original_, common_start_edited_);
        range.entered_end =
            read_gps_text(common_end_text_, common_end_original_, common_end_edited_);
        if (state_.session().set_common_time_range(range)) {
            common_time_initialized_ = false;
            ImGui::CloseCurrentPopup();
        } else {
            notify(NotificationLevel::Warning, "Invalid common time range");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        common_time_initialized_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FullGui::render_enu_dialog()
{
    if (enu_popup_requested_) {
        ImGui::OpenPopup("Edit user ENU");
        enu_popup_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Edit user ENU", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (consume_escape_cancel()) {
        enu_initialized_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (!enu_initialized_) {
        const auto position = state_.session().enu_configuration().user_position;
        enu_coordinate_kind_ = 0;
        enu_values_[0] = enu_values_[1] = enu_values_[2] = 0.0;
        if (position.has_value()) {
            if (const auto* llh = std::get_if<UserSpecifiedLlh>(&*position)) {
                enu_values_[0] = llh->latitude_deg;
                enu_values_[1] = llh->longitude_deg;
                enu_values_[2] = llh->ellipsoidal_height_m;
            } else if (const auto* ecef = std::get_if<Ecef>(&*position)) {
                enu_coordinate_kind_ = 1;
                enu_values_[0] = ecef->x_m;
                enu_values_[1] = ecef->y_m;
                enu_values_[2] = ecef->z_m;
            }
        }
        enu_initialized_ = true;
    }
    ImGui::Combo("Coordinates", &enu_coordinate_kind_, "LLH\0ECEF\0");
    ImGui::InputDouble(enu_coordinate_kind_ == 0 ? "Latitude" : "X", &enu_values_[0]);
    ImGui::InputDouble(enu_coordinate_kind_ == 0 ? "Longitude" : "Y", &enu_values_[1]);
    ImGui::InputDouble(enu_coordinate_kind_ == 0 ? "Height" : "Z", &enu_values_[2]);
    if (ImGui::Button("Apply")) {
        EnuReferenceConfiguration configuration = state_.session().enu_configuration();
        configuration.method = EnuReferenceMethod::UserSpecified;
        configuration.user_position = enu_coordinate_kind_ == 0
            ? UserSpecifiedEnuPosition{UserSpecifiedLlh{
                  enu_values_[0], enu_values_[1], enu_values_[2]}}
            : UserSpecifiedEnuPosition{Ecef{enu_values_[0], enu_values_[1], enu_values_[2]}};
        if (state_.session().set_enu_reference_configuration(configuration)) {
            enu_initialized_ = false;
            ImGui::CloseCurrentPopup();
        } else {
            notify(NotificationLevel::Warning, "ENU reference is unavailable");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        enu_initialized_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FullGui::render_reference_match_dialog()
{
    if (match_popup_requested_) {
        ImGui::OpenPopup("Reference matching");
        match_popup_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Reference matching", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (consume_escape_cancel()) {
        match_initialized_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (!match_initialized_) {
        const ReferenceMatchConfiguration current = state_.session().match_configuration();
        match_enabled_ = current.tolerance_check_enabled;
        match_seconds_ =
            static_cast<double>(current.maximum_time_difference_ns) / nanoseconds_per_second;
        match_initialized_ = true;
    }
    ImGui::Checkbox("Maximum age enabled", &match_enabled_);
    ImGui::BeginDisabled(!match_enabled_);
    ImGui::InputDouble("Maximum age (s)", &match_seconds_, 0.001, 0.1, "%.9g");
    ImGui::EndDisabled();
    if (ImGui::Button("OK")) {
        const long double ns = match_seconds_ * nanoseconds_per_second;
        if (std::isfinite(match_seconds_) && ns >= 0.0L
            && ns <= static_cast<long double>(std::numeric_limits<std::int64_t>::max())
            && state_.session().set_reference_match_configuration(
                {match_enabled_, static_cast<std::int64_t>(std::llround(ns))})) {
            match_initialized_ = false;
            ImGui::CloseCurrentPopup();
        } else {
            notify(NotificationLevel::Warning, "Invalid reference matching tolerance");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        match_initialized_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FullGui::render_options_dialog()
{
    if (options_popup_requested_) {
        ImGui::OpenPopup("Options");
        options_popup_requested_ = false;
    }
    if (!ImGui::BeginPopupModal("Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (consume_escape_cancel()) {
        options_initialized_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (!options_initialized_) {
        trajectory_fit_ratio_ = shared_options_.trajectory_fit_ratio;
        time_series_fit_ratio_ = shared_options_.time_series_fit_ratio;
        default_point_size_px_ = std::clamp(default_point_size_px_, 1.0F, 10.0F);
        zoom_modifier_index_ = modifier_index(shared_options_.zoom_center_modifier);
        resize_modifier_index_ = modifier_index(shared_options_.window_resize_modifier);
        slot_order_index_ = static_cast<int>(shared_options_.batch.slot_order);
        quality_order_index_ = static_cast<int>(shared_options_.batch.quality_order);
        options_initialized_ = true;
    }
    ImGui::InputDouble("Trajectory fit ratio", &trajectory_fit_ratio_, 0.01, 0.1, "%.3f");
    ImGui::InputDouble("Time-series fit ratio", &time_series_fit_ratio_, 0.01, 0.1, "%.3f");
    ImGui::SliderFloat("Default point size", &default_point_size_px_, 1.0F, 10.0F, "%.0f px");
    ImGui::TextDisabled("Default point size applies to newly created plots.");
    ImGui::Combo("Center-fixed zoom", &zoom_modifier_index_, "Ctrl\0Shift\0Alt\0");
    ImGui::Combo("Window resize", &resize_modifier_index_, "Ctrl\0Shift\0Alt\0");
    ImGui::Combo(
        "Slot drawing order", &slot_order_index_, "Larger slot in front\0Smaller slot in front\0");
    ImGui::Combo("Quality drawing order", &quality_order_index_,
        "Better quality in front\0Lower quality in front\0");
    const bool valid = std::isfinite(trajectory_fit_ratio_) && trajectory_fit_ratio_ > 0.0
        && trajectory_fit_ratio_ <= 1.0 && std::isfinite(time_series_fit_ratio_)
        && time_series_fit_ratio_ > 0.0 && time_series_fit_ratio_ <= 1.0
        && zoom_modifier_index_ != resize_modifier_index_;
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Apply")) {
        shared_options_.trajectory_fit_ratio = trajectory_fit_ratio_;
        shared_options_.time_series_fit_ratio = time_series_fit_ratio_;
        shared_options_.marker_size_px = default_point_size_px_;
        shared_options_.zoom_center_modifier = modifier_at(zoom_modifier_index_);
        shared_options_.window_resize_modifier = modifier_at(resize_modifier_index_);
        shared_options_.batch.slot_order = static_cast<SlotDrawingOrder>(slot_order_index_);
        shared_options_.batch.quality_order =
            static_cast<QualityDrawingOrder>(quality_order_index_);
        ++shared_options_revision_;
        options_initialized_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        options_initialized_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FullGui::render_delete_confirmation()
{
    if (delete_popup_requested_) {
        ImGui::OpenPopup("Delete plot window?");
        delete_popup_requested_ = false;
    }
    if (!ImGui::BeginPopupModal(
            "Delete plot window?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (!delete_candidate_.has_value()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const PlotInstanceState* plot = state_.find_plot(*delete_candidate_);
    if (plot == nullptr) {
        delete_candidate_.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::Text("Title: %s", plot->title.c_str());
    ImGui::Text("Type: %s", full_plot_type_name(plot->type));
    ImGui::Text("ID: %llu", static_cast<unsigned long long>(plot->id.value));
    if (consume_escape_cancel() || ImGui::Button("Cancel")) {
        delete_candidate_.reset();
        ImGui::CloseCurrentPopup();
    } else {
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            const PlotWindowId id = *delete_candidate_;
            delete_candidate_.reset();
            erase_plot(id);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void FullGui::render(SDL_Window* window)
{
    escape_cancel_consumed_ = false;
    drain_dialog_paths();
    process_load_queue();
    render_main_menu(window);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const full_detail::InitialWindowLayout layout = full_detail::initial_window_layout(
        viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, viewport->WorkSize.y);
    render_file_slots(window, layout);
    render_window_manager(layout);
    render_shared_controls(layout);
    render_plot_windows();
    initial_layout_pending_ = false;

    render_file_workflow_modals();
    render_common_time_dialog();
    render_enu_dialog();
    render_reference_match_dialog();
    render_options_dialog();
    render_delete_confirmation();
    render_notifications();
}

} // namespace rtktrace
