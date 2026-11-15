// OpenGL 1.x drawing for the window (the original used fixed-function GL with vertex arrays; this
// uses immediate mode, which gives the same picture). Functions are loaded through SDL so no GL
// library has to be linked.
#pragma once
#include <utility>
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
    void UpdateTexture(int texture, const Image& image) override;
    void BeginFrame(int width, int height, float r, float g, float b) override;
    void DrawQuad(int texture, const float xy[8], const float uv[8], float r, float g, float b, float a) override;
    void DrawMaskedQuad(int tileTexture, int maskTexture, const float xy[8], const float tileUV[8], const float maskUV[8], float alpha) override;
    void SetClip(bool enabled, float x, float y, float w, float h) override;
    void SetBlend(int mode) override;
    void SetTileTint(float r, float g, float b) override { tileR_ = r; tileG_ = g; tileB_ = b; }
    void EndFrame() override;
    bool SupportsPrograms() const override { return programsOk_; }
    int CreateProgram(const std::string& fragmentSource, std::string& error) override;
    void DeleteProgram(int program) override;
    void SetProgram(int program, const float* params) override;
    void SetProgramGlobals(float time, float width, float height) override { time_ = time; resW_ = width; resH_ = height; }

    // The last frame as RGBA, top row first (for screenshots).
    bool ReadPixels(std::vector<unsigned char>& out, int& width, int& height) override;

    struct Functions;                      // GL entry points, in the .cpp
private:
    Functions* gl_;
    std::vector<unsigned> textures_;
    int winW_ = 1, winH_ = 1;
    float tileR_ = 1, tileG_ = 1, tileB_ = 1;
    int screenW_ = 1, screenH_ = 1;
    int viewX_ = 0, viewY_ = 0, viewW_ = 1, viewH_ = 1;
    int boundTexture_ = -2;
    struct Program { unsigned id = 0; int tex = -1, time = -1, texel = -1, res = -1, hasTex = -1, uvRect = -1, param[4] = {-1, -1, -1, -1}; };
    std::vector<Program> programs_;
    std::vector<std::pair<int, int>> textureSizes_;
    bool programsOk_ = false;
    int program_ = -1;                 // the program in use (-1: none)
    int cachedProgram_ = -1; float cachedTime_ = 0, cachedResW_ = 0, cachedResH_ = 0, cachedParams_[16] = {};   // what SetProgram last sent
    float time_ = 0, resW_ = 1, resH_ = 1;
};

}  // namespace ab
