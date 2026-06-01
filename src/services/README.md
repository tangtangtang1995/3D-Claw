# Services Layer

This directory owns application-level orchestration: algorithm job startup,
cancellation, progress polling, Easy3D-to-algorithm conversion, result handoff,
file-loading coordination, resource lookup, and AI-generation backend calls.

UI code should ask services to run work. Algorithm code should remain focused on
computation and must not call back into UI widgets or windows.

Directory layout:

- `core/`: service-layer infrastructure such as `AlgorithmController`,
  `AlgorithmId`, Easy3D/algorithm bridge helpers, and shared job-handle helpers.
- `jobs/cgal/`: CGAL-backed algorithm job facades compiled only when
  `CLAW3D_ENABLE_CGAL` is enabled.
- `jobs/easy3d/`: Easy3D-backed or Easy3D-first jobs, including geodesic front
  propagation and Poisson reconstruction.
- `operations/`: synchronous Easy3D model operations used by legacy menus and
  tools.
- `ai_generation/`: AI 3D generation service, client, job, and archive helpers.
- `io/`: application-level file loading orchestration.
- `resources/`: runtime resource path resolution.