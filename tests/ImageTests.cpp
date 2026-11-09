#include "TestMaterial.h"

#include "md/image/Filters.h"
#include "md/io/Export.h"
#include "md/io/ImageIO.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

using namespace md;
using Catch::Matchers::WithinAbs;

TEST_CASE("16-bit greyscale PNG round-trips losslessly")
{
    const int w = 37, h = 23; // odd sizes exercise the filters
    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint16_t>((i * 2654435761u) >> 7);
    }
    const auto png = encodePng16Gray(w, h, pixels.data());

    int rw = 0, rh = 0, channels = 0;
    stbi_us* decoded = stbi_load_16_from_memory(png.data(), static_cast<int>(png.size()), &rw, &rh, &channels, 1);
    REQUIRE(decoded);
    REQUIRE(rw == w);
    REQUIRE(rh == h);
    REQUIRE(channels == 1);
    REQUIRE(std::equal(pixels.begin(), pixels.end(), decoded));
    stbi_image_free(decoded);
}

TEST_CASE("Box blur with wrap preserves the mean")
{
    ImageF img(32, 32, 1);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            img.at(x, y) = (x * 7 + y * 13) % 5 == 0 ? 1.0f : 0.0f;
        }
    }
    double before = 0, after = 0;
    const ImageF blurred = boxBlur(img, 3, true);
    for (float v : img.data()) before += v;
    for (float v : blurred.data()) after += v;
    REQUIRE_THAT(after, WithinAbs(before, 1e-3));
}

TEST_CASE("A flat height map gives flat normals and no occlusion")
{
    const ImageF flat(64, 64, 1, 0.5f);
    const ImageF n = normalFromHeight(flat, 0.01f, true, NormalConvention::DirectX);
    REQUIRE_THAT(n.at(10, 10, 2), WithinAbs(1.0, 1e-6));
    const ImageF ao = ambientOcclusion(flat, 0.01f, true);
    REQUIRE_THAT(ao.at(5, 5), WithinAbs(1.0, 1e-6));
}

TEST_CASE("Normal conventions differ only in the green channel")
{
    ImageF ramp(64, 64, 1);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            ramp.at(x, y) = static_cast<float>(x + y) / 128.0f;
        }
    }
    const ImageF dx = normalFromHeight(ramp, 0.01f, false, NormalConvention::DirectX);
    const ImageF gl = normalFromHeight(ramp, 0.01f, false, NormalConvention::OpenGL);
    REQUIRE(dx.at(20, 20, 0) < 0.0f); // height rises to the right -> normal leans left
    REQUIRE(dx.at(20, 20, 0) == gl.at(20, 20, 0));
    REQUIRE(dx.at(20, 20, 1) == -gl.at(20, 20, 1));
    REQUIRE(gl.at(20, 20, 1) > 0.0f); // OpenGL: height rising downwards in the image -> green up
}

TEST_CASE("Export writes the selected maps and the settings file")
{
    const mdtest::TestMaterial m;
    MaterialSettings s;
    s.material = "test";
    s.common.size = 64;
    s.common.seed = 77;
    s.common.maps = MapSet();
    s.common.maps.set(MapType::Albedo, true);
    s.common.maps.set(MapType::Height, true);
    const MaterialMaps maps = m.generate(s);

    const auto dir = std::filesystem::temp_directory_path() / "md_export_test";
    std::filesystem::remove_all(dir);
    const ExportResult r = exportMaps(maps, s, dir);
    REQUIRE(r.files.size() == 2);
    REQUIRE(r.files[0].path.filename() == "test_77_albedo.png");
    REQUIRE(r.files[1].path.filename() == "test_77_height.png");
    REQUIRE(r.files[1].format == "png-gray16-linear");
    REQUIRE(std::filesystem::exists(dir / "test_77_settings.json"));
    std::filesystem::remove_all(dir);
}
