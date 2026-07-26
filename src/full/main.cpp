#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "gui.hpp"
#include "imgui.h"
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
        std::fprintf(stderr, "OpenGL 3.3 configuration failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    const SDL_WindowFlags flags =
        static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    SDL_Window* window = SDL_CreateWindow("rtktrace full", 1280, 720, flags);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (!SDL_SetWindowMinimumSize(
            window, rtktrace::full_minimum_window_width, rtktrace::full_minimum_window_height)) {
        std::fprintf(stderr, "Window minimum size configuration failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_Rect usable_bounds;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (display != 0 && SDL_GetDisplayUsableBounds(display, &usable_bounds)) {
        static_cast<void>(SDL_SetWindowMaximumSize(window,
            std::max(usable_bounds.w, rtktrace::full_minimum_window_width),
            std::max(usable_bounds.h, rtktrace::full_minimum_window_height)));
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr || !SDL_GL_MakeCurrent(window, context)) {
        std::fprintf(stderr, "OpenGL context setup failed: %s\n", SDL_GetError());
        if (context != nullptr) {
            SDL_GL_DestroyContext(context);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    static_cast<void>(SDL_GL_SetSwapInterval(1));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForOpenGL(window, context)
        || !ImGui_ImplOpenGL3_Init("#version 330 core")) {
        std::fprintf(stderr, "Dear ImGui backend initialization failed\n");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (ImPlot::CreateContext() == nullptr) {
        std::fprintf(stderr, "ImPlot context creation failed\n");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    rtktrace::FullGui gui;
    for (int index = 1; index < argc; ++index) {
        gui.enqueue_file(argv[index]);
    }
    int smoke_frames = 0;
    if (const char* configured = std::getenv("RTKTRACE_SMOKE_FRAMES")) {
        smoke_frames = std::atoi(configured);
    }
    SDL_ShowWindow(window);

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT
                || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
                    && event.window.windowID == SDL_GetWindowID(window))) {
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
        done = gui.exit_requested();

        ImGui::Render();
        int width = 0;
        int height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
            std::fprintf(stderr, "Reading the drawable size failed: %s\n", SDL_GetError());
            break;
        }
        glViewport(0, 0, width, height);
        glClearColor(0.10F, 0.12F, 0.15F, 1.0F);
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
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
