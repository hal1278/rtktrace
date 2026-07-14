# plotcore

`plotcore` is a native desktop application for interactively visualizing GNSS positioning solutions.

The initial goal is to provide the subset of RTKPLOT-like functionality required for inspecting positioning trajectories and solution quality, without reproducing the complete RTKPLOT feature set.

## Project status

Requirements definition and architecture planning.

No stable application interface, file-format compatibility guarantee, or release is currently available.

## Initial scope

The initial implementation is intended to support:

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

The project documentation will be organized by concern:

- `docs/requirements.md`  
  Functional requirements, non-functional requirements, and acceptance criteria.

- `docs/data-specification.md`  
  Supported input formats and the semantics of time, coordinates, height, solution quality, and trajectory segmentation.

- `docs/architecture.md`  
  Processing pipeline, module responsibilities, implementation stack, dependency boundaries, and performance design.

- `docs/roadmap.md`  
  Development phases, dependencies, deliverables, and completion criteria.

These documents may be added incrementally as the corresponding decisions are finalized.

## Development policy

The implementation should prioritize:

1. correct interpretation of input data
2. interactive performance with multiple trajectories
3. clear separation between file parsing, data normalization, and plotting
4. limited implementation complexity
5. readable and maintainable code
6. portability between Linux and Windows where practical

Optimization should be based on measured bottlenecks. Complex level-of-detail processing or custom GPU rendering should not be introduced before the baseline implementation has been evaluated.

## License

Not yet determined.
