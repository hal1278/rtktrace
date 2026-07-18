#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>

#include "plotcore/session_state.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] plotcore::LoadedFile parsed_file(std::string_view name, double longitude_offset)
{
    std::ostringstream text;
    text << "0 0.0 35.0 " << 139.0 + longitude_offset << " 10.0 1\n"
         << "0 1.0 35.0 " << 139.0 + longitude_offset << " 11.0 2\n";
    std::istringstream input{text.str()};
    return plotcore::parse_pos(input, std::filesystem::path{name});
}

void test_format_detection()
{
    using namespace plotcore;
    check(infer_input_format("solution.POS") == InputFormat::Pos,
        "POS extension detection is case-insensitive");
    check(infer_input_format("receiver.gga") == InputFormat::Nmea,
        "NMEA GGA extension is recognized");
    check(!infer_input_format("ambiguous.txt").has_value(),
        "ambiguous extensions require a user format decision");
}

void test_processing_pipeline_and_slots()
{
    using namespace plotcore;
    PlotSessionState state;
    check(state.add_loaded_file(parsed_file("one.pos", 0.0))
            && state.add_loaded_file(parsed_file("two.pos", 0.00001)),
        "loaded files enter the shared processing pipeline");
    check(state.files().size() == 2 && state.enu_available()
            && state.effective_range() == TimeRange{GpsTime{0}, GpsTime{1'000'000'000}},
        "union time range and ENU reference are available after loading");

    const std::optional<PlotDataView> normal = state.normal_plot_data_view();
    const std::optional<PlotDataView> relative = state.relative_plot_data_view();
    check(normal.has_value() && normal->series.size() == 2, "normal view exposes every slot");
    check(relative.has_value() && relative->series.size() == 1
            && relative->series[0].slot_number == 2,
        "relative view uses slot one as the reference");
    check(state.recorded_statistics().size() == 2,
        "recorded statistics share the effective time range");

    const std::uint64_t visibility_revision = state.revision();
    check(state.set_file_visible(2, false) && state.revision() > visibility_revision
            && !state.normal_plot_data_view()->series[1].file_visible,
        "file visibility updates views without removing data");
    check(state.move_file(2, 1) && state.files()[0].source_path == "two.pos",
        "slot reordering replaces the reference and rebuilds derived data");
    check(state.erase_file(2) && state.files().size() == 1
            && state.relative_plot_data_view()->series.empty(),
        "slot deletion compacts files and relative data");
}

void test_configuration_validation()
{
    using namespace plotcore;
    PlotSessionState state;
    static_cast<void>(state.add_loaded_file(parsed_file("one.pos", 0.0)));
    CommonTimeRange invalid;
    invalid.start_enabled = true;
    check(!state.set_common_time_range(invalid), "enabled time boundary requires a value");
    check(!state.set_reference_match_configuration(ReferenceMatchConfiguration{true, -1}),
        "negative reference tolerance is rejected");
    EnuReferenceConfiguration invalid_enu;
    invalid_enu.method = EnuReferenceMethod::UserSpecified;
    invalid_enu.user_position =
        UserSpecifiedLlh{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0};
    check(!state.set_enu_reference_configuration(invalid_enu),
        "invalid user ENU reference is rejected without replacing the active reference");
    check(state.set_file_override_hz(1, 2.0) && state.files()[0].effective_hz() == 2.0
            && state.set_file_override_hz(1, std::nullopt),
        "Hz override can be applied and cleared");
}

} // namespace

int main()
{
    test_format_detection();
    test_processing_pipeline_and_slots();
    test_configuration_validation();
    return failures == 0 ? 0 : 1;
}
