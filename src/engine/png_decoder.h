// A small PNG reader (8-bit, not interlaced; grey, grey+alpha, RGB, RGBA and palette images), with its
// own inflate, so the engine needs no image library. The game's .png sheets are all 8-bit RGBA or palette.
// The original used libpng/zlib (strings in the exe); the decoded pixels are the same.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ab {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;       // width * height * 4, top row first
};

bool DecodePng(const std::vector<uint8_t>& file, Image& out, std::string* error);
bool LoadPngFile(const std::string& path, Image& out, std::string* error);

// Writes width x height RGBA pixels (top row first) as a PNG file.
bool WritePngFile(const char* path, int width, int height, const uint8_t* rgba);

}  // namespace ab
