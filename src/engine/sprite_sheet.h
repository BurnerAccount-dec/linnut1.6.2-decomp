// Sprite sheet (.dat, "KA3D") reader. Original: game::SpriteSheet::load (0x439e00), and the lookups
// game::Resources::spriteQuery a/b/c/d (0x435260 / 0x435360 / 0x435460 / 0x435560), which return the
// raw signed 16-bit width, height, pivotX, pivotY of a sprite found by name (across all loaded sheets).
//
// File layout (all numbers big endian):
//   "KA3D"  u32 length (file size - 8)
//   "SPRT"  u32 chunk size
//   u16 imageCount (1), per image: u16 nameLength, name           e.g. "INGAME_BLOCKS_1.png"
//   u16 spriteCount, per sprite: u16 nameLength, name, then s16 x, y, w, h, pivotX, pivotY
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ab {

struct Sprite {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0, pivotX = 0, pivotY = 0;
};

struct SpriteSheet {
    std::string image;                 // the png this sheet cuts from
    std::string directory;             // the folder the .dat was loaded from (the png is beside it)
    std::vector<Sprite> sprites;
};

// One composite sprite (a "COMP" entry of a *_composprites.dat file): sprites placed relative to one
// origin. The size and origin are computed from the parts' sprites (0x43af10).
struct CompoPart { std::string sprite; int x = 0, y = 0; };
struct CompoSprite {
    std::string name;
    std::vector<CompoPart> parts;
};

// Parses a *_composprites.dat file image.
bool ParseCompoSpriteSet(const std::vector<uint8_t>& bytes, std::vector<CompoSprite>& out, std::string* error);

// Parses a .dat file image. Returns false (with a reason in *error) if it is malformed.
bool ParseSpriteSheet(const std::vector<uint8_t>& bytes, SpriteSheet& out, std::string* error);

// All loaded sheets, looked up by sprite name like the exe's hashtable.
class SpriteLibrary {
public:
    bool Load(const std::string& key, const std::string& file, std::string* error);
    void Release(const std::string& key);
    const Sprite* Find(const std::string& sprite) const;
    // Same, and also the sheet it belongs to.
    const Sprite* Find(const std::string& sprite, const SpriteSheet** sheet) const;

    // Composite sprites, looked up by name in the loaded sets.
    bool LoadCompoSet(const std::string& key, const std::string& file, std::string* error);
    void ReleaseCompoSet(const std::string& key) { compoSets_.erase(key); }
    const CompoSprite* FindCompo(const std::string& name) const;
    // Size and origin of a composite: the union of its parts' rectangles (each part's sprite is placed
    // at (x, y) minus its pivot). width/height are the extent, originX/originY the distance of the
    // composite's (0, 0) from the top left corner.
    void CompoExtent(const CompoSprite& c, int& width, int& height, int& originX, int& originY) const;
private:
    std::map<std::string, SpriteSheet> sheets_;
    std::map<std::string, std::vector<CompoSprite>> compoSets_;
};

}  // namespace ab
