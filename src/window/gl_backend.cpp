#include "window/gl_backend.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cmath>
#include <cstdlib>
#include <string>

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
    // OpenGL 2.0 (v3.4: shaders written by the user); all of them or none
    unsigned (APIENTRY* CreateShader)(unsigned);
    void (APIENTRY* ShaderSource)(unsigned, int, const char* const*, const int*);
    void (APIENTRY* CompileShader)(unsigned);
    void (APIENTRY* GetShaderiv)(unsigned, unsigned, int*);
    void (APIENTRY* GetShaderInfoLog)(unsigned, int, int*, char*);
    unsigned (APIENTRY* CreateProgram)();
    void (APIENTRY* AttachShader)(unsigned, unsigned);
    void (APIENTRY* LinkProgram)(unsigned);
    void (APIENTRY* GetProgramiv)(unsigned, unsigned, int*);
    void (APIENTRY* GetProgramInfoLog)(unsigned, int, int*, char*);
    void (APIENTRY* UseProgram)(unsigned);
    int (APIENTRY* GetUniformLocation)(unsigned, const char*);
    void (APIENTRY* Uniform1i)(int, int);
    void (APIENTRY* Uniform1f)(int, float);
    void (APIENTRY* Uniform2f)(int, float, float);
    void (APIENTRY* Uniform4f)(int, float, float, float, float);
    void (APIENTRY* DeleteProgram)(unsigned);
    void (APIENTRY* DeleteShader)(unsigned);
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
    bool shaders = true;
#define LOADS(name) do { gl_->name = (decltype(gl_->name))SDL_GL_GetProcAddress("gl" #name); if (!gl_->name) shaders = false; } while (0)
    LOADS(CreateShader); LOADS(ShaderSource); LOADS(CompileShader); LOADS(GetShaderiv); LOADS(GetShaderInfoLog); LOADS(CreateProgram); LOADS(AttachShader);
    LOADS(LinkProgram); LOADS(GetProgramiv); LOADS(GetProgramInfoLog); LOADS(UseProgram); LOADS(GetUniformLocation); LOADS(Uniform1i); LOADS(Uniform1f);
    LOADS(Uniform2f); LOADS(Uniform4f); LOADS(DeleteProgram); LOADS(DeleteShader);
#undef LOADS
    programsOk_ = shaders && !std::getenv("AB_NO_SHADERS");   // AB_NO_SHADERS=1 keeps the fixed pipeline (to compare, or on a driver with broken shaders)
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
    textureSizes_.push_back({image.width, image.height});
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
    if (program_ >= 0 && program_ < (int)programs_.size()) {      // v3.4: a shader of the user's; it gets what the quad is made of
        const Program& pr = programs_[program_];
        float u0 = uv[0], v0 = uv[1], u1 = uv[0], v1 = uv[1];
        for (int i = 1; i < 4; i++) { u0 = std::fmin(u0, uv[i * 2]); u1 = std::fmax(u1, uv[i * 2]); v0 = std::fmin(v0, uv[i * 2 + 1]); v1 = std::fmax(v1, uv[i * 2 + 1]); }
        const bool has = texture >= 0 && texture < (int)textureSizes_.size();
        if (pr.hasTex >= 0) gl_->Uniform1f(pr.hasTex, has ? 1.0f : 0.0f);
        if (pr.texel >= 0) gl_->Uniform2f(pr.texel, has ? 1.0f / std::fmax(1, textureSizes_[texture].first) : 0.0f, has ? 1.0f / std::fmax(1, textureSizes_[texture].second) : 0.0f);
        if (pr.uvRect >= 0) gl_->Uniform4f(pr.uvRect, u0, v0, u1, v1);
    }
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
    if (program_ >= 0) SetProgram(-1, nullptr);      // the masked (tiled) blocks use the fixed pipeline
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
    gl_->Color4f(alpha * tileR_, alpha * tileG_, alpha * tileB_, alpha);   // premultiplied, like DrawQuad: the sprite alpha scales colour and alpha
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

void GlBackend::SetBlend(int mode) {
    switch (mode) {
    case 1: gl_->BlendFunc(GL_ONE, GL_ONE); break;
    case 2: gl_->BlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
    case 3: gl_->BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
    default: gl_->BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
    }
}

void GlBackend::EndFrame() { if (program_ >= 0) SetProgram(-1, nullptr); }

// v3.4: the vertex shader is fixed; the fragment shader is the user's (the engine puts the prelude in front, see shaders.cpp)
int GlBackend::CreateProgram(const std::string& fragmentSource, std::string& error) {
    if (!programsOk_) { error = "this graphics driver has no shaders (OpenGL 2.0 is needed)"; return -1; }
    static const char* vertex =
        "#version 110\n"
        "varying vec2 v_uv; varying vec4 v_color; varying vec2 v_pos;\n"
        "void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; v_uv = gl_MultiTexCoord0.xy; v_color = gl_Color; v_pos = gl_Vertex.xy; }\n";
    auto compile = [&](unsigned type, const char* text, const char* what, unsigned& out) {
        out = gl_->CreateShader(type);
        gl_->ShaderSource(out, 1, &text, nullptr);
        gl_->CompileShader(out);
        int ok = 0;
        gl_->GetShaderiv(out, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
        if (ok) return true;
        char log[2048] = {0};
        gl_->GetShaderInfoLog(out, sizeof(log) - 1, nullptr, log);
        error = std::string(what) + ": " + log;
        gl_->DeleteShader(out);
        return false;
    };
    unsigned vs = 0, fs = 0;
    if (!compile(0x8B31 /* GL_VERTEX_SHADER */, vertex, "vertex shader", vs)) return -1;
    if (!compile(0x8B30 /* GL_FRAGMENT_SHADER */, fragmentSource.c_str(), "fragment shader", fs)) { gl_->DeleteShader(vs); return -1; }
    Program p;
    p.id = gl_->CreateProgram();
    gl_->AttachShader(p.id, vs);
    gl_->AttachShader(p.id, fs);
    gl_->LinkProgram(p.id);
    gl_->DeleteShader(vs);
    gl_->DeleteShader(fs);
    int linked = 0;
    gl_->GetProgramiv(p.id, 0x8B82 /* GL_LINK_STATUS */, &linked);
    if (!linked) {
        char log[2048] = {0};
        gl_->GetProgramInfoLog(p.id, sizeof(log) - 1, nullptr, log);
        error = std::string("link: ") + log;
        gl_->DeleteProgram(p.id);
        return -1;
    }
    p.tex = gl_->GetUniformLocation(p.id, "u_tex"); p.time = gl_->GetUniformLocation(p.id, "u_time");
    p.texel = gl_->GetUniformLocation(p.id, "u_texel"); p.res = gl_->GetUniformLocation(p.id, "u_resolution");
    p.hasTex = gl_->GetUniformLocation(p.id, "u_hasTex"); p.uvRect = gl_->GetUniformLocation(p.id, "u_uvRect");
    for (int i = 0; i < 4; i++) p.param[i] = gl_->GetUniformLocation(p.id, (std::string("u_param") + (char)('0' + i)).c_str());
    for (size_t i = 0; i < programs_.size(); i++) if (programs_[i].id == 0) { programs_[i] = p; return (int)i; }     // a deleted slot
    programs_.push_back(p);
    return (int)programs_.size() - 1;
}
void GlBackend::DeleteProgram(int program) {
    if (program < 0 || program >= (int)programs_.size() || programs_[program].id == 0) return;
    if (program_ == program) SetProgram(-1, nullptr);
    gl_->DeleteProgram(programs_[program].id);
    programs_[program] = Program();
}
void GlBackend::SetProgram(int program, const float* params) {
    if (!programsOk_) return;
    if (program < 0 || program >= (int)programs_.size() || programs_[program].id == 0) {
        if (program_ >= 0) { gl_->UseProgram(0); program_ = -1; boundTexture_ = -2; }
        return;
    }
    const Program& p = programs_[program];
    gl_->UseProgram(p.id);
    program_ = program;
    if (p.tex >= 0) gl_->Uniform1i(p.tex, 0);
    if (p.time >= 0) gl_->Uniform1f(p.time, time_);
    if (p.res >= 0) gl_->Uniform2f(p.res, resW_, resH_);
    for (int i = 0; i < 4; i++) if (p.param[i] >= 0) gl_->Uniform4f(p.param[i], params ? params[i * 4] : 0, params ? params[i * 4 + 1] : 0, params ? params[i * 4 + 2] : 0, params ? params[i * 4 + 3] : 0);
}

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

namespace ab {
void GlBackend::UpdateTexture(int texture, const Image& image) {
    if (texture < 0 || texture >= (int)textures_.size()) return;
    gl_->BindTexture(GL_TEXTURE_2D, textures_[texture]);
    gl_->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl_->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    boundTexture_ = -2;
}
}  // namespace ab
