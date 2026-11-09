#pragma once

// Console output helpers (UTF-8 JSON and paths).

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <string>

namespace cli {

inline void printJson(const nlohmann::json& j, std::ostream& out = std::cout)
{
    out << j.dump(2) << "\n";
}

inline std::string pathToUtf8(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

inline std::filesystem::path pathFromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

} // namespace cli
