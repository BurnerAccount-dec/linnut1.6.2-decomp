#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include "engine/font.h"
#include <cstring>

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
    // Angry Birds 1.0 (iPhone) keeps its fonts without the KA3D header: the file starts at the image name's length
    const bool headerless = bytes.size() > 6 && !(bytes[0] == 'K' && bytes[1] == 'A') && bytes[0] == 0 && bytes[1] > 4 && bytes.size() >= (size_t)(2 + bytes[1]) && bytes[2 + bytes[1] - 4] == '.';
    if (std::getenv("AB_FONT_DEBUG")) std::printf("font headerless=%d size=%zu b0=%d b1=%d\n", (int)headerless, bytes.size(), bytes.empty() ? -1 : bytes[0], bytes.size() > 1 ? bytes[1] : -1);
    if (!headerless) {
        if (r.Bytes(4) != "KA3D") return Fail(error, "not a KA3D file");
        r.U32();
        if (r.Bytes(4) != "FONT") return Fail(error, "not a font");
        r.U32();
        unsigned images = r.U16();
        if (images != 1) return Fail(error, "font with other than one image");
    }
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
    if (r.ok && out.glyphs.size() > 300) {
        // v3.1: the international fonts (500+ glyphs: Angry Birds HD, 1.3.5, 2.0.0, 3.3.0) carry accented capitals and hooked letters that reach far
        // above and below the text, which made getFontHeight (and the gap between two lines placed with it, such as Highscore and Score) too big:
        // their height comes from the basic Latin letters
        int a = 0, d = 0;
        for (const auto& g : out.glyphs) if (g.first < 128) { a = std::max(a, g.second.pivotY); d = std::max(d, g.second.h - g.second.pivotY); }
        if (a + d > 0) { out.maxAscent = a; out.maxDescent = d; }
    }
    return r.ok ? true : Fail(error, "font file is cut short");
}

// The Android builds (1.3.x) keep their texts in an older layout without the KA3D header: 5 bytes, the number of locales, the locale names
// (u16 length + text), the number of ids, the ids, then one u32 offset per locale (counted from the end of the first of them) and, at each
// offset, one u16-length string per id. The first locale (en_EN) is used.
static bool ParseLegacyTextGroup(const std::vector<uint8_t>& b, TextGroup& out) {
    auto u16 = [&](size_t at) { return at + 2 <= b.size() ? ((unsigned)b[at] << 8 | b[at + 1]) : 0u; };
    auto u32 = [&](size_t at) { return at + 4 <= b.size() ? ((uint32_t)b[at] << 24 | (uint32_t)b[at + 1] << 16 | (uint32_t)b[at + 2] << 8 | b[at + 3]) : 0u; };
    if (b.size() < 16) return false;
    size_t p = 6;
    for (unsigned i = 0; i < b[5]; i++) { p += 2 + u16(p); if (p > b.size()) return false; }
    unsigned count = u16(p); p += 2;
    std::vector<std::string> ids;
    for (unsigned i = 0; i < count; i++) {
        unsigned n = u16(p); p += 2;
        if (p + n > b.size()) return false;
        ids.push_back(std::string((const char*)&b[p], n)); p += n;
    }
    if (b[5] == 0) return false;
    out.strings.clear(); out.locales.clear(); out.localeNames.clear();
    size_t names = 6;
    for (unsigned l = 0; l < b[5]; l++) {
        std::string name((const char*)&b[names + 2], u16(names));
        names += 2 + u16(names);
        size_t at = p + 4 + u32(p + 4 * l);
        std::map<std::string, std::string> texts;
        bool ok = true;
        for (size_t i = 0; i < ids.size() && ok; i++) {
            unsigned n = u16(at); at += 2;
            if (at + n > b.size()) { ok = false; break; }
            texts[ids[i]] = std::string((const char*)&b[at], n); at += n;
        }
        if (!ok && l == 0) return false;
        if (!ok) break;
        out.localeNames.push_back(name); out.locales[name] = texts;
        if (l == 0) out.strings = texts;
    }
    return true;
}

bool ParseTextGroup(const std::vector<uint8_t>& bytes, TextGroup& out, std::string* error) {
    if (bytes.size() > 16 && std::memcmp(bytes.data(), "KA3D", 4) != 0 && ParseLegacyTextGroup(bytes, out)) return true;
    Reader r(bytes);
    if (r.Bytes(4) != "KA3D") return Fail(error, "not a KA3D file");
    r.U32();
    if (r.Bytes(4) != "TEXT") return Fail(error, "not a text file");
    r.U32();
    r.U16();
    std::vector<std::string> ids;
    size_t groups = 0;                   // 2.0.0 has one TXGP per locale (named in LDAT, en_EN first); the first is used until another is picked
    out.strings.clear(); out.locales.clear(); out.localeNames.clear();
    while (r.ok && r.pos + 8 <= bytes.size()) {
        std::string tag = r.Bytes(4);
        uint32_t size = r.U32();
        size_t end = r.pos + size;
        if (tag == "LIDS") {
            unsigned n = r.U16();
            for (unsigned i = 0; i < n && r.ok; i++) ids.push_back(r.Bytes(r.U16()));
        } else if (tag == "LDAT") {
            unsigned n = r.U16();
            for (unsigned i = 0; i < n && r.ok; i++) out.localeNames.push_back(r.Bytes(r.U16()));
        } else if (tag == "TXGP") {
            std::map<std::string, std::string> texts;
            for (size_t i = 0; i < ids.size() && r.ok && r.pos < end; i++) texts[ids[i]] = r.Bytes(r.U16());
            if (groups == 0) out.strings = texts;
            if (groups < out.localeNames.size()) out.locales[out.localeNames[groups]] = texts;
            groups++;
        }
        r.pos = end;
    }
    return r.ok ? true : Fail(error, "text file is cut short");
}

}  // namespace ab
