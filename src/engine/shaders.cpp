// v3.4: shaders written by the user (GLSL), the built-in presets as shaders that can be edited, a live reload and the console's shader creator.
//
// A shader is one file, <user data folder>/shaders/<name>.frag. It is a fragment shader; the engine puts a prelude in front of it (the uniforms and
// varyings below) and, unless the file has its own main(), the main() that calls the function the file defines:
//     vec4 shade(vec4 texel, vec2 uv, vec4 tint)
// texel is the sprite's pixel (premultiplied by its alpha, as all the sheets are), uv its coordinates in the sheet, tint the colour the sprite is drawn
// with (also premultiplied). It returns the premultiplied colour that is blended over the picture.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <fstream>
#include <sstream>

#include "engine/game_platform.h"
#include "engine/level_writer.h"
#include "lua/dec_folder.h"

namespace ab {

namespace {

// what every shader file starts with. u_uvRect is the part of the sheet the sprite is cut from, so sampleTex never reads the neighbouring sprite
const char* kPrelude =
    "#version 110\n"
    "uniform sampler2D u_tex;      // the sheet\n"
    "uniform float u_time;         // seconds since the start\n"
    "uniform vec2 u_texel;         // the size of one sheet pixel in uv units\n"
    "uniform vec2 u_resolution;    // the game's screen in screen units\n"
    "uniform float u_hasTex;       // 0 for the plain coloured rectangles\n"
    "uniform vec4 u_uvRect;        // the sprite's part of the sheet: minU, minV, maxU, maxV\n"
    "uniform vec4 u_param0, u_param1, u_param2, u_param3;   // setShaderParam\n"
    "varying vec2 v_uv; varying vec4 v_color; varying vec2 v_pos;   // v_pos: where this is on the screen, in screen units\n"
    "vec4 sampleTex(vec2 uv) { return u_hasTex > 0.5 ? texture2D(u_tex, clamp(uv, u_uvRect.xy, u_uvRect.zw)) : vec4(1.0); }\n"
    "#line 0\n";   // GLSL 1.10 counts the line after #line as n + 1: the user's first line is line 1
const char* kMain = "\nvoid main() { gl_FragColor = shade(sampleTex(v_uv), v_uv, v_color); }\n";

struct Template { const char* name; const char* about; const char* body; };
const Template kTemplates[] = {
    {"blank", "draws the sprite as it is; a place to start",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    return texel * tint;\n}\n"},
    {"grayscale", "the built-in grayscale preset",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    float l = dot(texel.rgb, vec3(0.299, 0.587, 0.114));\n    return vec4(vec3(l), texel.a) * tint;\n}\n"},
    {"sepia", "the built-in sepia preset",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    float l = dot(texel.rgb, vec3(0.299, 0.587, 0.114));\n    return vec4(min(texel.a, l * 1.07), l * 0.74, l * 0.43, texel.a) * tint;\n}\n"},
    {"invert", "the built-in invert preset",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    return vec4(texel.a - texel.rgb, texel.a) * tint;\n}\n"},
    {"chromatic", "the built-in chromatic preset: red and blue pulled apart (u_param0.x: pixels, 2.5 when 0)",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    float d = (u_param0.x > 0.0 ? u_param0.x : 2.5) * u_texel.x;\n    float r = sampleTex(uv - vec2(d, 0.0)).r;\n    float b = sampleTex(uv + vec2(d, 0.0)).b;\n    return vec4(r, texel.g, b, texel.a) * tint;\n}\n"},
    {"glow", "the built-in glow preset: a soft halo from the neighbours (u_param0.x: pixels, 3 when 0)",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    vec2 d = vec2(u_param0.x > 0.0 ? u_param0.x : 3.0) * u_texel;\n    vec4 halo = sampleTex(uv + vec2(d.x, 0.0)) + sampleTex(uv - vec2(d.x, 0.0)) + sampleTex(uv + vec2(0.0, d.y)) + sampleTex(uv - vec2(0.0, d.y));\n    return (texel + halo * 0.22 * (1.0 - texel.a)) * tint;\n}\n"},
    {"wave", "the picture ripples sideways (u_param0.x: strength in pixels, 3 when 0)",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    float s = u_param0.x > 0.0 ? u_param0.x : 3.0;\n    vec2 w = uv + vec2(sin(uv.y / u_texel.y * 0.15 + u_time * 3.0) * u_texel.x * s, 0.0);\n    return sampleTex(w) * tint;\n}\n"},
    {"scanlines", "dark lines across the screen, like a monitor",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    float s = 0.78 + 0.22 * sin(v_pos.y * 3.14159);\n    return vec4(texel.rgb * s, texel.a) * tint;\n}\n"},
    {"vignette", "the corners of the screen go dark (u_param0.x: how much, 1.6 when 0)",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    vec2 p = v_pos / u_resolution - 0.5;\n    float k = u_param0.x > 0.0 ? u_param0.x : 1.6;\n    float v = clamp(1.0 - dot(p, p) * k, 0.0, 1.0);\n    return vec4(texel.rgb * v, texel.a) * tint;\n}\n"},
    {"pixelate", "big square pixels (u_param0.x: size in sheet pixels, 4 when 0)",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    vec2 g = u_texel * (u_param0.x > 0.0 ? u_param0.x : 4.0);\n    return sampleTex((floor(uv / g) + 0.5) * g) * tint;\n}\n"},
    {"pulse", "brightness that breathes in time (u_param0.x: speed, 4 when 0)",
     "vec4 shade(vec4 texel, vec2 uv, vec4 tint) {\n    float p = 0.75 + 0.25 * sin(u_time * (u_param0.x > 0.0 ? u_param0.x : 4.0));\n    return vec4(texel.rgb * p, texel.a) * tint;\n}\n"},
};
const int kTemplateCount = (int)(sizeof(kTemplates) / sizeof(kTemplates[0]));

const Template* FindTemplate(const std::string& name) {
    for (int i = 0; i < kTemplateCount; i++) if (name == kTemplates[i].name) return &kTemplates[i];
    return nullptr;
}

long long FileStamp(const std::string& path) {      // the time of the last change and the size: -1 when there is no such file
    struct stat st;
    return stat(path.c_str(), &st) != 0 ? -1 : (long long)st.st_mtime * 1000003LL + (long long)st.st_size;
}
bool FileExists(const std::string& path) { return FileStamp(path) >= 0; }
bool ReadAll(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
}  // namespace

std::string GamePlatform::ShaderDir() { return ResolveUserFile("shaders", false); }

bool GamePlatform::ShadersSupported() const { return backend_ && backend_->SupportsPrograms(); }

// The text that is compiled: the prelude, the user's code, and the main() unless the file brings its own.
bool GamePlatform::CompileShader(CustomShader& s) {
    if (!backend_ || !backend_->SupportsPrograms()) { s.error = "this renderer cannot run shaders (the software renderer, or a driver without OpenGL 2.0)"; return false; }
    std::string text = kPrelude + s.source;
    if (s.source.find("void main") == std::string::npos) text += kMain;
    std::string error;
    int program = backend_->CreateProgram(text, error);
    if (program < 0) { s.error = error; return false; }
    if (s.program >= 0) backend_->DeleteProgram(s.program);
    s.program = program;
    s.error.clear();
    return true;
}

// the index of a shader: one that is loaded, else <name>.frag in the shaders folder is loaded; -1: there is none
int GamePlatform::ShaderIndex(const std::string& name, bool load) {
    for (size_t i = 0; i < shaders_.size(); i++) if (shaders_[i].name == name) return (int)i;
    if (!load || !ShadersSupported() || name.empty() || name.find_first_of("/\\.") != std::string::npos) return -1;
    CustomShader s;
    s.name = name; s.path = ShaderDir() + "/" + name + ".frag";
    if (!ReadAll(s.path, s.source)) return -1;
    s.stamp = FileStamp(s.path);
    if (!CompileShader(s)) std::fprintf(stderr, "shader %s: %s\n", name.c_str(), s.error.c_str());
    shaders_.push_back(s);
    return (int)shaders_.size() - 1;
}

// setShader's names: a shader file of that name first (so the presets can be replaced by editing grayscale.frag ...), then a created one
int GamePlatform::ShaderByName(const std::string& name) {
    const int i = ShaderIndex(name, true);
    if (i < 0 || shaders_[i].program < 0) return -1;     // a shader that did not compile draws nothing special: the preset of that name (or none) is used
    return 6 + i;
}

bool GamePlatform::CreateShaderFromSource(const std::string& name, const std::string& source, std::string& error) {
    if (name.empty()) { error = "a shader needs a name"; return false; }
    int i = ShaderIndex(name, false);
    if (i < 0) { shaders_.push_back(CustomShader()); i = (int)shaders_.size() - 1; shaders_[i].name = name; }
    CustomShader& s = shaders_[i];
    s.source = source; s.path.clear(); s.stamp = 0;       // made by a script: not reloaded from a file
    const bool ok = CompileShader(s);
    error = s.error;
    return ok;
}

void GamePlatform::SetShaderParam(const std::string& name, int slot, float x, float y, float z, float w) {
    const int i = ShaderIndex(name, true);
    if (i < 0 || slot < 0 || slot > 3) return;
    float* p = &shaders_[i].params[slot * 4];
    p[0] = x; p[1] = y; p[2] = z; p[3] = w;
}

std::string GamePlatform::ShaderError(const std::string& name) {
    const int i = ShaderIndex(name, true);
    return i < 0 ? std::string("no shader of that name") : shaders_[i].error;
}

// the saved files of the shaders folder that are not loaded yet, and the ones that are
void GamePlatform::ShaderNames(std::vector<std::string>& out) {
    for (const std::string& f : ListFolder(ShaderDir()))
        if (f.size() > 5 && f.compare(f.size() - 5, 5, ".frag") == 0) out.push_back(f.substr(0, f.size() - 5));
    for (const CustomShader& s : shaders_) {
        bool have = false;
        for (const std::string& n : out) if (n == s.name) have = true;
        if (!have) out.push_back(s.name);
    }
}

// once a frame: the clock for u_time, and twice a second a look at the files; a saved change is compiled at once (a mistake keeps the last good version)
void GamePlatform::ShadersEveryFrame() {
    if (!backend_ || !backend_->SupportsPrograms()) return;
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    const double now = std::chrono::duration<double>(clock::now() - start).count();
    backend_->SetProgramGlobals((float)now, (float)screenWidth_, (float)screenHeight_);
    if (shaders_.empty() || now < shaderCheck_) return;
    shaderCheck_ = now + 0.5;
    for (CustomShader& s : shaders_) {
        if (s.path.empty()) continue;
        const long long stamp = FileStamp(s.path);
        if (stamp < 0 || stamp == s.stamp) continue;
        s.stamp = stamp;
        std::string text;
        if (!ReadAll(s.path, text)) continue;
        CustomShader trial = s;
        trial.source = text;
        trial.program = -1;
        if (CompileShader(trial)) {
            if (s.program >= 0) backend_->DeleteProgram(s.program);
            s.source = text; s.program = trial.program; s.error.clear();
            std::printf("shader %s: reloaded\n", s.name.c_str());
        } else {
            s.error = trial.error;
            std::fprintf(stderr, "shader %s: %s\n", s.name.c_str(), trial.error.c_str());     // the old version keeps running
        }
    }
}

// console: shader list | new <name> [template] | export | reload [name] | apply <target> <name> | param <name> <slot 0..3> x [y z w] | check <name>
// returns false when the first word is not one of these (then it is the old "shader <object> <effect>")
bool GamePlatform::ShaderConsole(const std::string& arg) {
    std::istringstream ss(arg);
    std::string sub;
    ss >> sub;
    if (sub != "list" && sub != "new" && sub != "export" && sub != "reload" && sub != "apply" && sub != "param" && sub != "templates" && sub != "check" && sub != "help") return false;
    auto say = [](const std::string& m) { std::printf("console: %s\n", m.c_str()); };
    if (sub == "help") {
        say("shader list | templates | new <name> [template] | export | reload [name] | apply <target> <name> | param <name> <slot 0..3> x [y z w] | check <name>");
        say("a shader is the file <user data>/shaders/<name>.frag; save it in any editor and it is compiled at once. See docs/SCRIPTING.md, \"Shaders\".");
        return true;
    }
    if (sub == "templates") { for (int i = 0; i < kTemplateCount; i++) std::printf("  %-10s %s\n", kTemplates[i].name, kTemplates[i].about); return true; }
    if (sub == "list") {
        std::vector<std::string> names;
        ShaderNames(names);
        std::printf("console: shaders are %s; folder %s\n", ShadersSupported() ? "available" : "NOT available in this renderer", ShaderDir().c_str());
        for (const std::string& n : names) {
            const int i = ShaderIndex(n, false);
            std::printf("  %-14s %s\n", n.c_str(), i < 0 ? "(file, not loaded yet)" : shaders_[i].error.empty() ? "ok" : ("ERROR " + shaders_[i].error).c_str());
        }
        return true;
    }
    if (sub == "export") {
        MakeDirectory(ShaderDir());
        int n = 0;
        for (int i = 0; i < kTemplateCount; i++) {
            const std::string path = ShaderDir() + "/" + kTemplates[i].name + ".frag";
            if (FileExists(path)) continue;       // never over a file that was edited
            std::ofstream(path, std::ios::binary) << "// " << kTemplates[i].about << "\n" << kTemplates[i].body;
            n++;
        }
        std::printf("console: %d shader files written to %s (existing files are left as they are); setShader(\"grayscale\") etc. now use these files\n", n, ShaderDir().c_str());
        return true;
    }
    std::string a, b;
    ss >> a >> b;
    if (sub == "new") {
        if (a.empty() || a.find_first_of("/\\.") != std::string::npos) { say("usage: shader new <name> [template]   (templates: shader templates)"); return true; }
        const Template* t = FindTemplate(b.empty() ? "blank" : b);
        if (!t) { say("no template " + b + " (shader templates)"); return true; }
        MakeDirectory(ShaderDir());
        const std::string path = ShaderDir() + "/" + a + ".frag";
        if (FileExists(path)) { say(path + " exists already; edit it, or choose another name"); return true; }
        std::ofstream(path, std::ios::binary) << "// " << t->about << "\n// Edit and save: it is compiled at once. Use it with setShader(\"" << a << "\" [, target]) or: shader apply <target> " << a << "\n" << t->body;
        const int i = ShaderIndex(a, true);
        say(path + (i >= 0 && shaders_[i].error.empty() ? " written and loaded" : " written"));
        return true;
    }
    if (sub == "reload") {
        for (CustomShader& s : shaders_) {
            if (s.path.empty() || (!a.empty() && s.name != a)) continue;
            s.stamp = 0;
        }
        shaderCheck_ = 0;
        ShadersEveryFrame();
        say("reloaded");
        return true;
    }
    if (sub == "check") {
        const int i = ShaderIndex(a, true);
        say(i < 0 ? "no shader " + a : shaders_[i].error.empty() ? a + ": compiles" : a + ": " + shaders_[i].error);
        return true;
    }
    if (sub == "apply") {
        const int i = b.empty() ? -1 : ShaderByName(b);
        if (a.empty() || i < 0) { say("usage: shader apply <target> <name>   (a target: background, foreground, trajectory, particles, an object, a sprite)"); return true; }
        SetRenderOverride(a, 8, (float)i, 0, 0);
        say("shader " + b + " on " + a);
        return true;
    }
    if (sub == "param") {
        std::string rest; std::getline(ss, rest);
        float v[4] = {0, 0, 0, 0};
        const int n = std::sscanf(rest.c_str(), "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]);
        if (a.empty() || b.empty() || n < 1) { say("usage: shader param <name> <slot 0..3> x [y z w]"); return true; }
        SetShaderParam(a, std::atoi(b.c_str()), v[0], v[1], v[2], v[3]);
        say(a + " u_param" + b + " set");
        return true;
    }
    return true;
}

// every quad of a custom shader in the frame list is drawn with its program (FlushZ asks for it)
const float* GamePlatform::ShaderParams(int index) const { return index >= 0 && index < (int)shaders_.size() ? shaders_[index].params : nullptr; }
int GamePlatform::ShaderProgram(int index) const { return index >= 0 && index < (int)shaders_.size() ? shaders_[index].program : -1; }

}  // namespace ab
