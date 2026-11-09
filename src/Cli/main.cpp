// material-designer-cli: scriptable front-end, designed to be driven by tools such as Claude.
//
//   material-designer-cli list
//   material-designer-cli describe <material>
//   material-designer-cli generate <material> --out <dir> [options] [--json]
//
// Exit codes: 0 ok, 1 generic error, 2 invalid arguments, 3 unknown material,
// 4 invalid preset, 5 I/O error. Errors go to stderr (as JSON with --json).

#include "Commands.h"
#include "Output.h"

#include "md/core/Error.h"
#include "md/core/Version.h"

#include <CLI/CLI.hpp>

#include <windows.h>

#include <cstring>
#include <iostream>

namespace {

void printError(bool json, int code, const std::string& message)
{
    if (json) {
        cli::printJson({{"ok", false}, {"error", {{"code", code}, {"message", message}}}}, std::cerr);
    } else {
        std::cerr << "error: " << message << "\n";
    }
}

} // namespace

int wmain(int argc, wchar_t** wargv)
{
    SetConsoleOutputCP(CP_UTF8);

    // Convert the arguments to UTF-8 so non-ASCII paths work.
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(len > 0 ? len - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len, nullptr, nullptr);
        args.push_back(std::move(s));
    }
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());

    bool json = false;
    for (const auto& a : args) {
        if (a == "--json") json = true;
    }

    CLI::App app{"material-designer-cli " + std::string(md::version()) + " - procedural PBR texture generator"};
    app.set_version_flag("--version", std::string(md::version()));
    app.require_subcommand(1);
    app.fallthrough();
    app.add_flag("--json", json, "Machine-readable JSON output on stdout (errors as JSON on stderr)");

    auto* list = app.add_subcommand("list", "List available materials (JSON)");

    std::string describeMaterial;
    auto* describe = app.add_subcommand("describe", "Print the parameter schema of a material (JSON)");
    describe->add_option("material", describeMaterial, "Material id (see 'list')")->required();

    cli::GenerateArgs g;
    auto* generate = app.add_subcommand("generate", "Generate the texture maps of a material");
    generate->add_option("material", g.material, "Material id (see 'list')")->required();
    generate->add_option("-o,--out", g.outDir, "Output directory (created if needed)")->required();
    generate->add_option("--preset", g.preset, "Preset / settings JSON file to start from");
    generate->add_option("--set", g.sets, "Set a parameter: key=value (repeatable; see 'describe')")->allow_extra_args(false);
    generate->add_option("--seed", g.seed, "Random seed (0..4294967295)");
    generate->add_option("--size", g.size, "Resolution: power of two, 64..8192 (default 1024)");
    generate->add_flag("--seamless", g.seamless, "Make the texture tileable (default)");
    generate->add_flag("--no-seamless", g.noSeamless, "Do not make the texture tileable");
    generate->add_option("--wear", g.wear, "Enable wear with this amount (0..1)");
    generate->add_flag("--no-wear", g.noWear, "Disable wear");
    generate->add_flag("--wear-mask-only", g.maskOnly, "Produce the wear mask without applying wear to the other maps");
    generate->add_option("--maps", g.maps, "Maps to write: comma separated (albedo,normal,roughness,height,ao,wear) or 'all'");
    generate->add_option("--normal", g.normal, "Normal map convention: directx (default) or opengl");
    generate->add_option("--normal-strength", g.normalStrength, "Normal map strength multiplier (0..4, default 1)");

    try {
        app.parse(static_cast<int>(argv.size()), argv.data());
    } catch (const CLI::CallForHelp& e) {
        return app.exit(e);
    } catch (const CLI::CallForAllHelp& e) {
        return app.exit(e);
    } catch (const CLI::CallForVersion& e) {
        return app.exit(e);
    } catch (const CLI::ParseError& e) {
        printError(json, static_cast<int>(md::ErrorCode::InvalidArgument), e.what());
        return static_cast<int>(md::ErrorCode::InvalidArgument);
    }

    try {
        if (list->parsed()) return cli::runList(json);
        if (describe->parsed()) return cli::runDescribe(describeMaterial);
        if (generate->parsed()) return cli::runGenerate(g, json);
    } catch (const md::Error& e) {
        printError(json, static_cast<int>(e.code()), e.what());
        return static_cast<int>(e.code());
    } catch (const std::exception& e) {
        printError(json, static_cast<int>(md::ErrorCode::Generic), e.what());
        return static_cast<int>(md::ErrorCode::Generic);
    }
    return 0;
}
