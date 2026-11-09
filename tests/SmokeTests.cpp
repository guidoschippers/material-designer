#include "md/core/Version.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Version string is not empty")
{
    REQUIRE_FALSE(md::version().empty());
}
