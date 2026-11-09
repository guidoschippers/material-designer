#include "md/core/Parameter.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>

namespace md {

namespace {

std::string toLower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string formatNumber(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

} // namespace

std::string_view paramTypeName(ParamType type)
{
    switch (type) {
    case ParamType::Bool: return "bool";
    case ParamType::Int: return "int";
    case ParamType::Float: return "float";
    case ParamType::Enum: return "enum";
    case ParamType::Color: return "color";
    }
    return "unknown";
}

Color quantizeColor(Color c)
{
    auto q = [](float v) { return static_cast<float>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)) / 255.0f; };
    return Color{q(c.r), q(c.g), q(c.b)};
}

std::string colorToHex(Color c)
{
    auto to8 = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", to8(c.r), to8(c.g), to8(c.b));
    return buf;
}

std::optional<Color> colorFromHex(std::string_view text)
{
    text = trim(text);
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        return std::nullopt;
    }
    unsigned int value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return Color{static_cast<float>((value >> 16) & 0xff) / 255.0f, static_cast<float>((value >> 8) & 0xff) / 255.0f,
                 static_cast<float>(value & 0xff) / 255.0f};
}

ParamValue ParamDef::defaultParamValue() const
{
    switch (type) {
    case ParamType::Bool: return defaultValue != 0.0;
    case ParamType::Int: return static_cast<int>(defaultValue);
    case ParamType::Float: return defaultValue;
    case ParamType::Enum: return defaultOption;
    case ParamType::Color: return quantizeColor(defaultColor);
    }
    return defaultValue;
}

int ParamDef::optionIndex(std::string_view id) const
{
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

ParamDef& ParamSchema::add(ParamDef def)
{
    params_.push_back(std::move(def));
    return params_.back();
}

ParamDef& ParamSchema::addBool(std::string key, std::string label, bool def)
{
    ParamDef d;
    d.key = std::move(key);
    d.label = std::move(label);
    d.type = ParamType::Bool;
    d.defaultValue = def ? 1.0 : 0.0;
    d.minValue = 0.0;
    d.maxValue = 1.0;
    return add(std::move(d));
}

ParamDef& ParamSchema::addInt(std::string key, std::string label, int def, int min, int max)
{
    ParamDef d;
    d.key = std::move(key);
    d.label = std::move(label);
    d.type = ParamType::Int;
    d.defaultValue = def;
    d.minValue = min;
    d.maxValue = max;
    return add(std::move(d));
}

ParamDef& ParamSchema::addFloat(std::string key, std::string label, double def, double min, double max)
{
    ParamDef d;
    d.key = std::move(key);
    d.label = std::move(label);
    d.type = ParamType::Float;
    d.defaultValue = def;
    d.minValue = min;
    d.maxValue = max;
    return add(std::move(d));
}

ParamDef& ParamSchema::addEnum(std::string key, std::string label, std::vector<EnumOption> options, std::string def)
{
    ParamDef d;
    d.key = std::move(key);
    d.label = std::move(label);
    d.type = ParamType::Enum;
    d.options = std::move(options);
    d.defaultOption = std::move(def);
    d.minValue = 0;
    d.maxValue = static_cast<double>(d.options.size()) - 1.0;
    return add(std::move(d));
}

ParamDef& ParamSchema::addColor(std::string key, std::string label, Color def)
{
    ParamDef d;
    d.key = std::move(key);
    d.label = std::move(label);
    d.type = ParamType::Color;
    d.defaultColor = def;
    return add(std::move(d));
}

const ParamDef* ParamSchema::find(std::string_view key) const
{
    for (const ParamDef& d : params_) {
        if (d.key == key) {
            return &d;
        }
    }
    return nullptr;
}

ParamValues ParamSchema::defaults() const
{
    ParamValues values;
    for (const ParamDef& d : params_) {
        values[d.key] = d.defaultParamValue();
    }
    return values;
}

std::vector<std::string> ParamSchema::validate() const
{
    std::vector<std::string> problems;
    std::set<std::string> keys;
    for (const ParamDef& d : params_) {
        const std::string where = "parameter '" + d.key + "': ";
        if (d.key.empty()) {
            problems.push_back("parameter with empty key");
        }
        for (char c : d.key) {
            if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_')) {
                problems.push_back(where + "key must be snake_case");
                break;
            }
        }
        if (!keys.insert(d.key).second) {
            problems.push_back(where + "duplicate key");
        }
        if (d.label.empty()) problems.push_back(where + "missing label");
        if (d.group.empty()) problems.push_back(where + "missing group");
        if (d.tooltip.empty()) problems.push_back(where + "missing tooltip");

        switch (d.type) {
        case ParamType::Int:
        case ParamType::Float:
            if (!(d.minValue < d.maxValue)) problems.push_back(where + "min must be < max");
            if (d.defaultValue < d.minValue || d.defaultValue > d.maxValue) problems.push_back(where + "default outside [min, max]");
            if (d.type == ParamType::Int && d.defaultValue != std::floor(d.defaultValue)) problems.push_back(where + "non-integer default");
            break;
        case ParamType::Enum:
            if (d.options.empty()) problems.push_back(where + "enum without options");
            if (d.optionIndex(d.defaultOption) < 0) problems.push_back(where + "default option not in options");
            break;
        case ParamType::Bool:
        case ParamType::Color:
            break;
        }
        if (!d.enabledBy.empty()) {
            const ParamDef* dep = find(d.enabledBy);
            if (!dep || dep->type != ParamType::Bool) {
                problems.push_back(where + "enabledBy must reference a bool parameter");
            }
        }
    }
    return problems;
}

bool sanitizeValue(const ParamDef& def, ParamValue& value, std::vector<std::string>* warnings)
{
    auto warn = [&](const std::string& msg) {
        if (warnings) warnings->push_back("parameter '" + def.key + "': " + msg);
    };
    switch (def.type) {
    case ParamType::Bool:
        if (std::holds_alternative<bool>(value)) return true;
        if (std::holds_alternative<int>(value)) { value = std::get<int>(value) != 0; return true; }
        return false;
    case ParamType::Int: {
        double v;
        if (std::holds_alternative<int>(value)) v = std::get<int>(value);
        else if (std::holds_alternative<double>(value)) v = std::get<double>(value);
        else return false;
        const double clamped = std::clamp(std::round(v), def.minValue, def.maxValue);
        if (clamped != v) warn(formatNumber(v) + " clamped to " + formatNumber(clamped));
        value = static_cast<int>(clamped);
        return true;
    }
    case ParamType::Float: {
        double v;
        if (std::holds_alternative<double>(value)) v = std::get<double>(value);
        else if (std::holds_alternative<int>(value)) v = std::get<int>(value);
        else return false;
        if (!std::isfinite(v)) return false;
        const double clamped = std::clamp(v, def.minValue, def.maxValue);
        if (clamped != v) warn(formatNumber(v) + " clamped to " + formatNumber(clamped));
        value = clamped;
        return true;
    }
    case ParamType::Enum:
        return std::holds_alternative<std::string>(value) && def.optionIndex(std::get<std::string>(value)) >= 0;
    case ParamType::Color: {
        if (!std::holds_alternative<Color>(value)) return false;
        Color& c = std::get<Color>(value);
        c = quantizeColor(c);
        return true;
    }
    }
    return false;
}

std::optional<ParamValue> parseParamValue(const ParamDef& def, std::string_view text, std::string* error)
{
    auto fail = [&](const std::string& msg) -> std::optional<ParamValue> {
        if (error) *error = "parameter '" + def.key + "': " + msg;
        return std::nullopt;
    };
    text = trim(text);
    switch (def.type) {
    case ParamType::Bool: {
        const std::string t = toLower(text);
        if (t == "true" || t == "1" || t == "yes" || t == "on") return ParamValue{true};
        if (t == "false" || t == "0" || t == "no" || t == "off") return ParamValue{false};
        return fail("expected true/false, got '" + std::string(text) + "'");
    }
    case ParamType::Int: {
        int v = 0;
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
        if (ec != std::errc{} || ptr != text.data() + text.size()) return fail("expected an integer, got '" + std::string(text) + "'");
        return ParamValue{v};
    }
    case ParamType::Float: {
        double v = 0;
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
        if (ec != std::errc{} || ptr != text.data() + text.size() || !std::isfinite(v)) return fail("expected a number, got '" + std::string(text) + "'");
        return ParamValue{v};
    }
    case ParamType::Enum: {
        const std::string t = toLower(text);
        if (def.optionIndex(t) >= 0) return ParamValue{t};
        std::string valid;
        for (const EnumOption& o : def.options) valid += (valid.empty() ? "" : ", ") + o.id;
        return fail("unknown option '" + std::string(text) + "' (valid: " + valid + ")");
    }
    case ParamType::Color: {
        if (auto c = colorFromHex(text)) return ParamValue{*c};
        return fail("expected a colour as #rrggbb, got '" + std::string(text) + "'");
    }
    }
    return fail("unsupported type");
}

std::string formatParamValue(const ParamValue& value)
{
    struct Visitor {
        std::string operator()(bool v) const { return v ? "true" : "false"; }
        std::string operator()(int v) const { return std::to_string(v); }
        std::string operator()(double v) const { return formatNumber(v); }
        std::string operator()(const std::string& v) const { return v; }
        std::string operator()(Color c) const { return colorToHex(c); }
    };
    return std::visit(Visitor{}, value);
}

ParamReader::ParamReader(const ParamSchema& schema, const ParamValues& values) : schema_(schema)
{
    for (const ParamDef& d : schema.params()) {
        ParamValue v = d.defaultParamValue();
        if (auto it = values.find(d.key); it != values.end()) {
            ParamValue candidate = it->second;
            if (sanitizeValue(d, candidate, nullptr)) {
                v = std::move(candidate);
            }
        }
        values_[d.key] = std::move(v);
    }
}

const ParamValue& ParamReader::get(std::string_view key, ParamType expected) const
{
    const ParamDef* def = schema_.find(key);
    if (!def || def->type != expected) {
        throw std::logic_error("ParamReader: unknown parameter or wrong type: " + std::string(key));
    }
    return values_.find(def->key)->second;
}

bool ParamReader::getBool(std::string_view key) const { return std::get<bool>(get(key, ParamType::Bool)); }
int ParamReader::getInt(std::string_view key) const { return std::get<int>(get(key, ParamType::Int)); }
float ParamReader::getFloat(std::string_view key) const
{
    return static_cast<float>(std::get<double>(get(key, ParamType::Float)));
}
const std::string& ParamReader::getEnum(std::string_view key) const { return std::get<std::string>(get(key, ParamType::Enum)); }
int ParamReader::getEnumIndex(std::string_view key) const
{
    return schema_.find(key)->optionIndex(getEnum(key));
}
Color ParamReader::getColor(std::string_view key) const { return std::get<Color>(get(key, ParamType::Color)); }

} // namespace md
