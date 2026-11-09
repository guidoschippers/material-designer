# CLAUDE.md

Guidance for working on this repository (for Claude and humans alike).
All code, comments, UI text, docs and commit messages are in **English**.

## Build

Visual Studio 2026 (MSVC v145), C++20, x64 only. Dependencies come from vcpkg in manifest
mode (`vcpkg.json`); the MSBuild integration installs them automatically on first build
into `vcpkg_installed/` (triplet `x64-windows-static-md`: static libs, dynamic CRT). The vcpkg
bundled with Visual Studio is used unless `VCPKG_ROOT` is set.

```powershell
# Release / Debug builds (from the repository root)
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" material-designer.slnx /m /p:Configuration=Release /p:Platform=x64
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" material-designer.slnx /m /p:Configuration=Debug /p:Platform=x64

# Run the tests
bin\x64\Release\material-designer-tests.exe

# Or the wrapper script (locates MSBuild with vswhere); -Test also runs the tests
./build.ps1 -Configuration Release -Test
```

Binaries end up in `bin/x64/<Configuration>/`:
`material-designer.exe` (GUI), `material-designer-cli.exe`, `material-designer-tests.exe`.

Warnings are errors (`/W4 /WX`). Floating point model is `/fp:precise` on purpose: output must
be byte-identical between runs, the GUI and the CLI.

## Repository layout

| Path | Type | Purpose |
|------|------|---------|
| `src/Core` | static lib | All generation logic. No UI dependencies. |
| `src/App`  | Windows exe | GUI (Dear ImGui, DirectX 11, Win32). |
| `src/Cli`  | console exe | CLI for scripting / Claude. |
| `tests`    | console exe | Catch2 tests (unit tests + CLI smoke tests). |
| `build/Common.props` | | Compiler/linker settings shared by all projects. |

The `.vcxproj` files use wildcards (`src\**\*.cpp`), so new source files are picked up
without editing project files.

## Core architecture (`src/Core`)

Public headers live in `src/Core/include/md/...`, sources in `src/Core/src/...`.

- `md/core/Parameter.h`: `ParamSchema` / `ParamDef` (key, label, group, tooltip, type
  bool/int/float/enum/color, default, min/max, enum options, `wearOnly`, `enabledBy`),
  `ParamValue`, parsing (`parseParamValue`) and `ParamReader` (typed access with defaults).
- `md/core/Settings.h`: `CommonSettings` (size, seed, seamless, wear, wear amount, apply wear,
  map selection, normal convention/strength), `MapType`, `MaterialSettings`.
- `md/core/Material.h`: the `Material` interface. A material implements `id()`,
  `displayName()`, `description()`, `schema()`, `reliefDepth()` and `generateSurface()`
  (albedo, height, roughness, wear mask, extra occlusion). The non-virtual
  `Material::generate(settings)` runs the shared pipeline: surface -> normal map and AO derived
  from height -> `MaterialMaps`.
- `md/core/Registry.h`: `MaterialRegistry::builtin()`; registration happens in
  `src/Core/src/materials/BuiltinMaterials.cpp`.
- `md/core/Parallel.h`: `parallelFor` (all cores, deterministic, cancellable via `std::stop_token`).
- `md/noise/Noise.h`: tileable hash, value, Perlin, fBm, ridged, Voronoi (F1/F2 and exact
  border distance) and domain warping. All take a `Period`; see the header comment.
- `md/image/Filters.h`: wrap-aware blur, normal-from-height and AO-from-height.
- `md/io/Preset.h`: JSON presets (`settingsToJson`, `loadPreset`, ...), settings
  normalisation, `describeMaterial` (schema as JSON).
- `md/io/Export.h` / `ImageIO.h`: map encoding (albedo sRGB 8-bit, data maps linear, height
  16-bit) and PNG writing. 16-bit PNGs use a small built-in encoder on top of stb's zlib
  (stb_image_write only writes 8-bit PNG).

### Base materials and overlays

- **Base materials** (asphalt, ...) tile on both axes and produce the six standard maps.
- **Overlays** (`asphalt_edge`, `road_marking`) are meant to be blended over a base material.
  They override `outputMaps()` to add `MapType::Opacity` (written as `<material>_<seed>_opacity.png`,
  8-bit linear) and `tileAxes()` to tile on one axis only (edge strip: vertical) or not at all
  (single dash decal). Location-specific details (kerb edges, markings) belong in overlays, never
  in a seamless base texture.
- `asphalt_edge` reuses the asphalt surface model (`materials/AsphaltSurface.h`) with the same
  aggregate parameter keys, so with the same seed and aggregate settings its stones line up
  exactly with the base asphalt.

### Generation rules (keep these when writing materials)

- Work in UV space `[0, 1)`; never in pixels. Feature sizes are fractions of the texture so the
  low-resolution GUI preview looks like the full-resolution export.
- Every pixel must be a pure function of `(u, v, settings)`: use the hash-based noise, never a
  stateful RNG. Use `ctx.forEachPixel` for per-pixel work (parallel, cancellable).
- Seamless: sample noise at `u * f` with `noise::periodFor(f, f, ctx.seamless())` and an
  integer frequency `f` (`noise::frequency()`); periodic Voronoi cell ids are stable across the
  wrap. For distances across the tile edge use `periodicDelta`.
- Albedo is written in **linear** RGB; colour parameters are sRGB (`srgbToLinear`).
- Wear: always write the wear mask (`out.wear`) when `ctx.wearEnabled()`; only modify the base
  maps when `ctx.applyWear()`.

## CLI usage (`material-designer-cli.exe`)

The CLI is the scriptable interface; use it to discover options and generate textures without
reading code. `list` and `describe` always print JSON.

```powershell
material-designer-cli list                       # materials as JSON
material-designer-cli describe asphalt           # full parameter schema as JSON
material-designer-cli generate asphalt --out out --seed 1234 --size 2048 --json
material-designer-cli generate asphalt --out out --set aggregate_size=0.7 --set cracks=0.4 --wear 0.6
material-designer-cli generate asphalt --out out --preset out/asphalt_1234_settings.json --maps albedo,normal
```

`generate` options: `--out <dir>` (required), `--preset <file.json>`, `--set key=value`
(repeatable; bool `true/false`, numbers, enum option ids, colours `#rrggbb`), `--seed N`,
`--size N` (power of two 64..8192), `--seamless` / `--no-seamless`, `--wear <0..1>` / `--no-wear`,
`--wear-mask-only`, `--maps albedo,normal,roughness,height,ao,wear|all`,
`--normal directx|opengl`, `--normal-strength <0..4>`, `--json`.
Precedence: defaults < preset < `--set` < explicit flags.

Output: `<material>_<seed>_<map>.png` plus `<material>_<seed>_settings.json` (a preset that
reproduces the run exactly). Albedo is 8-bit sRGB, normal/roughness/AO/wear 8-bit linear, height
16-bit linear. With `--json` the result (files, formats, timings, warnings) is printed on stdout.

Exit codes: 0 ok, 1 generic error, 2 invalid arguments, 3 unknown material, 4 invalid preset,
5 I/O error. Errors go to stderr (as `{"ok": false, "error": {...}}` with `--json`).

## GUI (`src/App`)

- `MainWindow`: one tab per registered material, actions (seed, reset, presets, export), debounced
  preview regeneration (256 px while dragging, then the selected preview size).
- `SettingsPanel`: controls generated from the schema; wear-only parameters are disabled while
  wear is off; right-click resets a value.
- `PreviewPanel`: map selector, 2x2 tiling, zoom (wheel) and pan (drag). Uses the exporter's
  encoders, so the preview matches the PNGs.
- `GenerationWorker`: background generation with cancellation (std::jthread + stop_token).
- `AppSettings`: window placement, last folders, preview size, active tab and the last settings
  per material, stored in `%APPDATA%\material-designer\gui-settings.json` (written on exit).

## Reference material

`docs/reference-notes.md` describes the look the owner wants for each material (from photos).

## Adding a material

1. Create `src/Core/src/materials/<Name>.h/.cpp` with a class deriving from `md::Material`.
   Declare its parameters in the constructor (`schema_.addFloat(...).inGroup(...).tip(...)`;
   mark wear parameters with `.wear()`).
2. Add one `#include` and one `registry.add(std::make_unique<...>());` line in
   `src/Core/src/materials/BuiltinMaterials.cpp`.

That's all: the GUI tab, CLI options, presets and `describe` output are generated from the schema,
and the tests automatically cover every registered material.
