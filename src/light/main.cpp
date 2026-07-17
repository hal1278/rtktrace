#include <cstdio>
#include <cstdlib>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "gui.hpp"
#include "implot.h"

int main(int argc, char** argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0)
        || !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE)
        || !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3)
        || !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3)) {
        std::fprintf(stderr, "OpenGL 3.3 core profile configuration failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    const SDL_WindowFlags window_flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    SDL_Window* window = SDL_CreateWindow("plotcore light", 1280, 720, window_flags);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!SDL_SetWindowMinimumSize(window,
            plotcore::light_minimum_window_width,
            plotcore::light_minimum_window_height)) {
        std::fprintf(stderr, "Window minimum size configuration failed: %s\n",
            SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_Rect usable_bounds;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (display != 0 && SDL_GetDisplayUsableBounds(display, &usable_bounds)) {
        static_cast<void>(SDL_SetWindowMaximumSize(window,
            usable_bounds.w < plotcore::light_minimum_window_width
                ? plotcore::light_minimum_window_width
                : usable_bounds.w,
            usable_bounds.h < plotcore::light_minimum_window_height
                ? plotcore::light_minimum_window_height
                : usable_bounds.h));
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        std::fprintf(stderr, "OpenGL context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!SDL_GL_MakeCurrent(window, gl_context)) {
        std::fprintf(stderr, "Making the OpenGL context current failed: %s\n", SDL_GetError());
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForOpenGL(window, gl_context)) {
        std::fprintf(stderr, "Dear ImGui SDL3 backend initialization failed\n");
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    constexpr const char* glsl_version = "#version 330 core";
    if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
        std::fprintf(stderr, "Dear ImGui OpenGL3 backend initialization failed\n");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (ImPlot::CreateContext() == nullptr) {
        std::fprintf(stderr, "ImPlot context creation failed\n");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_ShowWindow(window);

    plotcore::LightGui gui;
    for (int index = 1; index < argc; ++index) {
        gui.enqueue_file(argv[index]);
    }
    int smoke_frames = 0;
    if (const char* configured_frames = std::getenv("PLOTCORE_SMOKE_FRAMES")) {
        smoke_frames = std::atoi(configured_frames);
    }

    bool done = false;

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                done = true;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
                && event.window.windowID == SDL_GetWindowID(window)) {
                done = true;
            }
        }

        if (done) {
            break;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        gui.render(window);

        ImGui::Render();
        int display_width = 0;
        int display_height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &display_width, &display_height)) {
            std::fprintf(stderr, "Reading the drawable size failed: %s\n", SDL_GetError());
            done = true;
            continue;
        }

        glViewport(0, 0, display_width, display_height);
        glClearColor(0.10F, 0.12F, 0.15F, 1.00F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        if (smoke_frames > 0 && --smoke_frames == 0) {
            done = true;
        }
    }

    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
