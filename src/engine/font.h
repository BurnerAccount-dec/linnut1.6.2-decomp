// Bitmap fonts (.dat "KA3D"/"FONT", game::BitmapFont::load 0x43c2a0) and text groups (.dat "KA3D"/"TEXT").
//
// FONT file, big endian:
//   "KA3D" u32 length, "FONT" u32 chunk size, u16 imageCount (1) + name ("FONT_BASIC_IPAD.png", beside the .dat),
//   s16 leading (+0x40), s16 tracking (+0x34), u16 glyphCount, then per glyph six 16-bit values:
//   unicode code, x, y, w, h, ascent. Each glyph is a sprite with its pivot at (0, ascent).
//   The font keeps maxAscent = max(ascent) (+0x38) and maxDescent = max(h - ascent) (+0x3c).
// Width of a string (0x43d6e0): sum of the glyph widths + (characters - 1) * tracking; characters without
// a glyph add nothing to the sum. Drawing (0x43d260): the baseline is y + maxAscent (TOP), y + ascent -
// (maxAscent + maxDescent) / 2 (VCENTER) or y - maxDescent (BOTTOM); x is moved by -width / 2 (HCENTER) or
// -width (RIGHT); then every glyph is drawn with its pivot at the pen and the pen moves by w + tracking.
//
// TEXT file: "KA3D" u32 length, "TEXT" u32 chunk size, u16 1, chunks "LDAT" (locale names), "LIDS"
// (u16 count, then u16-length ids) and "TXGP" (one u16-length UTF-8 string per id).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/sprite_sheet.h"

namespace ab {

struct BitmapFont {
    std::string image, directory;
    int leading = 0, tracking = 0, maxAscent = 0, maxDescent = 0;
    std::map<uint32_t, Sprite> glyphs;      // keyed by code point; pivotY = ascent

    float StringWidth(const std::string& utf8) const;
};

bool ParseBitmapFont(const std::vector<uint8_t>& bytes, BitmapFont& out, std::string* error);

// A text group: id -> string for the locale in the file.
struct TextGroup {
    std::map<std::string, std::string> strings;
};
bool ParseTextGroup(const std::vector<uint8_t>& bytes, TextGroup& out, std::string* error);

// UTF-8 to code points (invalid bytes become U+FFFD).
std::vector<uint32_t> DecodeUtf8(const std::string& text);

}  // namespace ab
