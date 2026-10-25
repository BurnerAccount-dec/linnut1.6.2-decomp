#include "engine/font.h"

namespace ab {

namespace {

struct Reader {
    const std::vector<uint8_t>& b;
    size_t pos = 0;
    bool ok = true;
    explicit Reader(const std::vector<uint8_t>& bytes) : b(bytes) {}
    uint32_t U16() {
        if (pos + 2 > b.size()) { ok = false; return 0; }
        uint32_t v = (b[pos] << 8) | b[pos + 1];
        pos += 2;
        return v;
    }
    uint32_t U32() { uint32_t hi = U16(); return (hi << 16) | U16(); }
    int S16() { uint32_t v = U16(); return v >= 0x8000 ? (int)v - 0x10000 : (int)v; }
    std::string Bytes(size_t n) {
        if (pos + n > b.size()) { ok = false; return std::string(); }
        std::string s((const char*)&b[pos], n);
        pos += n;
        return s;
    }
};

bool Fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}

}  // namespace

std::vector<uint32_t> DecodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0 || i + extra >= s.size() + (extra == 0 ? 1 : 0)) {
            out.push_back(0xFFFD);
            i++;
            continue;
        }
        uint32_t cp = extra == 0 ? c : (c & (0x3F >> extra));
        for (int k = 1; k <= extra; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

float BitmapFont::StringWidth(const std::string& utf8) const {
    std::vector<uint32_t> chars = DecodeUtf8(utf8);
    int sum = 0;
    for (size_t i = 0; i < chars.size(); i++) {
        auto it = glyphs.find(chars[i]);
        if (it != glyphs.end()) sum += it->second.w;
    }
    return (float)(((int)chars.size() - 1) * tracking + sum);
}

bool ParseBitmapFont(const std::vector<uint8_t>& bytes, BitmapFont& out, std::string* error) {
    Reader r(bytes);
    if (r.Bytes(4) != "KA3D") return Fail(error, "not a KA3D file");
    r.U32();
    if (r.Bytes(4) != "FONT") return Fail(error, "not a font");
    r.U32();
    unsigned images = r.U16();
    if (images != 1) return Fail(error, "font with other than one image");
    out.image = r.Bytes(r.U16());
    out.leading = r.S16();
    out.tracking = r.S16();
    unsigned count = r.U16();
    out.glyphs.clear();
    out.maxAscent = out.maxDescent = 0;
    for (unsigned i = 0; i < count && r.ok; i++) {
        uint32_t code = r.U16();
        Sprite s;
        s.x = r.S16(); s.y = r.S16(); s.w = r.S16(); s.h = r.S16();
        int ascent = r.S16();
        s.pivotX = 0;
        s.pivotY = ascent;
        if (ascent > out.maxAscent) out.maxAscent = ascent;
        if (s.h - ascent > out.maxDescent) out.maxDescent = s.h - ascent;
        out.glyphs[code] = s;
    }
    return r.ok ? true : Fail(error, "font file is cut short");
}

bool ParseTextGroup(const std::vector<uint8_t>& bytes, TextGroup& out, std::string* error) {
    Reader r(bytes);
    if (r.Bytes(4) != "KA3D") return Fail(error, "not a KA3D file");
    r.U32();
    if (r.Bytes(4) != "TEXT") return Fail(error, "not a text file");
    r.U32();
    r.U16();
    std::vector<std::string> ids;
    bool gotGroup = false;               // 2.0.0 has one TXGP per locale (en_EN first); the first is used
    out.strings.clear();
    while (r.ok && r.pos + 8 <= bytes.size()) {
        std::string tag = r.Bytes(4);
        uint32_t size = r.U32();
        size_t end = r.pos + size;
        if (tag == "LIDS") {
            unsigned n = r.U16();
            for (unsigned i = 0; i < n && r.ok; i++) ids.push_back(r.Bytes(r.U16()));
        } else if (tag == "TXGP" && !gotGroup) {
            gotGroup = true;
            for (size_t i = 0; i < ids.size() && r.ok && r.pos < end; i++) out.strings[ids[i]] = r.Bytes(r.U16());
        }
        r.pos = end;
    }
    return r.ok ? true : Fail(error, "text file is cut short");
}

}  // namespace ab
