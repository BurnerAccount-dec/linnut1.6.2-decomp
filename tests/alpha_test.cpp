// The alpha of setRenderState reaches plain and masked (tiled) quads of the software renderer.
#include <cstdio>

#include "engine/render_backend.h"

using namespace ab;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

static Image Solid(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Image im; im.width = im.height = 2; im.rgba.resize(16);
    for (int i = 0; i < 4; i++) { im.rgba[i * 4] = r; im.rgba[i * 4 + 1] = g; im.rgba[i * 4 + 2] = b; im.rgba[i * 4 + 3] = a; }
    return im;
}

int main() {
    SoftwareBackend be;
    int tile = be.CreateTexture(Solid(255, 255, 255, 255), true);   // white tile colour
    int mask = be.CreateTexture(Solid(255, 255, 255, 255));         // fully opaque mask shape
    const float xy[8] = {0, 0, 4, 0, 4, 4, 0, 4}, uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    for (int pass = 0; pass < 2; pass++) {
        float alpha = pass == 0 ? 1.0f : 0.5f;
        be.BeginFrame(4, 4, 0, 0, 0);                                // black background
        be.DrawMaskedQuad(tile, mask, xy, uv, uv, alpha);
        int v = be.Pixels()[(2 * 4 + 1) * 4];                        // red of a pixel inside (not on the quad's diagonal)
        if (pass == 0) CHECK(v >= 250);
        else { if (!(v >= 120 && v <= 135)) std::printf("middle pixel %d\n", v); CHECK(v >= 120 && v <= 135); }
    }
    be.BeginFrame(4, 4, 0, 0, 0);
    be.DrawQuad(mask, xy, uv, 1, 1, 1, 0.5f);
    int plain = be.Pixels()[(2 * 4 + 1) * 4];
    be.BeginFrame(4, 4, 0, 0, 0);
    be.DrawMaskedQuad(tile, mask, xy, uv, uv, 0.5f);
    int masked = be.Pixels()[(2 * 4 + 1) * 4];
    CHECK(plain == masked);                                          // a tiled sprite fades exactly like a plain one
    if (g_fail == 0) std::printf("alpha tests passed\n");
    return g_fail ? 1 : 0;
}
