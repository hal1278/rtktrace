# rtktrace

`rtktrace` is a family of native desktop applications for interactively visualizing GNSS positioning solutions.

The application targets are:

- **rtktrace light** — a fixed, compact interface for quickly opening and inspecting positioning results
- **rtktrace full** — a workspace-style interface that can create an arbitrary number of independent floating plot areas

The current implementation target is **rtktrace full**. The shared data-processing and plotting components have been validated through light, and full is being implemented as an extension using the same shared components.

The project goal is to provide the subset of RTKPLOT-like functionality required for inspecting positioning trajectories and solution quality, without reproducing the complete RTKPLOT feature set.

## Project status

The shared data, coordinate, relative-processing, plotting, and `rtktrace light` phases are
complete. The repository includes POS/NMEA workflows, revisioned ENU and relative caches,
Normal/Relative trajectory and time-series plotting, the fixed light application, and
backend-free regression tests.

No stable application interface, file-format compatibility guarantee, or release is currently available.

Phase 6 work currently provides the backend-free full application state and a full GUI runtime
boundary. Plot instances have monotonic IDs and independent plot components while sharing one
session and quality filter. The production `rtktrace-full` executable and its floating-window
composition are not yet available.

## Current implementation scope

The initial `rtktrace light` implementation is intended to support:

- RTKLIB/MRTKLIB-style position solution files
- NMEA position logs
- simultaneous display of multiple files
- horizontal trajectory plots in local East/North coordinates
- East, North, and Up time-series plots
- coloring by solution quality
- interactive pan and zoom
- axis ticks and grid display
- per-file visibility control
- per-quality visibility control

The expected initial workload is several files containing tens of thousands of points each, typically sampled at approximately 5 Hz over an area of several kilometers.

## Non-goals for the initial implementation

The initial implementation does not aim to provide:

- complete RTKPLOT compatibility
- positioning computation
- real-time receiver or network-stream input
- RINEX observation display
- RTCM decoding
- satellite sky plots
- observation, residual, or DOP plots
- map-tile backgrounds
- three-dimensional visualization
- trajectory editing
- project or workspace persistence
- browser-based or WebView-based execution

These items may be reconsidered after the initial plotting functionality and performance characteristics have been validated.

## Platform direction

The native target platform is x86-64 Linux. The build foundation also supports cross-compiling an x86-64 Windows executable from x86-64 Linux; Windows packaging and distribution remain future work.

The application is intended to run as a native desktop program and must not depend on Electron, WebView, or a browser runtime.

## Documentation

The project documentation is organized by concern:

- `docs/requirements.md`  
  Common functional requirements and application-specific requirements for rtktrace light and rtktrace full.

- `docs/data-specification.md`  
  Input formats and the shared semantics of time, coordinates, height, solution quality, reference comparison, and statistics.

- `docs/architecture.md`  
  Shared component boundaries, application targets, processing pipeline, implementation stack, dependency rules, and performance design.

- `docs/roadmap.md`  
  The light-first implementation sequence, the subsequent full application extension, dependencies, deliverables, and completion criteria.

These documents may be added incrementally as the corresponding decisions are finalized.

## Development policy

The implementation should prioritize:

1. correct interpretation of input data
2. interactive performance with multiple trajectories
3. clear separation between file parsing, data normalization, plotting, and application-specific window composition
4. reuse of the same data-processing and plotting components by rtktrace light and rtktrace full
5. limited implementation complexity
6. readable and maintainable code
7. portability between Linux and Windows where practical

Optimization should be based on measured bottlenecks. Complex level-of-detail processing or custom GPU rendering should not be introduced before the baseline implementation has been evaluated.

## Build foundation

The implementation language is C++20. A Nix flake fixes the build environment and dependency versions, Meson defines the `rtktrace` build, and Ninja executes it. The GUI stack is Dear ImGui with the official SDL3 platform backend and official OpenGL3 renderer backend, plus ImPlot. The renderer uses the loader embedded in `imgui_impl_opengl3.cpp`; no additional OpenGL loader is used.

Nix is the only dependency source. The project does not use Meson Wrap downloads or dependency fallbacks. CMake is not used to build `rtktrace` itself. An external dependency may use its own upstream build system, including CMake, inside its Nix derivation.

The canonical Linux native build is:

```shell
nix build .#rtktrace-light-linux
```

The canonical Windows x86-64 cross build is:

```shell
nix build .#rtktrace-light-windows
```

The Linux development environment provides the pinned sources through environment variables:

```shell
nix develop .#linux
meson setup build/linux \
  -Dimgui_source_dir="$IMGUI_SOURCE_DIR" \
  -Dimplot_source_dir="$IMPLOT_SOURCE_DIR"
meson compile -C build/linux
meson test -C build/linux
```

The Windows cross development environment additionally provides a generated Meson cross file. Cross-built tests are compiled but are not run on Linux:

```shell
nix develop .#windows
meson setup build/windows \
  --cross-file "$RTKTRACE_MESON_CROSS_FILE" \
  -Dimgui_source_dir="$IMGUI_SOURCE_DIR" \
  -Dimplot_source_dir="$IMPLOT_SOURCE_DIR"
meson compile -C build/windows
```

Run all sandbox build checks with:

```shell
nix flake check
```

The light executable is installed as `bin/rtktrace-light` for Linux and
`bin/rtktrace-light.exe` for Windows. Its OpenGL 3.3 core-profile request is a prototype choice,
not a permanent product minimum.

## License

Not yet determined.
