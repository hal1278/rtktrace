# plotcore

`plotcore` is a family of native desktop applications for interactively visualizing GNSS positioning solutions.

The application targets are:

- **plotcore light** — a fixed, compact interface for quickly opening and inspecting positioning results
- **plotcore full** — a workspace-style interface that can create an arbitrary number of independent floating plot areas

The current implementation target is **plotcore light**. After the shared data-processing and plotting components have been validated through light, **plotcore full** will be implemented as an extension using the same shared components.

The project goal is to provide the subset of RTKPLOT-like functionality required for inspecting positioning trajectories and solution quality, without reproducing the complete RTKPLOT feature set.

## Project status

Requirements definition and architecture planning.

No stable application interface, file-format compatibility guarantee, or release is currently available.

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

The initial target platform is Linux.

The design should not prevent later support for Windows.

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

## License

Not yet determined.
