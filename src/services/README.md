# Services Layer

This directory owns application-level orchestration: algorithm job startup,
cancellation, lifecycle state, progress polling, Easy3D-to-algorithm
conversion, result handoff, file-loading coordination, resource lookup, and
AI-generation backend calls.

UI code should ask services to run work. Algorithm code should remain focused on
computation and must not call back into UI widgets or windows.

Directory layout:

- `core/`: service-layer infrastructure such as `AlgorithmController`,
  `AlgorithmJobLifecycle`, `AlgorithmCompletionPolicy`, `AlgorithmId`,
  Easy3D/algorithm bridge helpers, and shared job-handle helpers.
- `jobs/cgal/`: CGAL-backed algorithm job facades compiled only when
  `CLAW3D_ENABLE_CGAL` is enabled.
- `jobs/easy3d/`: Easy3D-backed or Easy3D-first jobs, including geodesic front
  propagation and Poisson reconstruction.
- `operations/`: synchronous Easy3D model operations used by legacy menus and
  tools.
- `ai_generation/`: AI 3D generation service, client, job, and archive helpers.
- `io/`: application-level file loading orchestration.
- `resources/`: runtime resource path resolution.

Lifecycle rule:

- Every asynchronous algorithm uses the shared lifecycle states in
  `AlgorithmController`.
- Algorithm-specific params, preview events, result payloads, and debug stats
  stay in typed contracts and typed job handles.
- Dialogs may render typed preview data, but they should use lifecycle states
  such as `WorkerFinished`, `FlushingPreview`, `HoldingFinalPreview`, and
  `AwaitingUiCommit` for generic completion flow.
