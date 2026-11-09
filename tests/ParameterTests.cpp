#include "TestMaterial.h"

#include "md/core/Parameter.h"

#include <catch2/catch_test_macros.hpp>

using namespace md;

TEST_CASE("Colours round-trip through hex")
{
    const auto c = colorFromHex("#ff8000");
    REQUIRE(c);
    REQUIRE(colorToHex(*c) == "#ff8000");
    REQUIRE(colorFromHex("12ab3c"));
    REQUIRE_FALSE(colorFromHex("#12ab3"));
    REQUIRE_FALSE(colorFromHex("#gg0000"));
}

TEST_CASE("Parameter values are parsed according to their type")
{
    const mdtest::TestMaterial m;
    const ParamSchema& s = m.schema();
    std::string error;

    auto v = parseParamValue(*s.find("scale"), "2.5", &error);
    REQUIRE(v);
    REQUIRE(std::get<double>(*v) == 2.5);

    REQUIRE(std::get<int>(*parseParamValue(*s.find("octaves"), "5", &error)) == 5);
    REQUIRE_FALSE(parseParamValue(*s.find("octaves"), "5.5", &error));
    REQUIRE(std::get<bool>(*parseParamValue(*s.find("invert"), "on", &error)));
    REQUIRE(std::get<std::string>(*parseParamValue(*s.find("palette"), "BLUE", &error)) == "blue");
    REQUIRE_FALSE(parseParamValue(*s.find("palette"), "green", &error));
    REQUIRE(error.find("valid: red, blue") != std::string::npos);
    REQUIRE(parseParamValue(*s.find("tint"), "#102030", &error));
}

TEST_CASE("Out-of-range values are clamped with a warning")
{
    const mdtest::TestMaterial m;
    ParamValue v = 100.0;
    std::vector<std::string> warnings;
    REQUIRE(sanitizeValue(*m.schema().find("scale"), v, &warnings));
    REQUIRE(std::get<double>(v) == 16.0);
    REQUIRE(warnings.size() == 1);

    ParamValue wrongType = std::string("x");
    REQUIRE_FALSE(sanitizeValue(*m.schema().find("scale"), wrongType, &warnings));
}

TEST_CASE("Schema validation detects problems")
{
    REQUIRE(mdtest::TestMaterial().schema().validate().empty());

    ParamSchema bad;
    bad.addFloat("a", "A", 2.0, 0.0, 1.0).inGroup("G").tip("t");      // default out of range
    bad.addFloat("a", "A2", 0.5, 0.0, 1.0).inGroup("G").tip("t");     // duplicate key
    bad.addFloat("BadKey", "B", 0.5, 0.0, 1.0).inGroup("G").tip("t"); // not snake_case
    bad.addEnum("e", "E", {{"x", "X"}}, "y").inGroup("G").tip("t");   // default not an option
    bad.addBool("b", "B", true).enabledIf("missing");                  // no group/tooltip, bad dependency
    REQUIRE(bad.validate().size() >= 6);
}

TEST_CASE("ParamReader falls back to defaults")
{
    const mdtest::TestMaterial m;
    ParamValues values{{"scale", 8.0}, {"palette", std::string("blue")}};
    const ParamReader r(m.schema(), values);
    REQUIRE(r.getFloat("scale") == 8.0f);
    REQUIRE(r.getInt("octaves") == 3);
    REQUIRE(r.getEnum("palette") == "blue");
    REQUIRE(r.getEnumIndex("palette") == 1);
    REQUIRE(r.getColor("tint") == Color{0.4f, 0.2f, 1.0f});
    REQUIRE_THROWS(r.getInt("scale"));
}
