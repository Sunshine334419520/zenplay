# GitHub Copilot instructions for zenplay

Purpose: provide concise, actionable guidance so Copilot suggestions match this project's conventions, dependencies, and architecture.

How to suggest code
- Prefer small, incremental completions that match existing style (C++17, RAII, smart pointers).
- When suggesting changes to header files, update corresponding implementation `.cpp` files too.
- Keep public APIs stable; prefer adding new helpers over changing existing function signatures.

Project conventions
- Language: C++17. Use `std::unique_ptr` / `std::shared_ptr` where ownership semantics demand.
- Error handling: prefer returning `bool` for success/failure in public-facing simple APIs; propagate detailed errors via logging (`spdlog`) or optional `std::error_code` if needed.
- Threading: `third_party/loki` provides task runner/thread utilities—use them for background work instead of raw `std::thread` in most cases.
- Naming: follow existing CamelCase class names and snake_case for member variables ending with `_` (e.g., `demuxer_`).

Build & dependencies
- Build system: CMake (top-level `CMakeLists.txt`). Keep changes compatible with MSVC and cross-platform builds.
- Dependencies used: Qt6 (Core, Widgets, Gui), FFmpeg (avutil/avcodec/avformat/avfilter), spdlog, nlohmann_json, loki (vendored under `third_party/loki`).
- When adding new source files, ensure they are placed under `src/` and are discoverable by the project's existing globbing in `CMakeLists.txt` (`file(GLOB_RECURSE SRC_FILES "src/*.cpp" "src/*.h" "src/*.ui")`).

Media-related guidance
- Use FFmpeg types and functions only through thin wrappers already present (e.g., `Demuxer`, `VideoDecoder`, `AudioDecoder`). Avoid copying low-level FFmpeg code directly; instead extend or refactor wrappers.
- Decoding and rendering should be separated: decoding in worker threads, rendering on the UI/main thread or dedicated renderer thread.

Tests & CI
- Prefer adding unit tests for pure logic (e.g., timestamp calculations, demuxer behavior) using a C++ test framework (GoogleTest recommended). Keep tests small and deterministic.

Commit & PR guidance for Copilot suggestions
- If Copilot suggests multiple-file changes (headers + implementations + CMake), prefer creating a single commit that updates all affected files.
- For behavioral changes, include a short rationale comment in the top of the modified file explaining the why.

Examples of helpful completions
- Implementations for TODOs present in `src/player/` that follow existing patterns (open/close lifecycle, error cleanup, thread lifecycle management).
- Small refactors that remove duplicated cleanup code by extracting helpers.

Examples of bad completions to avoid
- Large API reshapes without providing migration stubs.
- Inline inclusion of external third-party code without attribution or license checks.

If you need to add new libraries
- Prefer header-only or widely-adopted libraries and update `CMakeLists.txt` accordingly.
- Document new external dependencies in this file and top-level `README.md`.

Contact
- For ambiguous design choices, open an issue describing the proposal rather than making large unilateral changes.

---
This file is intended for Copilot and contributors to improve suggestion quality. Keep it concise and update as the project evolves.
