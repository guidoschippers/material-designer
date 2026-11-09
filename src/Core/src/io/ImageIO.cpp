#include "md/io/ImageIO.h"

#include "md/core/Error.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

namespace md {

namespace {

const std::array<std::uint32_t, 256>& crcTable()
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            t[n] = c;
        }
        return t;
    }();
    return table;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0xffffffffu)
{
    const auto& t = crcTable();
    for (std::size_t i = 0; i < size; ++i) {
        crc = t[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc;
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void putChunk(std::vector<std::uint8_t>& out, const char type[4], const std::uint8_t* data, std::size_t size)
{
    putU32(out, static_cast<std::uint32_t>(size));
    const std::size_t typeOffset = out.size();
    out.insert(out.end(), type, type + 4);
    if (size > 0) {
        out.insert(out.end(), data, data + size);
    }
    putU32(out, crc32(out.data() + typeOffset, size + 4) ^ 0xffffffffu);
}

int paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

void stbWriteToVector(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

void writeFileBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw Error(ErrorCode::Io, "cannot open '" + path.string() + "' for writing");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw Error(ErrorCode::Io, "failed to write '" + path.string() + "'");
    }
}

void writePng8(const std::filesystem::path& path, int width, int height, int channels, const std::uint8_t* pixels)
{
    std::vector<std::uint8_t> bytes;
    // Written to memory first so that Unicode paths work (stb's file API takes narrow paths).
    if (!stbi_write_png_to_func(stbWriteToVector, &bytes, width, height, channels, pixels, width * channels)) {
        throw Error(ErrorCode::Io, "PNG encoding failed for '" + path.string() + "'");
    }
    writeFileBytes(path, bytes);
}

std::vector<std::uint8_t> encodePng16Gray(int width, int height, const std::uint16_t* pixels)
{
    // Filtered scanlines: one filter byte + big-endian 16-bit samples. The filter is chosen
    // per row by the minimum-sum-of-absolute-differences heuristic recommended by the PNG spec.
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 2;
    std::vector<std::uint8_t> raw(rowBytes * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint16_t v = pixels[static_cast<std::size_t>(y) * width + x];
            raw[y * rowBytes + x * 2] = static_cast<std::uint8_t>(v >> 8);
            raw[y * rowBytes + x * 2 + 1] = static_cast<std::uint8_t>(v & 0xff);
        }
    }

    constexpr int bpp = 2;
    std::vector<std::uint8_t> filtered((rowBytes + 1) * height);
    std::vector<std::uint8_t> candidate(rowBytes);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* cur = raw.data() + y * rowBytes;
        const std::uint8_t* prev = y > 0 ? raw.data() + (y - 1) * rowBytes : nullptr;
        long bestScore = -1;
        std::uint8_t* dst = filtered.data() + y * (rowBytes + 1);
        for (int f = 0; f <= 4; ++f) {
            long score = 0;
            for (std::size_t i = 0; i < rowBytes; ++i) {
                const int a = i >= bpp ? cur[i - bpp] : 0;
                const int b = prev ? prev[i] : 0;
                const int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
                int predictor = 0;
                switch (f) {
                case 1: predictor = a; break;
                case 2: predictor = b; break;
                case 3: predictor = (a + b) / 2; break;
                case 4: predictor = paeth(a, b, c); break;
                default: break;
                }
                const auto v = static_cast<std::uint8_t>(cur[i] - predictor);
                candidate[i] = v;
                score += std::abs(static_cast<int>(static_cast<std::int8_t>(v)));
            }
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                dst[0] = static_cast<std::uint8_t>(f);
                std::memcpy(dst + 1, candidate.data(), rowBytes);
            }
        }
    }

    int zlen = 0;
    unsigned char* zlib = stbi_zlib_compress(filtered.data(), static_cast<int>(filtered.size()), &zlen, 8);
    if (!zlib) {
        throw Error(ErrorCode::Io, "zlib compression failed");
    }

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> ihdr;
    putU32(ihdr, static_cast<std::uint32_t>(width));
    putU32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {16 /*bit depth*/, 0 /*greyscale*/, 0 /*deflate*/, 0 /*adaptive filter*/, 0 /*no interlace*/});
    putChunk(out, "IHDR", ihdr.data(), ihdr.size());
    putChunk(out, "IDAT", zlib, static_cast<std::size_t>(zlen));
    putChunk(out, "IEND", nullptr, 0);
    STBIW_FREE(zlib);
    return out;
}

void writePng16Gray(const std::filesystem::path& path, int width, int height, const std::uint16_t* pixels)
{
    writeFileBytes(path, encodePng16Gray(width, height, pixels));
}

} // namespace md
