#include "engine/sprite_sheet.h"

#include <cstdio>
#include <algorithm>
#include <cstring>

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

// KA3D, total size, then chunks (tag, size); the COMP chunk: version 1, count, and per composite its
// name, its part count and per part the sprite name and x, y (loader at 0x43b7d0).
bool ParseCompoSpriteSet(const std::vector<uint8_t>& bytes, std::vector<CompoSprite>& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    Reader r{bytes};
    if (r.Str(4) != "KA3D") return fail("not a KA3D file");
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
                    p.x = r.S16();
                    p.y = r.S16();
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
    FILE* f = fopen(file.c_str(), "rb");
    if (!f) { if (error) *error = "cannot open " + file; return false; }
    std::vector<uint8_t> bytes;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    fclose(f);
    std::vector<CompoSprite> set;
    if (!ParseCompoSpriteSet(bytes, set, error)) return false;
    compoSets_[key] = std::move(set);
    compoOrder_[key] = ++loaded_;
    return true;
}

const CompoSprite* SpriteLibrary::FindCompo(const std::string& name) const {
    const CompoSprite* found = nullptr;
    int foundOrder = -1;
    for (const auto& set : compoSets_)
        for (const CompoSprite& c : set.second)
            if (c.name == name && compoOrder_.at(set.first) > foundOrder) {
                found = &c;
                foundOrder = compoOrder_.at(set.first);
            }
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
        if (r.Str(4) != "KA3D") return fail("not a KA3D file");
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
    if (found) cache_[sprite] = {found, foundSheet};
    if (sheet && found) *sheet = foundSheet;
    return found;
}

}  // namespace ab
