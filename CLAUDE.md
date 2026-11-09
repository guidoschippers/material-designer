# CLAUDE.md

Guidance for working on this repository (for Claude and humans alike).
All code, comments, UI text, docs and commit messages are in **English**.

## Build

Visual Studio 2026 (MSVC v145), C++20, x64 only. Dependencies come from vcpkg in manifest
mode (`vcpkg.json`); the MSBuild integration installs them automatically on first build
into `vcpkg_installed/` (triplet `x64-windows-static-md`). The vcpkg bundled with Visual
Studio is used unless `VCPKG_ROOT` is set.

```powershell
# Release / Debug builds (from the repository root)
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" material-designer.slnx /m /p:Configuration=Release /p:Platform=x64
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" material-designer.slnx /m /p:Configuration=Debug /p:Platform=x64

# Or the wrapper script (locates MSBuild with vswhere); -Test also runs the tests
./build.ps1 -Configuration Release -Test
```

Binaries end up in `bin/x64/<Configuration>/`:
`material-designer.exe`, `material-designer-cli.exe`, `material-designer-tests.exe`.

## Architecture

| Project | Type | Purpose |
|---------|------|---------|
| `src/Core` | static lib | All generation logic. No UI dependencies. |
| `src/App`  | Windows exe | GUI (Dear ImGui, DirectX 11, Win32). |
| `src/Cli`  | console exe | CLI for scripting / Claude. |
| `tests`    | console exe | Catch2 tests. |

_To be completed in milestone 2._
