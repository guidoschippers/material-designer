#pragma once

#include <string_view>

namespace md {

/// Application version, shared by the GUI, the CLI and the preset files.
std::string_view version();

} // namespace md
