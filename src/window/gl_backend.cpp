#include "window/gl_backend.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cmath>

namespace ab {

struct GlBackend::Functions {
    void (APIENTRY* ClearColor)(GLclampf, GLclampf, GLclampf, GLclampf);
    void (APIENTRY* Clear)(GLbitfield);
    void (APIENTRY* Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (APIENTRY* MatrixMode)(GLenum);
    void (APIENTRY* LoadIdentity)();
    void (APIENTRY* Ortho)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
    void (APIENTRY* Enable)(GLenum);
    void (APIENTRY* Disable)(GLenum);
    void (APIENTRY* BlendFunc)(GLenum, GLenum);
    void (APIENTRY* GenTextures)(GLsizei, GLuint*);
    void (APIENTRY* BindTexture)(GLenum, GLuint);
    void (APIENTRY* TexParameteri)(GLenum, GLenum, GLint);
    void (APIENTRY* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (APIENTRY* TexEnvi)(GLenum, GLenum, GLint);
    void (APIENTRY* PixelStorei)(GLenum, GLint);
    void (APIENTRY* Begin)(GLenum);
    void (APIENTRY* End)();
    void (APIENTRY* Vertex2f)(GLfloat, GLfloat);
    void (APIENTRY* TexCoord2f)(GLfloat, GLfloat);
    void (APIENTRY* Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (APIENTRY* Scissor)(GLint, GLint, GLsizei, GLsizei);
    void (APIENTRY* ActiveTexture)(GLenum);
    void (APIENTRY* MultiTexCoord2f)(GLenum, GLfloat, GLfloat);
    void (APIENTRY* ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
};

GlBackend::GlBackend() : gl_(new Functions()) {}

bool GlBackend::Init() {
    bool ok = true;
#define LOAD(name) do { gl_->name = (decltype(gl_->name))SDL_GL_GetProcAddress("gl" #name); if (!gl_->name) ok = false; } while (0)
    LOAD(ClearColor); LOAD(Clear); LOAD(Viewport); LOAD(MatrixMode); LOAD(LoadIdentity); LOAD(Ortho);
    LOAD(Enable); LOAD(Disable); LOAD(BlendFunc); LOAD(GenTextures); LOAD(BindTexture); LOAD(TexParameteri);
    LOAD(TexImage2D); LOAD(TexEnvi); LOAD(PixelStorei); LOAD(Begin); LOAD(End); LOAD(Vertex2f); LOAD(TexCoord2f);
    LOAD(Color4f); LOAD(Scissor); LOAD(ReadPixels);
    gl_->ActiveTexture = (decltype(gl_->ActiveTexture))SDL_GL_GetProcAddress("glActiveTexture");
    gl_->MultiTexCoord2f = (decltype(gl_->MultiTexCoord2f))SDL_GL_GetProcAddress("glMultiTexCoord2f");
#undef LOAD
    return ok;
}

int GlBackend::CreateTexture(const Image& image, bool repeat) {
    GLuint id = 0;
    gl_->GenTextures(1, &id);
    gl_->BindTexture(GL_TEXTURE_2D, id);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    gl_->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl_->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    boundTexture_ = -2;
    textures_.push_back(id);
    return (int)textures_.size() - 1;
}

// The game's screen (width x height units) is fitted into the window, keeping its shape.
void GlBackend::BeginFrame(int width, int height, float r, float g, float b) {
    screenW_ = width;
    screenH_ = height;
    gl_->Disable(GL_SCISSOR_TEST);
    gl_->Viewport(0, 0, winW_, winH_);
    gl_->ClearColor(0, 0, 0, 1);
    gl_->Clear(GL_COLOR_BUFFER_BIT);
    float scale = std::fmin((float)winW_ / width, (float)winH_ / height);
    viewW_ = (int)(width * scale);
    viewH_ = (int)(height * scale);
    viewX_ = (winW_ - viewW_) / 2;
    viewY_ = (winH_ - viewH_) / 2;
    gl_->Viewport(viewX_, viewY_, viewW_, viewH_);
    gl_->Enable(GL_SCISSOR_TEST);
    gl_->Scissor(viewX_, viewY_, viewW_, viewH_);
    gl_->ClearColor(r, g, b, 1);
    gl_->Clear(GL_COLOR_BUFFER_BIT);
    gl_->MatrixMode(GL_PROJECTION);
    gl_->LoadIdentity();
    gl_->Ortho(0, width, height, 0, -1, 1);
    gl_->MatrixMode(GL_MODELVIEW);
    gl_->LoadIdentity();
    gl_->Enable(GL_BLEND);
    // the sprite sheets' pixels are premultiplied by their alpha
    gl_->BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    boundTexture_ = -2;
}

void GlBackend::SetClip(bool enabled, float x, float y, float w, float h) {
    if (!enabled) {
        gl_->Scissor(viewX_, viewY_, viewW_, viewH_);
        return;
    }
    float sx = (float)viewW_ / screenW_, sy = (float)viewH_ / screenH_;
    int px = viewX_ + (int)std::floor(x * sx), pw = (int)std::ceil(w * sx);
    int py = viewY_ + (int)std::floor((screenH_ - (y + h)) * sy), ph = (int)std::ceil(h * sy);
    gl_->Scissor(px, py, pw < 0 ? 0 : pw, ph < 0 ? 0 : ph);
}

void GlBackend::DrawQuad(int texture, const float xy[8], const float uv[8], float r, float g, float b, float a) {
    if (texture != boundTexture_) {
        if (texture >= 0 && texture < (int)textures_.size()) {
            gl_->Enable(GL_TEXTURE_2D);
            gl_->BindTexture(GL_TEXTURE_2D, textures_[texture]);
        } else {
            gl_->Disable(GL_TEXTURE_2D);
        }
        boundTexture_ = texture;
    }
    gl_->Color4f(r * a, g * a, b * a, a);
    gl_->Begin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
        gl_->TexCoord2f(uv[i * 2], uv[i * 2 + 1]);
        gl_->Vertex2f(xy[i * 2], xy[i * 2 + 1]);
    }
    gl_->End();
}

// Two texture units: unit 0 gives the colour (the tile texture), unit 1 multiplies the alpha by the mask's alpha.
void GlBackend::DrawMaskedQuad(int tileTexture, int maskTexture, const float xy[8], const float tileUV[8], const float maskUV[8], float alpha) {
    if (!gl_->ActiveTexture || !gl_->MultiTexCoord2f) return;
    if (tileTexture < 0 || tileTexture >= (int)textures_.size() || maskTexture < 0 || maskTexture >= (int)textures_.size()) return;
    gl_->ActiveTexture(GL_TEXTURE0);
    gl_->Enable(GL_TEXTURE_2D);
    gl_->BindTexture(GL_TEXTURE_2D, textures_[tileTexture]);
    gl_->ActiveTexture(GL_TEXTURE1);
    gl_->Enable(GL_TEXTURE_2D);
    gl_->BindTexture(GL_TEXTURE_2D, textures_[maskTexture]);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);       // tile colour times the mask's alpha (premultiplied)
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_ALPHA);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_TEXTURE);
    gl_->TexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
    gl_->Color4f(alpha, alpha, alpha, alpha);   // premultiplied, like DrawQuad: the sprite alpha scales colour and alpha
    gl_->Begin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
        gl_->MultiTexCoord2f(GL_TEXTURE0, tileUV[i * 2], tileUV[i * 2 + 1]);
        gl_->MultiTexCoord2f(GL_TEXTURE1, maskUV[i * 2], maskUV[i * 2 + 1]);
        gl_->Vertex2f(xy[i * 2], xy[i * 2 + 1]);
    }
    gl_->End();
    gl_->Disable(GL_TEXTURE_2D);
    gl_->ActiveTexture(GL_TEXTURE0);
    boundTexture_ = -2;
}

void GlBackend::EndFrame() {}

bool GlBackend::ReadPixels(std::vector<unsigned char>& out, int& width, int& height) {
    width = winW_;
    height = winH_;
    std::vector<unsigned char> raw((size_t)width * height * 4);
    gl_->PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl_->ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    out.resize(raw.size());
    for (int y = 0; y < height; y++)                 // GL's first row is the bottom one
        for (int x = 0; x < width * 4; x++) out[(size_t)y * width * 4 + x] = raw[(size_t)(height - 1 - y) * width * 4 + x];
    return true;
}

}  // namespace ab
