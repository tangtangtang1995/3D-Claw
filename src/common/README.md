# Common Contracts

This directory is reserved for small shared contracts only: lightweight enums,
handles, POD DTOs, and error/status types that need to be visible across UI,
services, algorithms, or IO.

It must not grow into a business layer. Keep it free of ImGui, GLFW, CGAL,
renderer objects, file IO, and algorithm implementation details.
