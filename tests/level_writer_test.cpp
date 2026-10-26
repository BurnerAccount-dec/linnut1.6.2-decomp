// The editor's saveLevel: writes `objects` as a Lua file that loadLevel reads back, over the old file.
#include <cstdio>
#include <fstream>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#include "engine/level_writer.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    // the scripts' environment: `objects` as the game scripts leave it after loading a level, plus a runtime table that must not be saved
    const char* setup =
        "objects = {theme = 'theme2', physicsToWorld = 20, counts = {Wood = 2}, joints = {}, themeSprites = {}, deadBlocks = {x = 1},\n"
        "  castleCameraData = {px = 1.5, ipad = {sx = 0.5}},\n"
        "  world = {\n"
        "    ground = {name = 'ground', definition = 'Ground', x = 5, y = 6, angle = 0, material = 'immovable'},\n"
        "    Wood_1 = {name = 'Wood_1', definition = 'Wood', x = 1.25, y = -2, angle = 0.5, width = 4, material = 'wood', mass = 9, startNumber = 3}}}\n";
    CHECK(luaL_dostring(L, setup) == 0);
    lua_getglobal(L, "_G");
    lua_setfield(L, LUA_REGISTRYINDEX, "ab.env");
    // what the file held that `objects` does not keep
    CHECK(luaL_dostring(L, "lastLevel = {trainCarts = {{x = 7}}, weather = 'snow', world = {}}") == 0);
    lua_getglobal(L, "lastLevel");
    lua_setfield(L, LUA_REGISTRYINDEX, "ab.lastLevel");

    std::string path = "level_writer_test.lua", error;
    { std::ofstream old(path); old << "old content"; }                        // a previous copy of the level
    CHECK(ab::WriteLevelFile(L, path, false, error));
    CHECK(error.empty());

    // read back in a fresh environment, as loadLevel does
    CHECK(luaL_loadfile(L, path.c_str()) == 0);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfenv(L, -3);
    lua_pushvalue(L, -2);
    CHECK(lua_pcall(L, 0, 0, 0) == 0);
    int level = lua_gettop(L);
    auto num = [&](const char* expr) { lua_pushstring(L, expr); lua_gettable(L, level); double v = lua_tonumber(L, -1); lua_pop(L, 1); return v; };
    CHECK(num("physicsToWorld") == 20);
    lua_getfield(L, level, "theme"); CHECK(std::string(lua_tostring(L, -1)) == "theme2"); lua_pop(L, 1);
    lua_getfield(L, level, "weather"); CHECK(std::string(lua_tostring(L, -1)) == "snow"); lua_pop(L, 1);
    lua_getfield(L, level, "deadBlocks"); CHECK(lua_isnil(L, -1)); lua_pop(L, 1);          // runtime tables stay out
    lua_getfield(L, level, "themeSprites"); CHECK(lua_isnil(L, -1)); lua_pop(L, 1);        // an empty list is not written
    lua_getfield(L, level, "castleCameraData"); lua_getfield(L, -1, "ipad"); lua_getfield(L, -1, "sx");
    CHECK(lua_tonumber(L, -1) == 0.5); lua_pop(L, 3);
    lua_getfield(L, level, "trainCarts"); lua_rawgeti(L, -1, 1); lua_getfield(L, -1, "x");
    CHECK(lua_tonumber(L, -1) == 7); lua_pop(L, 3);
    lua_getfield(L, level, "world");
    lua_getfield(L, -1, "Wood_1");
    CHECK(lua_istable(L, -1));
    lua_getfield(L, -1, "x"); CHECK(lua_tonumber(L, -1) == 1.25); lua_pop(L, 1);
    lua_getfield(L, -1, "startNumber"); CHECK(lua_tonumber(L, -1) == 3); lua_pop(L, 1);
    lua_getfield(L, -1, "mass"); CHECK(lua_isnil(L, -1)); lua_pop(L, 1);                  // only the authored fields
    lua_getfield(L, -1, "material"); CHECK(lua_isnil(L, -1)); lua_pop(L, 1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "ground"); CHECK(lua_istable(L, -1)); lua_pop(L, 2);

    // a Seasons level also keeps the strength of each block
    CHECK(luaL_dostring(L, "objects.world.Wood_1.strength = 12") == 0);
    CHECK(ab::WriteLevelFile(L, path, true, error));
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("[\"strength\"] = 12") != std::string::npos);
    CHECK(!ab::WriteLevelFile(L, "no_such_folder/level.lua", false, error) && !error.empty());

    std::remove(path.c_str());
    lua_close(L);
    std::printf(failures ? "%d FAILED\n" : "level_writer: all passed\n", failures);
    return failures ? 1 : 0;
}
