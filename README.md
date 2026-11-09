# material-designer

Procedural PBR texture generator for Windows. Generates tileable material textures
(albedo, normal, roughness, height, ambient occlusion and a wear mask) from a small set
of meaningful parameters.

Two front-ends share one generation core:

- **`material-designer.exe`**: interactive GUI (Dear ImGui + DirectX 11) with live preview.
- **`material-designer-cli.exe`**: scriptable, deterministic command-line tool with JSON output.

> Status: under construction.

## Building

Requirements: Visual Studio 2026 with the *Desktop development with C++* workload
(MSVC v145 toolset and the bundled *vcpkg package manager*).

```powershell
./build.ps1 -Configuration Release -Test
```

See [CLAUDE.md](CLAUDE.md) for the raw MSBuild commands and an architecture overview.

## License

[MIT](LICENSE)
