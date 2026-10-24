#include "lua/script_host.h"

#include <cstdio>

extern "C" {
#include "lauxlib.h"
#include "lualib.h"
}

#include "lua/lua_api.h"

namespace ab {

namespace {
int MissingGlobalHook(lua_State* L) {
    // _G.__index(key): remember the key, return nil
    ScriptHost* host = static_cast<ScriptHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (lua_type(L, 2) == LUA_TSTRING) {
        const std::string name = lua_tostring(L, 2);
        const std::vector<std::string>& m = host->MissingGlobals();
        bool seen = false;
        for (size_t i = 0; i < m.size(); i++) if (m[i] == name) seen = true;
        if (!seen) const_cast<std::vector<std::string>&>(m).push_back(name);
    }
    return 0;
}
}  // namespace

ScriptHost::ScriptHost(Platform& platform, const BootConfig& config)
    : platform_(platform), config_(config), L_(luaL_newstate()) {
    luaL_openlibs(L_);

    // The scripts run in their own environment table (the exe's Game root table, `this`). Engine
    // functions and values are visible there by bare name; the standard libraries, `res` and
    // `particles` are only reachable through `_G`, which is the real global table.
    lua_newtable(L_);
    lua_pushvalue(L_, -1);
    lua_setfield(L_, LUA_REGISTRYINDEX, "ab.env");
    lua_pushvalue(L_, LUA_GLOBALSINDEX);
    lua_setfield(L_, -2, "_G");
    lua_pushvalue(L_, -1);
    lua_setfield(L_, -2, "this");
    // options.lua sets gamelua.releaseBuild, gamelua.gameVersionNumber ...; the scripts read them by bare
    // name, so gamelua is the environment itself
    lua_pushvalue(L_, -1);
    lua_setfield(L_, -2, "gamelua");
    lua_pop(L_, 1);

    RegisterApi(L_, &platform_);

    SetString("scriptPath", config_.scriptPath);
    SetString("imagePath", config_.imagePath);
    SetString("fontPath", config_.fontPath);
    SetString("audioPath", config_.audioPath);
    SetString("localizationPath", config_.localizationPath);
    SetString("levelPath", config_.levelPath);
    SetString("deviceModel", config_.deviceModel);
    lua_pushboolean(L_, config_.isLiteVersion);
    SetEnvField("isLiteVersion");
    lua_pushboolean(L_, config_.isBetaVersion);
    SetEnvField("isBetaVersion");
    lua_pushboolean(L_, 0);
    SetEnvField("roku");
    lua_pushboolean(L_, 0);
    SetEnvField("rokuOnWindows");
    lua_pushnumber(L_, config_.screenWidth);
    SetEnvField("screenWidth");
    lua_pushnumber(L_, config_.screenHeight);
    SetEnvField("screenHeight");
    lua_pushnumber(L_, 1);
    SetEnvField("zoomLevel");
    lua_pushnumber(L_, 0);
    SetEnvField("touchcount");

    const char* tables[] = {"keyPressed", "keyReleased", "keyHold", "touches", "multitouchSweep", "multitouchZoom",
                            "clippedText", "objects"};
    for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) NewGlobalTable(tables[i]);

    // The touch gesture tables: no zoom is cooling down, no sweep is going on (the scripts compare these directly).
    GetEnvField("multitouchZoom");
    lua_pushnumber(L_, -1); lua_setfield(L_, -2, "zoomCoolingTime");
    lua_pop(L_, 1);
    GetEnvField("multitouchSweep");
    lua_pushboolean(L_, 0); lua_setfield(L_, -2, "isSweepping");
    lua_pushnumber(L_, 0); lua_setfield(L_, -2, "sweepSpeed");
    lua_pop(L_, 1);

    // cursor { x, y, wheel, wheelTriggered }
    lua_newtable(L_);
    lua_pushnumber(L_, 0); lua_setfield(L_, -2, "x");
    lua_pushnumber(L_, 0); lua_setfield(L_, -2, "y");
    lua_pushnumber(L_, 0); lua_setfield(L_, -2, "wheel");
    lua_pushboolean(L_, 0); lua_setfield(L_, -2, "wheelTriggered");
    SetEnvField("cursor");

    // objects.world / loadedObjects / joints / counts are filled by the level loader and the scripts
    GetEnvField("objects");
    const char* sub[] = {"world", "loadedObjects", "joints", "counts", "deadBlocks"};
    for (size_t i = 0; i < sizeof(sub) / sizeof(sub[0]); i++) {
        lua_newtable(L_);
        lua_setfield(L_, -2, sub[i]);
    }
    lua_pop(L_, 1);
    lua_newtable(L_);
    SetEnvField("deadBlocks");

    LoadPersisted("settings", "settings.lua");
    LoadPersisted("highscores", "highscores.lua");
}

ScriptHost::~ScriptHost() { lua_close(L_); }

// Pops the top value into the script environment under `name`.
void ScriptHost::SetEnvField(const char* name) {
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    lua_insert(L_, -2);
    lua_setfield(L_, -2, name);
    lua_pop(L_, 1);
}

// Pushes the value of `name` in the script environment.
void ScriptHost::GetEnvField(const char* name) {
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L_, -1, name);
    lua_remove(L_, -2);
}

// Loads a Lua file so that it runs in the script environment. Leaves the function on the stack.
int ScriptHost::LoadInEnv(const std::string& path) {
    int rc = luaL_loadfile(L_, path.c_str());
    if (rc == 0) {
        lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
        lua_setfenv(L_, -2);
    }
    return rc;
}

void ScriptHost::SetString(const char* name, const std::string& v) {
    lua_pushlstring(L_, v.data(), v.size());
    SetEnvField(name);
}

void ScriptHost::NewGlobalTable(const char* name) {
    lua_newtable(L_);
    SetEnvField(name);
}

// settings.lua / highscores.lua define a global of the same name; a missing file means an empty table.
void ScriptHost::LoadPersisted(const char* tableName, const char* fileName) {
    std::string path = platform_.ResolveUserFile(fileName, true);
    if (LoadInEnv(path) == 0) {
        if (lua_pcall(L_, 0, 0, 0) != 0) {
            platform_.Print(std::string("cannot run ") + path + ": " + lua_tostring(L_, -1));
            lua_pop(L_, 1);
        }
    } else {
        lua_pop(L_, 1);
    }
    GetEnvField(tableName);
    bool isTable = lua_istable(L_, -1);
    lua_pop(L_, 1);
    if (!isTable) NewGlobalTable(tableName);
}

bool ScriptHost::RunFile(const std::string& path) {
    if (LoadInEnv(path) != 0 || lua_pcall(L_, 0, 0, 0) != 0) {
        error_ = path + ": " + lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool ScriptHost::LoadScripts() {
    const char* files[] = {"/loadlist.lua", "/gamelogic.lua"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (!RunFile(config_.scriptPath + files[i])) return false;
    }
    // starLimits.lua, blocks.lua and particles.lua are data files: each fills its own table
    if (!LoadTableFile(L_, "starTable", config_.scriptPath + "/starLimits.lua", &error_)) return false;
    if (!LoadTableFile(L_, "blockTable", config_.scriptPath + "/blocks.lua", &error_)) return false;
    if (!LoadTableFile(L_, "particleTable", config_.scriptPath + "/particles.lua", &error_)) return false;
    return true;
}

void ScriptHost::TraceMissingGlobals(bool on) {
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    if (on) {
        lua_newtable(L_);
        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, MissingGlobalHook, 1);
        lua_setfield(L_, -2, "__index");
        lua_setmetatable(L_, -2);
    } else {
        lua_pushnil(L_);
        lua_setmetatable(L_, -2);
    }
    lua_pop(L_, 1);
}

bool ScriptHost::CallWithArgs(const char* name, int nargs) {
    // the function and its arguments are pushed by the caller; debug.traceback is the error handler
    int base = lua_gettop(L_) - nargs - 1;
    lua_getglobal(L_, "debug");
    lua_getfield(L_, -1, "traceback");
    lua_remove(L_, -2);
    lua_insert(L_, base + 1);
    if (lua_pcall(L_, nargs, 0, base + 1) != 0) {
        error_ = std::string(name) + ": " + lua_tostring(L_, -1);
        lua_pop(L_, 2);
        return false;
    }
    lua_pop(L_, 1);
    return true;
}

bool ScriptHost::Call(const char* name) {
    GetEnvField(name);
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    return CallWithArgs(name, 0);
}

bool ScriptHost::Update(float dt, float realDt) {
    GetEnvField("update");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushnumber(L_, dt);
    lua_pushnumber(L_, realDt);
    return CallWithArgs("update", 2);
}

bool ScriptHost::BirdCollision(const std::string& a, const std::string& b, float force, float damage) {
    GetEnvField("birdCollision");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushlstring(L_, a.data(), a.size());
    lua_pushlstring(L_, b.data(), b.size());
    lua_pushnumber(L_, force);
    lua_pushnumber(L_, damage);
    return CallWithArgs("birdCollision", 4);
}

bool ScriptHost::BlockCollision(const std::string& a, const std::string& b, float force, bool wasDamageDone) {
    GetEnvField("blockCollision");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushlstring(L_, a.data(), a.size());
    lua_pushlstring(L_, b.data(), b.size());
    lua_pushnumber(L_, force);
    lua_pushboolean(L_, wasDamageDone);
    return CallWithArgs("blockCollision", 4);
}

bool ScriptHost::ResolutionChanged(float w, float h) {
    GetEnvField("resolutionChanged");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushnumber(L_, w);
    lua_pushnumber(L_, h);
    return CallWithArgs("resolutionChanged", 2);
}

}  // namespace ab
