// Loads a real level through the real scripts and lets the physics run.
// Usage: ab_level_test <scripts folder> <data folder> <level, e.g. levels/pack1/Level1.lua> [seconds] [birdVx birdVy]  (fires RedBird_4, the bird the script puts on the slingshot)
//
// It boots the scripts (3000 frames, as ab_script_boot does), calls the script's loadLevelInternal,
// then runs the engine frame update and reports how the level settles.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine/game_platform.h"

extern "C" {
#include "lauxlib.h"
}

using namespace ab;

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: %s <scripts folder> <data folder> <level path> [seconds]\n", argv[0]); return 2; }
    BootConfig cfg;
    cfg.scriptPath = argv[1];
    cfg.imagePath = "images/pc_build";
    cfg.fontPath = "fonts/pc_build";   // a key of loadlist.lua; the sprite sheets are under <data>/images/pc_build
    GamePlatform platform(argv[2]);
    ScriptHost scripts(platform, cfg);
    platform.Attach(scripts);
    if (!scripts.LoadScripts()) { std::printf("LOAD FAILED: %s\n", scripts.LastError().c_str()); return 1; }
    if (!scripts.CreateStartUpAssets()) { std::printf("createStartUpAssets FAILED: %s\n", scripts.LastError().c_str()); return 1; }
    for (int i = 0; i < 3000; i++)
        if (!scripts.Update(1.0f / 30.0f, 1.0f / 30.0f)) { std::printf("boot update FAILED: %s\n", scripts.LastError().c_str()); return 1; }
    std::printf("booted\n");

    lua_State* L = scripts.State();
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "loadLevelInternal");
    if (!lua_isfunction(L, -1)) { std::printf("no loadLevelInternal\n"); return 1; }
    lua_pushstring(L, argv[3]);
    int rc = lua_pcall(L, 1, 0, 0);
    if (rc != 0) { std::printf("loadLevelInternal FAILED: %s\n", lua_tostring(L, -1)); return 1; }
    lua_pop(L, 1);
    std::printf("level loaded: %d bodies, %d joints\n", platform.Physics().BodyCount(), (int)platform.Physics().Frame().joints.size());

    for (const char* n : {"RedBird_4", "RedBird_3", "RedBird_4"}) {
        PhysObject* o = platform.Physics().Find(n);
        if (o) std::printf("%s is %s\n", n, o->b2->GetType() == b2_staticBody ? "static" : "dynamic");
    }
    std::vector<float> startX, startY;
    for (size_t i = 0; i < platform.Physics().Frame().objects.size(); i++) {
        startX.push_back(platform.Physics().Frame().objects[i]->body->PosX());
        startY.push_back(platform.Physics().Frame().objects[i]->body->PosY());
    }
    int awakeAtStart = 0;
    for (size_t i = 0; i < platform.Physics().Frame().objects.size(); i++)
        if (platform.Physics().Frame().objects[i]->body->IsAwake()) awakeAtStart++;
    std::printf("awake right after loading: %d\n", awakeAtStart);
    // The saved positions come from a settled level, so waking everything up and letting it run
    // should move nothing far: a check of friction, restitution and the solver constants.
    for (size_t i = 0; i < platform.Physics().Frame().objects.size(); i++)
        platform.Physics().SetSleeping(platform.Physics().NameOf(platform.Physics().Frame().objects[i]->id), false);
    platform.Physics().SetRunning(true);
    if (argc > 6) {   // fire the start bird (RedBird_4) at the castle
        platform.Physics().SetVelocity("RedBird_4", (float)std::atof(argv[5]), (float)std::atof(argv[6]));
        std::printf("RedBird_4 fired with (%s, %s)\n", argv[5], argv[6]);
    }
    float seconds = argc > 4 ? (float)std::atof(argv[4]) : 10.0f;
    int frames = (int)(seconds * 60);
    for (int i = 0; i < frames; i++) {
        platform.RunFrame(1.0f / 60.0f);   // not exactly 1/30: the update steps only while the accumulator is above 1/30
        if (!scripts.LastError().empty()) { std::printf("frame %d: %s\n", i, scripts.LastError().c_str()); break; }
    }
    int awake = 0;
    float maxSpeed = 0;
    for (size_t i = 0; i < platform.Physics().Frame().objects.size(); i++) {
        FrameObject* o = platform.Physics().Frame().objects[i];
        if (o->body->IsAwake()) awake++;
        float s = std::sqrt(o->body->VelX() * o->body->VelX() + o->body->VelY() * o->body->VelY());
        if (s > maxSpeed) maxSpeed = s;
    }
    // how far did things move from where the level file put them?
    float worst = 0;
    std::string worstName;
    for (size_t i = 0; i < platform.Physics().Frame().objects.size(); i++) {
        FrameObject* o = platform.Physics().Frame().objects[i];
        float dx = o->body->PosX() - startX[i], dy = o->body->PosY() - startY[i];
        float d = std::sqrt(dx * dx + dy * dy);
        if (d > 0.05f) std::printf("  moved %.3f: %s\n", d, platform.Physics().NameOf(o->id).c_str());
        if (d > worst) { worst = d; worstName = platform.Physics().NameOf(o->id); }
    }
    std::printf("largest move from the saved position: %.6f (%s)\n", worst, worstName.c_str());
    std::printf("after %.1f s: %d of %d bodies awake, max speed %.3f\n", seconds, awake,
                (int)platform.Physics().Frame().objects.size(), maxSpeed);
    if (argc > 6) {
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "scoreTable");
        lua_getfield(L, -1, "blocks");
        lua_getfield(L, -1, "score");
        std::printf("scoreTable.blocks.score = %g\n", lua_tonumber(L, -1));
        lua_pop(L, 4);
        std::printf("birdCollision / blockCollision calls: %d / %d\n", platform.birdCollisions, platform.blockCollisions);
    }
    return 0;
}
