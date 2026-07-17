# plotcore

`plotcore` is a family of native desktop applications for interactively visualizing GNSS positioning solutions.

The application targets are:

- **plotcore light** — a fixed, compact interface for quickly opening and inspecting positioning results
- **plotcore full** — a workspace-style interface that can create an arbitrary number of independent floating plot areas

The current implementation target is **plotcore light**. After the shared data-processing and plotting components have been validated through light, **plotcore full** will be implemented as an extension using the same shared components.

The project goal is to provide the subset of RTKPLOT-like functionality required for inspecting positioning trajectories and solution quality, without reproducing the complete RTKPLOT feature set.

## Project status

Phase 1 shared-data implementation is complete. The repository currently includes the
normalized data model, UTC/GPST conversion, WGS 84 LLH/ECEF conversion, common time ranges,
sample-rate estimation, Recorded/Expected statistics, and POS/NMEA parsers with diagnostics.
Phase 2 coordinate processing is underway, with ENU reference selection, ECEF/ENU conversion,
revisioned ENU and common-time-range caches, and slot 1 reference matching implemented.

No stable application interface, file-format compatibility guarantee, or release is currently available.

The current executable remains a GUI smoke target only. It creates an SDL3/OpenGL window,
initializes Dear ImGui and ImPlot, and displays a fixed four-point sample plot. The shared data
components are not yet connected to file loading or the application-specific `plotcore light`
layout described below.

## Current implementation scope

The initial `plotcore light` implementation is intended to support:

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
  Common functional requirements and application-specific requirements for plotcore light and plotcore full.

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
4. reuse of the same data-processing and plotting components by plotcore light and plotcore full
5. limited implementation complexity
6. readable and maintainable code
7. portability between Linux and Windows where practical

Optimization should be based on measured bottlenecks. Complex level-of-detail processing or custom GPU rendering should not be introduced before the baseline implementation has been evaluated.

## Build foundation

The implementation language is C++20. A Nix flake fixes the build environment and dependency versions, Meson defines the `plotcore` build, and Ninja executes it. The GUI stack is Dear ImGui with the official SDL3 platform backend and official OpenGL3 renderer backend, plus ImPlot. The renderer uses the loader embedded in `imgui_impl_opengl3.cpp`; no additional OpenGL loader is used.

Nix is the only dependency source. The project does not use Meson Wrap downloads or dependency fallbacks. CMake is not used to build `plotcore` itself. An external dependency may use its own upstream build system, including CMake, inside its Nix derivation.

The canonical Linux native build is:

```bash
nix build .#plotcore-light-linux
```

The canonical Windows x86-64 cross build is:

```bash
nix build .#plotcore-light-windows
```

The Linux development environment provides the pinned sources through environment variables:

```bash
nix develop .#linux
meson setup build/linux \
  -Dimgui_source_dir="$IMGUI_SOURCE_DIR" \
  -Dimplot_source_dir="$IMPLOT_SOURCE_DIR"
meson compile -C build/linux
meson test -C build/linux
```

The Windows cross development environment additionally provides a generated Meson cross file. Cross-built tests are compiled but are not run on Linux:

```bash
nix develop .#windows
meson setup build/windows \
  --cross-file "$PLOTCORE_MESON_CROSS_FILE" \
  -Dimgui_source_dir="$IMGUI_SOURCE_DIR" \
  -Dimplot_source_dir="$IMPLOT_SOURCE_DIR"
meson compile -C build/windows
```

Run all sandbox build checks with:

```bash
nix flake check
```

The GUI smoke executable is installed as `bin/plotcore-light` for Linux and `bin/plotcore-light.exe` for Windows. Its OpenGL 3.3 core-profile request is a prototype choice, not a permanent product minimum.

## License

Not yet determined.
