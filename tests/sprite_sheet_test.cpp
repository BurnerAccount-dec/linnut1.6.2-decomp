// Checks the sprite sheet reader against the three real sheets (values read out of the files).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/sprite_sheet.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/data";
    ab::SpriteLibrary lib;
    std::string err;
    CHECK(lib.Load("blocks", dir + "/images/pc_build/INGAME_BLOCKS_1.dat", &err));
    CHECK(lib.Load("grounds", dir + "/images/pc_build/INGAME_GROUNDS_1.dat", &err));
    CHECK(lib.Load("birds", dir + "/images/pc_build/INGAME_BIRDS_1.dat", &err));
    const ab::Sprite* s = lib.Find("BLOCK_ROPE_THICK_1");
    CHECK(s && s->x == 375 && s->y == 1 && s->w == 12 && s->h == 133 && s->pivotX == 6 && s->pivotY == 66);
    s = lib.Find("BLOCK_WOOD_1_6");
    CHECK(s && s->w == 168 && s->h == 20 && s->pivotX == 84 && s->pivotY == 10);
    s = lib.Find("BIRD_BLUE");
    CHECK(s && s->w == 31 && s->h == 30 && s->pivotX == 16 && s->pivotY == 16);
    CHECK(!lib.Find("NO_SUCH_SPRITE"));
    CHECK(!lib.Load("bad", dir + "/nothing.dat", &err));
    lib.Release("birds");
    CHECK(!lib.Find("BIRD_BLUE"));
    if (failures == 0) std::printf("sprite_sheet: all passed\n");
    return failures ? 1 : 0;
}
