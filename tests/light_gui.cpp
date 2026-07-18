#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <string>

#include "../src/light/gui.hpp"
#include "imgui.h"
#include "implot.h"
#include "plotcore/analysis/coordinates.hpp"

namespace {

[[nodiscard]] plotcore::LoadedFile synthetic_file(std::size_t slot, std::size_t sample_count)
{
    using namespace plotcore;
    LoadedFile file{
        std::filesystem::path{"synthetic-" + std::to_string(slot) + ".pos"}, InputFormat::Pos};
    file.samples.reserve(sample_count);
    constexpr double radians_per_degree = std::numbers::pi / 180.0;
    for (std::size_t index = 0; index < sample_count; ++index) {
        const Wgs84Llh llh{
            (35.0 + static_cast<double>(index) * 1.0e-7) * radians_per_degree,
            (139.0 + static_cast<double>(slot) * 1.0e-5 + static_cast<double>(index) * 5.0e-8)
                * radians_per_degree,
            10.0 + static_cast<double>(slot) * 0.2 + static_cast<double>(index) * 1.0e-4,
        };
        file.samples.push_back(NormalizedSample{
            .time = GpsTime{1'400'000'000'000'000'000LL
                + static_cast<std::int64_t>(index) * 100'000'000},
            .llh = llh,
            .ecef = wgs84_llh_to_ecef(llh),
            .enu = {},
            .quality = static_cast<SolutionQuality>(index % 6 + 1),
            .source_line_number = index + 1,
            .continuous_from_previous = index != 0,
        });
    }
    static_cast<void>(file.set_estimated_hz(10.0));
    return file;
}

} // namespace

int main()
{
    using namespace plotcore;
    constexpr std::size_t slot_count = 4;
    constexpr std::size_t samples_per_slot = 10'000;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{1600.0F, 1200.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    LightGui gui;
    const auto setup_start = std::chrono::steady_clock::now();
    bool loaded = true;
    for (std::size_t slot = 1; slot <= slot_count; ++slot) {
        loaded = gui.add_loaded_file(synthetic_file(slot, samples_per_slot)) && loaded;
    }
    const auto setup_end = std::chrono::steady_clock::now();

    ImGui::NewFrame();
    const auto frame_start = std::chrono::steady_clock::now();
    gui.render(nullptr);
    const auto frame_end = std::chrono::steady_clock::now();
    ImGui::Render();

    const double setup_ms =
        std::chrono::duration<double, std::milli>(setup_end - setup_start).count();
    const double frame_ms =
        std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
    std::cout << "plotcore light performance: " << slot_count << " slots x " << samples_per_slot
              << " samples; pipeline=" << setup_ms << " ms, headless frame=" << frame_ms << " ms\n";

    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return loaded ? 0 : 1;
}
