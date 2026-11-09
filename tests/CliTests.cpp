// Smoke tests for material-designer-cli.exe (expected next to the test executable).

#include "md/core/Registry.h"
#include "md/io/Export.h"
#include "md/io/Preset.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using nlohmann::json;

namespace {

std::filesystem::path cliPath()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path() / L"material-designer-cli.exe";
}

struct RunResult {
    int exitCode = -1;
    std::string out;
};

RunResult runCli(const std::wstring& args)
{
    // cmd.exe strips the outer quotes, so the whole command is wrapped once more.
    const std::wstring cmd = L"\"\"" + cliPath().wstring() + L"\" " + args + L" 2>NUL\"";
    RunResult r;
    FILE* pipe = _wpopen(cmd.c_str(), L"rb");
    REQUIRE(pipe);
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) {
        r.out.append(buf, n);
    }
    r.exitCode = _pclose(pipe);
    return r;
}

std::wstring widen(std::string_view s)
{
    return std::wstring(s.begin(), s.end());
}

std::string readFile(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::filesystem::path tempDir(const std::string& name)
{
    const auto dir = std::filesystem::temp_directory_path() / ("md_cli_test_" + name);
    std::filesystem::remove_all(dir);
    return dir;
}

} // namespace

TEST_CASE("CLI executable exists")
{
    REQUIRE(std::filesystem::exists(cliPath()));
}

TEST_CASE("CLI list returns every material as JSON")
{
    const RunResult r = runCli(L"list");
    REQUIRE(r.exitCode == 0);
    const json j = json::parse(r.out);
    REQUIRE(j["materials"].size() == md::MaterialRegistry::builtin().all().size());
    for (const auto& m : md::MaterialRegistry::builtin().all()) {
        bool found = false;
        for (const auto& item : j["materials"]) found = found || item["id"] == std::string(m->id());
        CHECK(found);
    }
}

TEST_CASE("CLI describe returns the schema of every material")
{
    for (const auto& m : md::MaterialRegistry::builtin().all()) {
        INFO("material: " << m->id());
        const RunResult r = runCli(L"describe " + widen(m->id()));
        REQUIRE(r.exitCode == 0);
        const json j = json::parse(r.out);
        CHECK(j["id"] == std::string(m->id()));
        CHECK(j["parameters"].size() == m->schema().params().size());
    }
}

TEST_CASE("CLI generate works for every material")
{
    for (const auto& m : md::MaterialRegistry::builtin().all()) {
        INFO("material: " << m->id());
        const auto dir = tempDir(std::string(m->id()));
        const RunResult r = runCli(L"generate " + widen(m->id()) + L" --out \"" + dir.wstring() +
                                   L"\" --size 64 --seed 99 --wear 0.5 --json");
        REQUIRE(r.exitCode == 0);
        const json j = json::parse(r.out);
        CHECK(j["ok"] == true);
        CHECK(j["files"].size() == m->outputMaps().list().size());
        for (const auto& f : j["files"]) {
            const std::string path = f["path"].get<std::string>();
            CHECK(std::filesystem::exists(std::filesystem::path(std::u8string(path.begin(), path.end()))));
        }
        CHECK(std::filesystem::exists(dir / (std::string(m->id()) + "_99_settings.json")));
        CHECK(std::filesystem::exists(dir / (std::string(m->id()) + "_99_albedo.png")));
        std::filesystem::remove_all(dir);
    }
}

TEST_CASE("CLI output matches in-process generation and its settings file reproduces it")
{
    const md::Material& m = *md::MaterialRegistry::builtin().all().front();
    const std::string id(m.id());
    const auto dir = tempDir("match");
    const RunResult r = runCli(L"generate " + widen(id) + L" --out \"" + dir.wstring() +
                               L"\" --size 64 --seed 7 --maps albedo,height");
    REQUIRE(r.exitCode == 0);

    // Same settings through the library (the GUI's code path).
    md::MaterialSettings s = md::defaultSettings(m);
    s.common.size = 64;
    s.common.seed = 7;
    s.common.maps = md::MapSet();
    s.common.maps.set(md::MapType::Albedo, true);
    s.common.maps.set(md::MapType::Height, true);
    const auto lib = tempDir("match_lib");
    md::exportMaps(m.generate(s), s, lib);
    CHECK(readFile(dir / (id + "_7_albedo.png")) == readFile(lib / (id + "_7_albedo.png")));
    CHECK(readFile(dir / (id + "_7_height.png")) == readFile(lib / (id + "_7_height.png")));

    // Re-running from the written settings file gives identical files.
    const auto again = tempDir("match_again");
    const RunResult r2 = runCli(L"generate " + widen(id) + L" --out \"" + again.wstring() + L"\" --preset \"" +
                                (dir / (id + "_7_settings.json")).wstring() + L"\"");
    REQUIRE(r2.exitCode == 0);
    CHECK(readFile(dir / (id + "_7_albedo.png")) == readFile(again / (id + "_7_albedo.png")));

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(lib);
    std::filesystem::remove_all(again);
}

TEST_CASE("CLI reports errors with exit codes")
{
    CHECK(runCli(L"describe no-such-material").exitCode == 3);
    CHECK(runCli(L"generate asphalt --out x --set nope=1").exitCode == 2);
    CHECK(runCli(L"generate asphalt --out x --size 1000").exitCode == 2);
    CHECK(runCli(L"generate asphalt").exitCode == 2); // missing --out
    CHECK(runCli(L"generate asphalt --out x --preset does-not-exist.json").exitCode == 5);
}
