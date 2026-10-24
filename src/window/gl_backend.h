// OpenGL 1.x drawing for the window (the original used fixed-function GL with vertex arrays; this
// uses immediate mode, which gives the same picture). Functions are loaded through SDL so no GL
// library has to be linked.
#pragma once
#include <vector>

#include "engine/render_backend.h"

namespace ab {

class GlBackend : public RenderBackend {
public:
    GlBackend();
    // Needs a current GL context. Returns false if the functions cannot be found.
    bool Init();
    // The size of the window's drawable area in pixels; the game's screen is fitted into it.
    void SetWindowSize(int pixelWidth, int pixelHeight) { winW_ = pixelWidth; winH_ = pixelHeight; }

    int CreateTexture(const Image& image, bool repeat = false) override;
    void BeginFrame(int width, int height, float r, float g, float b) override;
    void DrawQuad(int texture, const float xy[8], const float uv[8], float r, float g, float b, float a) override;
    void DrawMaskedQuad(int tileTexture, int maskTexture, const float xy[8], const float tileUV[8], const float maskUV[8]) override;
    void SetClip(bool enabled, float x, float y, float w, float h) override;
    void EndFrame() override;

    // The last frame as RGBA, top row first (for screenshots).
    bool ReadPixels(std::vector<unsigned char>& out, int& width, int& height);

    struct Functions;                      // GL entry points, in the .cpp
private:
    Functions* gl_;
    std::vector<unsigned> textures_;
    int winW_ = 1, winH_ = 1;
    int screenW_ = 1, screenH_ = 1;
    int viewX_ = 0, viewY_ = 0, viewW_ = 1, viewH_ = 1;
    int boundTexture_ = -2;
};

}  // namespace ab
