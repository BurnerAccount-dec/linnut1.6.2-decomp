// What the engine needs from a graphics layer. All coordinates are already in screen units (the
// game's own 2D space with the origin at the top left and y down); the backend only has to draw.
// Two implementations: SoftwareBackend (a CPU rasteriser, used by tests and screenshots) and
// GlBackend (OpenGL 1.x through SDL, in src/window/).
#pragma once
#include <cstdint>
#include <vector>

#include "engine/png_decoder.h"

namespace ab {

class RenderBackend {
public:
    virtual ~RenderBackend() {}
    // Returns a texture handle (>= 0).
    // repeat: the texture wraps (for tiled fills) instead of clamping at its edges.
    virtual int CreateTexture(const Image& image, bool repeat = false) = 0;
    // Starts a frame in a space of width x height screen units; clears to the colour (0..1).
    virtual void BeginFrame(int width, int height, float r, float g, float b) = 0;
    // A quad with corners in the order top-left, top-right, bottom-right, bottom-left (xy: 4 x,y pairs).
    // texture < 0 draws a plain colour. The colour multiplies the texture; alpha blends over what is there.
    virtual void DrawQuad(int texture, const float xy[8], const float uv[8], float r, float g, float b, float a) = 0;
    // The effect "2d-sprite-alpha-masked": colour from the tile texture at tileUV, alpha multiplied by the mask
    // texture's alpha at maskUV (used for the ground blocks, which show a theme texture through a sprite shape).
    virtual void DrawMaskedQuad(int tileTexture, int maskTexture, const float xy[8], const float tileUV[8], const float maskUV[8]) = 0;
    virtual void SetClip(bool enabled, float x, float y, float w, float h) = 0;
    virtual void EndFrame() = 0;
};

// Draws on the CPU into an RGBA buffer. Slow, but needs nothing, and gives the same picture every time.
class SoftwareBackend : public RenderBackend {
public:
    int CreateTexture(const Image& image, bool repeat = false) override;
    void BeginFrame(int width, int height, float r, float g, float b) override;
    void DrawQuad(int texture, const float xy[8], const float uv[8], float r, float g, float b, float a) override;
    void DrawMaskedQuad(int tileTexture, int maskTexture, const float xy[8], const float tileUV[8], const float maskUV[8]) override;
    void SetClip(bool enabled, float x, float y, float w, float h) override;
    void EndFrame() override {}

    int Width() const { return width_; }
    int Height() const { return height_; }
    const std::vector<uint8_t>& Pixels() const { return pixels_; }     // RGBA, top row first
    bool WritePng(const char* path) const;                              // for looking at a frame

private:
    void Raster(int texture, int mask, const float xy[8], const float uv[8], const float maskUV[8], float cr, float cg, float cb, float ca);
    std::vector<Image> textures_;
    std::vector<bool> repeat_;
    std::vector<uint8_t> pixels_;
    int width_ = 0, height_ = 0;
    bool clip_ = false;
    int clipX0_ = 0, clipY0_ = 0, clipX1_ = 0, clipY1_ = 0;
};

}  // namespace ab
