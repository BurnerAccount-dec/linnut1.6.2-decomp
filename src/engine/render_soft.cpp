#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "engine/render_backend.h"

namespace ab {

int SoftwareBackend::CreateTexture(const Image& image, bool repeat) {
    textures_.push_back(image);
    repeat_.push_back(repeat);
    return (int)textures_.size() - 1;
}

void SoftwareBackend::BeginFrame(int width, int height, float r, float g, float b) {
    width_ = width;
    height_ = height;
    pixels_.assign((size_t)width * height * 4, 255);
    for (size_t i = 0; i < pixels_.size(); i += 4) {
        pixels_[i] = (uint8_t)(r * 255 + 0.5f);
        pixels_[i + 1] = (uint8_t)(g * 255 + 0.5f);
        pixels_[i + 2] = (uint8_t)(b * 255 + 0.5f);
    }
    clip_ = false;
}

void SoftwareBackend::SetClip(bool enabled, float x, float y, float w, float h) {
    clip_ = enabled;
    clipX0_ = (int)std::floor(x);
    clipY0_ = (int)std::floor(y);
    clipX1_ = (int)std::ceil(x + w);
    clipY1_ = (int)std::ceil(y + h);
}

namespace {
float Edge(float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }
}

void SoftwareBackend::DrawQuad(int texture, const float xy[8], const float uv[8], float cr, float cg, float cb, float ca) {
    Raster(texture, -1, xy, uv, nullptr, cr, cg, cb, ca);
}

void SoftwareBackend::DrawMaskedQuad(int tileTexture, int maskTexture, const float xy[8], const float tileUV[8], const float maskUV[8]) {
    Raster(tileTexture, maskTexture, xy, tileUV, maskUV, 1, 1, 1, 1);
}

void SoftwareBackend::Raster(int texture, int mask, const float xy[8], const float uv[8], const float maskUV[8], float cr, float cg, float cb, float ca) {
    const Image* tex = (texture >= 0 && texture < (int)textures_.size()) ? &textures_[texture] : nullptr;
    const Image* mtex = (mask >= 0 && mask < (int)textures_.size()) ? &textures_[mask] : nullptr;
    bool wrap = tex && repeat_[texture];
    static const int tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (int t = 0; t < 2; t++) {
        int i0 = tri[t][0], i1 = tri[t][1], i2 = tri[t][2];
        float x0 = xy[i0 * 2], y0 = xy[i0 * 2 + 1], x1 = xy[i1 * 2], y1 = xy[i1 * 2 + 1], x2 = xy[i2 * 2], y2 = xy[i2 * 2 + 1];
        float area = Edge(x0, y0, x1, y1, x2, y2);
        if (std::fabs(area) < 1e-6f) continue;
        int minX = std::max(0, (int)std::floor(std::min(x0, std::min(x1, x2))));
        int maxX = std::min(width_ - 1, (int)std::ceil(std::max(x0, std::max(x1, x2))));
        int minY = std::max(0, (int)std::floor(std::min(y0, std::min(y1, y2))));
        int maxY = std::min(height_ - 1, (int)std::ceil(std::max(y0, std::max(y1, y2))));
        if (clip_) {
            minX = std::max(minX, clipX0_); maxX = std::min(maxX, clipX1_ - 1);
            minY = std::max(minY, clipY0_); maxY = std::min(maxY, clipY1_ - 1);
        }
        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = Edge(x1, y1, x2, y2, px, py) / area;
                float w1 = Edge(x2, y2, x0, y0, px, py) / area;
                float w2 = Edge(x0, y0, x1, y1, px, py) / area;
                const float eps = -1e-5f;
                if (w0 < eps || w1 < eps || w2 < eps) continue;
                // premultiplied: the sprite sheets' pixels already carry their alpha; the tile texture of a masked quad does not
                float r = cr * ca, g = cg * ca, b = cb * ca, a = ca;
                if (tex) {
                    float u = w0 * uv[i0 * 2] + w1 * uv[i1 * 2] + w2 * uv[i2 * 2];
                    float v = w0 * uv[i0 * 2 + 1] + w1 * uv[i1 * 2 + 1] + w2 * uv[i2 * 2 + 1];
                    int tx, ty;
                    if (wrap) {
                        u -= std::floor(u); v -= std::floor(v);
                        tx = std::min(tex->width - 1, (int)(u * tex->width));
                        ty = std::min(tex->height - 1, (int)(v * tex->height));
                    } else {
                        tx = std::min(tex->width - 1, std::max(0, (int)(u * tex->width)));
                        ty = std::min(tex->height - 1, std::max(0, (int)(v * tex->height)));
                    }
                    const uint8_t* s = &tex->rgba[((size_t)ty * tex->width + tx) * 4];
                    r *= s[0] / 255.0f; g *= s[1] / 255.0f; b *= s[2] / 255.0f; a *= s[3] / 255.0f;
                }
                if (mtex) {
                    float u = w0 * maskUV[i0 * 2] + w1 * maskUV[i1 * 2] + w2 * maskUV[i2 * 2];
                    float v = w0 * maskUV[i0 * 2 + 1] + w1 * maskUV[i1 * 2 + 1] + w2 * maskUV[i2 * 2 + 1];
                    int tx = std::min(mtex->width - 1, std::max(0, (int)(u * mtex->width)));
                    int ty = std::min(mtex->height - 1, std::max(0, (int)(v * mtex->height)));
                    float m = mtex->rgba[((size_t)ty * mtex->width + tx) * 4 + 3] / 255.0f;
                    a *= m; r *= m; g *= m; b *= m;
                }
                uint8_t* d = &pixels_[((size_t)y * width_ + x) * 4];
                d[0] = (uint8_t)(std::min(1.0f, r + d[0] / 255.0f * (1 - a)) * 255 + 0.5f);
                d[1] = (uint8_t)(std::min(1.0f, g + d[1] / 255.0f * (1 - a)) * 255 + 0.5f);
                d[2] = (uint8_t)(std::min(1.0f, b + d[2] / 255.0f * (1 - a)) * 255 + 0.5f);
            }
        }
    }
}

bool SoftwareBackend::WritePng(const char* path) const { return WritePngFile(path, width_, height_, pixels_.data()); }

}  // namespace ab
