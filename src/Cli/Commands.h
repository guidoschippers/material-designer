#pragma once

// Implementation of the CLI sub-commands. Each returns the process exit code.

#include <optional>
#include <string>
#include <vector>

namespace cli {

struct GenerateArgs {
    std::string material;
    std::string outDir;
    std::string preset;
    std::vector<std::string> sets;          ///< "key=value" items
    std::optional<unsigned long long> seed;
    std::optional<int> size;
    bool seamless = false;
    bool noSeamless = false;
    std::optional<double> wear;             ///< enables wear with this amount
    bool noWear = false;
    bool maskOnly = false;                  ///< wear mask without applying wear to the base maps
    std::string maps;                       ///< comma separated ids or "all"
    std::string normal;                     ///< directx | opengl
    std::optional<double> normalStrength;
};

int runList(bool json);
int runDescribe(const std::string& material);
int runGenerate(const GenerateArgs& args, bool json);

} // namespace cli
