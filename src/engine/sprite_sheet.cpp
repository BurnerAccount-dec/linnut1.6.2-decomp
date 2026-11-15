#include "engine/sprite_sheet.h"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "lua/aes.h"

namespace ab {

namespace {
struct Reader {
    const std::vector<uint8_t>& b;
    size_t pos = 0;
    bool ok = true;
    uint32_t U32() { if (pos + 4 > b.size()) { ok = false; return 0; } uint32_t v = (b[pos] << 24) | (b[pos+1] << 16) | (b[pos+2] << 8) | b[pos+3]; pos += 4; return v; }
    uint16_t U16() { if (pos + 2 > b.size()) { ok = false; return 0; } uint16_t v = (uint16_t)((b[pos] << 8) | b[pos+1]); pos += 2; return v; }
    int16_t S16() { return (int16_t)U16(); }
    std::string Str(size_t n) { if (pos + n > b.size()) { ok = false; return {}; } std::string s((const char*)&b[pos], n); pos += n; return s; }
};
}  // namespace


// ---- v3.1: the newer sheets (Seasons 4.1.0): ArtPacker's JSON, encrypted like the scripts (ReadLuaFile decrypts it)
//   NAME.sheet.json  {"frames": [{"filename", "frame": {x, y, w, h}, "pivot": {x, y}, "rotated"}], "meta": {"image": "NAME.pvr"}}
//   NAME.compo.json  {"compo": [{"name", "sprites": [{"name", "x", "y", "angle", "scale": [sx, sy]}]}]}
namespace {
struct JVal {
    enum Kind { Null, Num, Str, Arr, Obj, Bool } kind = Null;
    double num = 0;
    std::string str;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;
    const JVal* Get(const char* key) const { for (const auto& kv : obj) if (kv.first == key) return &kv.second; return nullptr; }
    double Number(const char* key, double def) const { const JVal* v = Get(key); return v && v->kind == Num ? v->num : def; }
    std::string String(const char* key) const { const JVal* v = Get(key); return v && v->kind == Str ? v->str : std::string(); }
};
struct JParser {
    const std::string& t;
    size_t i = 0;
    bool ok = true;
    void Space() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) i++; }
    std::string Text() {
        std::string out;
        i++;                                                   // the opening quote
        while (i < t.size() && t[i] != '"') {
            if (t[i] == '\\' && i + 1 < t.size()) { i++; char c = t[i]; out += c == 'n' ? '\n' : c == 't' ? '\t' : c; }
            else out += t[i];
            i++;
        }
        i++;
        return out;
    }
    JVal Value() {
        JVal v;
        Space();
        if (i >= t.size()) { ok = false; return v; }
        char c = t[i];
        if (c == '{') {
            v.kind = JVal::Obj; i++;
            for (;;) {
                Space();
                if (i < t.size() && t[i] == '}') { i++; break; }
                if (i >= t.size() || t[i] != '"') { ok = false; break; }
                std::string key = Text();
                Space();
                if (i >= t.size() || t[i] != ':') { ok = false; break; }
                i++;
                v.obj.push_back({key, Value()});
                Space();
                if (i < t.size() && t[i] == ',') i++;
                if (!ok) break;
            }
        } else if (c == '[') {
            v.kind = JVal::Arr; i++;
            for (;;) {
                Space();
                if (i < t.size() && t[i] == ']') { i++; break; }
                v.arr.push_back(Value());
                Space();
                if (i < t.size() && t[i] == ',') i++;
                if (!ok || i >= t.size()) break;
            }
        } else if (c == '"') { v.kind = JVal::Str; v.str = Text(); }
        else if (t.compare(i, 4, "true") == 0) { v.kind = JVal::Bool; v.num = 1; i += 4; }
        else if (t.compare(i, 5, "false") == 0) { v.kind = JVal::Bool; i += 5; }
        else if (t.compare(i, 4, "null") == 0) { i += 4; }
        else { char* end = nullptr; v.kind = JVal::Num; v.num = std::strtod(t.c_str() + i, &end); if (end == t.c_str() + i) ok = false; i = end - t.c_str(); }
        return v;
    }
};
bool EndsWithJson(const std::string& f) { return f.size() > 5 && f.compare(f.size() - 5, 5, ".json") == 0; }
bool ReadJson(const std::string& file, JVal& out, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!ReadLuaFile(file, bytes) || bytes.empty()) { if (error) *error = "cannot open " + file; return false; }
    std::string text(bytes.begin(), bytes.end());
    JParser p{text};
    out = p.Value();
    if (!p.ok || out.kind != JVal::Obj) { if (error) *error = "not a JSON sheet: " + file; return false; }
    return true;
}
int Round(double v) { return (int)std::floor(v + 0.5); }
}  // namespace

bool ParseSheetJson(const std::string& file, SpriteSheet& out, std::string* error) {
    JVal root;
    if (!ReadJson(file, root, error)) return false;
    const JVal* meta = root.Get("meta");
    out.image = meta ? meta->String("image") : std::string();
    const JVal* frames = root.Get("frames");
    if (!frames || frames->kind != JVal::Arr) { if (error) *error = "no frames in " + file; return false; }
    for (const JVal& f : frames->arr) {
        Sprite s;
        s.name = f.String("filename");
        if (const JVal* r = f.Get("frame")) { s.x = Round(r->Number("x", 0)); s.y = Round(r->Number("y", 0)); s.w = Round(r->Number("w", 0)); s.h = Round(r->Number("h", 0)); }
        if (const JVal* pv = f.Get("pivot")) { s.pivotX = Round(pv->Number("x", 0)); s.pivotY = Round(pv->Number("y", 0)); }
        if (!s.name.empty()) out.sprites.push_back(s);
    }
    return true;
}

bool ParseCompoJson(const std::string& file, std::vector<CompoSprite>& out, std::string* error) {
    JVal root;
    if (!ReadJson(file, root, error)) return false;
    const JVal* list = root.Get("compo");
    if (!list || list->kind != JVal::Arr) { if (error) *error = "no compo list in " + file; return false; }
    for (const JVal& c : list->arr) {
        CompoSprite cs;
        cs.name = c.String("name");
        if (const JVal* parts = c.Get("sprites")) {
            for (const JVal& sp : parts->arr) {
                CompoPart p;
                p.sprite = sp.String("name");
                p.fx = (float)sp.Number("x", 0); p.fy = (float)sp.Number("y", 0);
                p.x = Round(p.fx); p.y = Round(p.fy);
                p.angle = (float)sp.Number("angle", 0);
                const JVal* sc = sp.Get("scale");
                float sx = 1, sy = 1;
                if (sc && sc->kind == JVal::Arr && sc->arr.size() >= 2) { sx = (float)sc->arr[0].num; sy = (float)sc->arr[1].num; }
                else if (sc && sc->kind == JVal::Num) sx = sy = (float)sc->num;
                p.scale = sx;
                p.exact = std::fabs(sx - 1) > 1e-4f || std::fabs(sy - 1) > 1e-4f || p.angle != 0;   // a scaled or turned part is placed with its own scale and angle
                cs.parts.push_back(p);
            }
        }
        out.push_back(cs);
    }
    return true;
}

// KA3D, total size, then chunks (tag, size); the COMP chunk: version 1, count, and per composite its
// name, its part count and per part the sprite name and x, y (loader at 0x43b7d0).
bool ParseCompoSpriteSet(const std::vector<uint8_t>& bytes, std::vector<CompoSprite>& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    Reader r{bytes};
    const std::string magic = r.Str(4);
    if (magic != "KA3D" && magic != "RVIO") return fail("not a KA3D file");
    // v3.1: Seasons 4.1.0's BUTTONS_COMPOSPRITES.dat starts with RVIO and gives each part 20 bytes after its name:
    // s16 (0), s16 x, s16 y, float scaleX, float scaleY, float angle, s16 (0)
    const bool rvio = magic == "RVIO";
    uint32_t total = r.U32();
    size_t end = r.pos + total;
    if (end > bytes.size()) return fail("file ends early");
    while (r.ok && r.pos + 8 <= end) {
        std::string tag = r.Str(4);
        uint32_t size = r.U32();
        size_t next = r.pos + size;
        if (tag == "COMP") {
            uint16_t ver = r.U16();
            if (ver != 1 && ver != 2) return fail("unknown COMP version");
            int count = r.S16();
            for (int i = 0; i < count && r.ok; i++) {
                CompoSprite c;
                c.name = r.Str(r.U16());
                int parts = r.S16();
                for (int j = 0; j < parts && r.ok; j++) {
                    CompoPart p;
                    p.sprite = r.Str(r.U16());
                    if (rvio) {
                        r.S16();
                        p.x = r.S16(); p.y = r.S16();
                        auto f32 = [&]() { uint32_t u = r.U32(); float v; std::memcpy(&v, &u, 4); return v; };
                        float sx = f32(), sy = f32();
                        p.angle = f32();
                        const int partFlags = r.S16();
                        p.fx = (float)p.x; p.fy = (float)p.y; p.scale = sx; p.scaleY = sy;
                        p.exact = std::fabs(sx - 1) > 1e-4f || std::fabs(sy - 1) > 1e-4f || p.angle != 0;
                        // Stella's theme composites: the angle is in degrees (5.6, 348.3 ...) and 0x100 mirrors the part (the parts are drawn about their pivot)
                        p.angle = p.angle * 3.14159265f / 180.0f;
                        p.flipX = (partFlags & 0x100) != 0;
                        if (p.flipX) p.exact = true;
                    } else {
                        p.x = r.S16();
                        p.y = r.S16();
                    }
                    c.parts.push_back(p);
                }
                if (ver == 2) r.S16();                 // v2 adds one 16-bit value per composite (0 in the shipped files)
                out.push_back(c);
            }
        }
        r.pos = next;
    }
    if (!r.ok) return fail("file ends early");
    return true;
}

bool SpriteLibrary::LoadCompoSet(const std::string& key, const std::string& file, std::string* error) {
    if (EndsWithJson(file)) {
        std::vector<CompoSprite> set;
        if (!ParseCompoJson(file, set, error)) return false;
        compoCache_.clear();
        compoSets_[key] = std::move(set);
        compoOrder_[key] = ++loaded_;
        return true;
    }
    FILE* f = fopen(file.c_str(), "rb");
    if (!f) { if (error) *error = "cannot open " + file; return false; }
    std::vector<uint8_t> bytes;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    fclose(f);
    std::vector<CompoSprite> set;
    if (!ParseCompoSpriteSet(bytes, set, error)) return false;
    compoCache_.clear();
    compoSets_[key] = std::move(set);
    compoOrder_[key] = ++loaded_;
    return true;
}

const CompoSprite* SpriteLibrary::FindCompo(const std::string& name) const {
    auto hit = compoCache_.find(name);
    if (hit != compoCache_.end()) return hit->second;
    const CompoSprite* found = nullptr;
    int foundOrder = -1;
    for (const auto& set : compoSets_)
        for (const CompoSprite& c : set.second)
            if (c.name == name && compoOrder_.at(set.first) > foundOrder) {
                found = &c;
                foundOrder = compoOrder_.at(set.first);
            }
    compoCache_[name] = found;
    return found;
}

void SpriteLibrary::CompoExtent(const CompoSprite& c, int& width, int& height, int& originX, int& originY) const {
    int minX = 0x7fffffff, minY = 0x7fffffff, maxX = -0x7fffffff - 1, maxY = -0x7fffffff - 1;
    for (const CompoPart& p : c.parts) {
        const Sprite* s = Find(p.sprite);
        if (!s) continue;
        minX = std::min(minX, p.x - s->pivotX);
        minY = std::min(minY, p.y - s->pivotY);
        maxX = std::max(maxX, p.x + s->w - s->pivotX);
        maxY = std::max(maxY, p.y + s->h - s->pivotY);
    }
    width = maxX - minX; height = maxY - minY; originX = -minX; originY = -minY;
}

bool ParseSpriteSheet(const std::vector<uint8_t>& bytes, SpriteSheet& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    Reader r{bytes};
    // Angry Birds 1.0.0 ships INGAME_SKIES_1.dat (the backgrounds of the first five packs) without the 18 byte header
    // ("KA3D", length, "SPRT", length, image count): the file starts at the image name's length, and the exe still loads it.
    bool headerless = bytes.size() > 6 && !(bytes[0] == 'K' && bytes[1] == 'A' && bytes[2] == '3' && bytes[3] == 'D') &&
                      bytes[0] == 0 && bytes[1] > 4 && bytes.size() >= (size_t)(2 + bytes[1]) &&
                      bytes[2 + bytes[1] - 4] == '.' && bytes[2 + bytes[1] - 3] == 'p';
    if (!headerless) {
        { const std::string magic = r.Str(4); if (magic != "KA3D" && magic != "RVIO") return fail("not a KA3D file"); }   // v3.1: Seasons 4.1.0 writes RVIO, the same layout
        r.U32();                               // total length
        if (r.Str(4) != "SPRT") return fail("no SPRT chunk");
        r.U32();                               // chunk size
        int images = r.U16();
        if (images != 1) return fail("expected exactly one image");
    }
    out.image = r.Str(r.U16());
    int count = r.U16();
    out.sprites.clear();
    for (int i = 0; i < count && r.ok; i++) {
        Sprite s;
        s.name = r.Str(r.U16());
        s.x = r.S16(); s.y = r.S16(); s.w = r.S16(); s.h = r.S16();
        s.pivotX = r.S16(); s.pivotY = r.S16();
        out.sprites.push_back(s);
    }
    if (!r.ok) return fail("file ends early");
    return true;
}

bool SpriteLibrary::Load(const std::string& key, const std::string& file, std::string* error) {
    if (EndsWithJson(file)) {
        SpriteSheet sheet;
        if (!ParseSheetJson(file, sheet, error)) return false;
        size_t slash = file.find_last_of("/\\");
        sheet.directory = slash == std::string::npos ? std::string(".") : file.substr(0, slash);
        sheet.order = ++loaded_;
        cache_.clear();
        sheets_[key] = std::move(sheet);
        return true;
    }
    FILE* f = fopen(file.c_str(), "rb");
    if (!f) { if (error) *error = "cannot open " + file; return false; }
    std::vector<uint8_t> bytes;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    fclose(f);
    SpriteSheet sheet;
    if (!ParseSpriteSheet(bytes, sheet, error)) return false;
    size_t slash = file.find_last_of("/\\");
    sheet.directory = slash == std::string::npos ? std::string(".") : file.substr(0, slash);
    sheet.order = ++loaded_;
    cache_.clear();
    sheets_[key] = std::move(sheet);
    return true;
}

void SpriteLibrary::Release(const std::string& key) { sheets_.erase(key); cache_.clear(); }

const Sprite* SpriteLibrary::Find(const std::string& sprite) const {
    return Find(sprite, nullptr);
}

const Sprite* SpriteLibrary::Find(const std::string& sprite, const SpriteSheet** sheet) const {
    // The Seasons PC data has the same sprite names in a full size sheet (INGAME_PARALLAX_21) and a half size
    // theme sheet (THEME_SUMMER_PARALLAX_1): the sheet loaded last is the one the game uses.
    auto hit = cache_.find(sprite);
    if (hit != cache_.end()) {
        if (sheet) *sheet = hit->second.second;
        return hit->second.first;
    }
    const Sprite* found = nullptr;
    const SpriteSheet* foundSheet = nullptr;
    for (const auto& sh : sheets_)
        for (const Sprite& s : sh.second.sprites)
            if (s.name == sprite && (!foundSheet || sh.second.order > foundSheet->order)) {
                found = &s;
                foundSheet = &sh.second;
            }
    cache_[sprite] = {found, foundSheet};      // a name that is in no sheet is remembered too: scripts draw and measure such names every frame
    if (sheet && found) *sheet = foundSheet;
    return found;
}

}  // namespace ab

namespace ab {
void SpriteLibrary::AddCompo(const CompoSprite& c) {
    compoCache_.clear();
    auto& set = compoSets_["runtime:compo"];
    for (CompoSprite& old : set) if (old.name == c.name) { old = c; return; }
    set.push_back(c);
    if (!compoOrder_.count("runtime:compo")) compoOrder_["runtime:compo"] = ++loaded_;
}
}  // namespace ab
