#include "md/core/Error.h"
#include "md/core/Parallel.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <stdexcept>
#include <vector>

TEST_CASE("parallelFor visits every index exactly once")
{
    std::vector<std::atomic<int>> hits(1000);
    md::parallelFor(1000, [&](int i) { hits[static_cast<std::size_t>(i)]++; });
    for (auto& h : hits) {
        REQUIRE(h.load() == 1);
    }
}

TEST_CASE("parallelFor stops and throws Cancelled when stop is requested")
{
    std::stop_source source;
    std::atomic<int> processed{0};
    REQUIRE_THROWS_AS(md::parallelFor(
                          100000,
                          [&](int i) {
                              if (i == 10) source.request_stop();
                              processed++;
                          },
                          source.get_token()),
                      md::Cancelled);
    REQUIRE(processed.load() < 100000);
}

TEST_CASE("parallelFor propagates exceptions")
{
    REQUIRE_THROWS_AS(md::parallelFor(100,
                                      [](int i) {
                                          if (i == 50) throw std::runtime_error("boom");
                                      }),
                      std::runtime_error);
}
