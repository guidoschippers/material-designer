#pragma once

// Parameter schema: the single description of a material's settings. The GUI controls, the CLI
// options, the JSON presets and the `describe` output are all derived from it.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace md {

enum class ParamType { Bool, Int, Float, Enum, Color };

std::string_view paramTypeName(ParamType type);

/// sRGB colour with components in [0, 1].
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    bool operator==(const Color&) const = default;
};

/// Formats a colour as "#rrggbb".
std::string colorToHex(Color c);
/// Parses "#rrggbb" or "rrggbb". Returns nullopt on malformed input.
std::optional<Color> colorFromHex(std::string_view text);

/// Value of a single parameter. The alternative used depends on the ParamType:
/// Bool -> bool, Int -> int, Float -> double, Enum -> std::string (option id), Color -> Color.
using ParamValue = std::variant<bool, int, double, std::string, Color>;

/// Parameter values keyed by parameter key (ordered, so serialisation is deterministic).
using ParamValues = std::map<std::string, ParamValue>;

struct EnumOption {
    std::string id;    ///< Stable identifier used in presets and on the CLI, e.g. "light".
    std::string label; ///< Display text, e.g. "Light (pine)".
};

/// Description of one material parameter.
struct ParamDef {
    std::string key;     ///< Stable snake_case identifier, e.g. "aggregate_size".
    std::string label;   ///< Display name.
    std::string group;   ///< Section the control is shown in, e.g. "Aggregate".
    std::string tooltip; ///< One or two sentences explaining the effect.
    ParamType type = ParamType::Float;

    double defaultValue = 0.0; ///< Bool (0/1), Int and Float default.
    double minValue = 0.0;     ///< Int/Float lower bound (inclusive).
    double maxValue = 1.0;     ///< Int/Float upper bound (inclusive).
    std::vector<EnumOption> options; ///< Enum options.
    std::string defaultOption;       ///< Enum default (option id).
    Color defaultColor;              ///< Color default (sRGB).

    bool wearOnly = false;  ///< Only has an effect when wear is enabled.
    std::string enabledBy;  ///< Key of a Bool parameter that must be true for this one to matter.

    // Fluent setters used when declaring schemas.
    ParamDef& inGroup(std::string g) { group = std::move(g); return *this; }
    ParamDef& tip(std::string t) { tooltip = std::move(t); return *this; }
    ParamDef& wear() { wearOnly = true; return *this; }
    ParamDef& enabledIf(std::string boolKey) { enabledBy = std::move(boolKey); return *this; }

    /// Default value as a ParamValue.
    ParamValue defaultParamValue() const;
    /// Index of an enum option id, or -1.
    int optionIndex(std::string_view id) const;
};

/// Ordered list of parameter definitions. Order defines the UI order.
class ParamSchema {
public:
    ParamDef& addBool(std::string key, std::string label, bool def);
    ParamDef& addInt(std::string key, std::string label, int def, int min, int max);
    ParamDef& addFloat(std::string key, std::string label, double def, double min, double max);
    ParamDef& addEnum(std::string key, std::string label, std::vector<EnumOption> options, std::string def);
    ParamDef& addColor(std::string key, std::string label, Color def);

    const std::vector<ParamDef>& params() const { return params_; }
    const ParamDef* find(std::string_view key) const;

    /// All parameters set to their default values.
    ParamValues defaults() const;

    /// Checks the schema itself (unique keys, defaults within range, ...). Returns problems found.
    std::vector<std::string> validate() const;

private:
    ParamDef& add(ParamDef def);
    std::vector<ParamDef> params_;
};

/// Checks that `value` has the right alternative for `def` and lies within range.
/// Out-of-range numbers are clamped (a warning is appended); wrong types return false.
bool sanitizeValue(const ParamDef& def, ParamValue& value, std::vector<std::string>* warnings);

/// Parses a textual value (CLI `--set key=value`) according to the parameter type.
/// Accepted forms: bool "true/false/1/0/yes/no/on/off", numbers, enum option ids, colours "#rrggbb".
std::optional<ParamValue> parseParamValue(const ParamDef& def, std::string_view text, std::string* error);

/// Human-readable representation of a value (used in messages and the CLI).
std::string formatParamValue(const ParamValue& value);

/// Typed, fully resolved access to parameter values for generators.
/// Missing values fall back to the schema defaults. Accessing an unknown key or the wrong type
/// is a programming error and throws std::logic_error.
class ParamReader {
public:
    ParamReader(const ParamSchema& schema, const ParamValues& values);

    bool getBool(std::string_view key) const;
    int getInt(std::string_view key) const;
    float getFloat(std::string_view key) const;
    /// Enum option id.
    const std::string& getEnum(std::string_view key) const;
    /// Enum option index (order of declaration).
    int getEnumIndex(std::string_view key) const;
    Color getColor(std::string_view key) const;

private:
    const ParamValue& get(std::string_view key, ParamType expected) const;

    const ParamSchema& schema_;
    ParamValues values_;
};

} // namespace md
