#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

namespace md {

/// Planar-interleaved floating point image (row-major, channels interleaved per pixel).
class ImageF {
public:
    ImageF() = default;
    ImageF(int width, int height, int channels, float fill = 0.0f)
        : width_(width), height_(height), channels_(channels),
          data_(static_cast<std::size_t>(width) * height * channels, fill)
    {
    }

    int width() const { return width_; }
    int height() const { return height_; }
    int channels() const { return channels_; }
    bool empty() const { return data_.empty(); }

    float* row(int y) { return data_.data() + index(0, y); }
    const float* row(int y) const { return data_.data() + index(0, y); }

    float& at(int x, int y, int c = 0) { return data_[index(x, y) + c]; }
    float at(int x, int y, int c = 0) const { return data_[index(x, y) + c]; }

    /// Pixel read with wrap-around (periodic) or clamp-to-edge addressing.
    float sample(int x, int y, int c, bool wrap) const
    {
        if (wrap) {
            x = ((x % width_) + width_) % width_;
            y = ((y % height_) + height_) % height_;
        } else {
            x = x < 0 ? 0 : (x >= width_ ? width_ - 1 : x);
            y = y < 0 ? 0 : (y >= height_ ? height_ - 1 : y);
        }
        return at(x, y, c);
    }

    std::vector<float>& data() { return data_; }
    const std::vector<float>& data() const { return data_; }

private:
    std::size_t index(int x, int y) const
    {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        return (static_cast<std::size_t>(y) * width_ + x) * channels_;
    }

    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::vector<float> data_;
};

} // namespace md
