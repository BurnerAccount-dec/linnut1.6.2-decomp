// Save states: leave a level half played and come back to exactly that, in every game.
//
// While a level runs, the state of the level is written to a file in the save folder every few seconds and when the level is left (the game mode
// leaves `updateGame`) or the game is closed. When the same level is loaded again it is put back in the first frame the level runs. What is kept:
//   - every object of objects.world: its place, angle and speed (a body that was at rest is put to sleep), and every plain value (number, string,
//     flag) in its table: strength, damage sprite, start number and so on;
//   - the script variables named in saveStates.globals (the bird that is next, shots fired, the score ...), the tables in saveStates.tables
//     (the score table, the camera), the objects the variables in saveStates.objectGlobals point to (the flying bird) and the camera function;
//   - objects that are gone are removed again (also from the tables in saveStates.objectTables).
// A restart, finishing the level and failing it clear the state. Objects the scripts made after the level began are not part of it.
// The settings are the Lua table `saveStates` (made here with its defaults; scripts may change any field). See docs/SCRIPTING.md.
#include "lua/lua_api.h"
#include "lua/platform.h"
#include "lua/aes.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace ab {
namespace {

struct State {
    bool pending = false, ready = false, suspended = false, prevRunning = false;
    double clock = 0, lastWrite = 0, leftAt = -1e9, lastSnap = 0;
    std::string curId, lastId;
    Platform* platform = nullptr;
} g;

int AbsIdx(lua_State* L, int i) { return (i > 0 || i <= LUA_REGISTRYINDEX) ? i : lua_gettop(L) + i + 1; }
void PushEnv(lua_State* L) { lua_getfield(L, LUA_REGISTRYINDEX, "ab.env"); }
void GetG(lua_State* L, const char* name) { PushEnv(L); lua_getfield(L, -1, name); lua_remove(L, -2); }

// ---- settings
void GetCfg(lua_State* L) { GetG(L, "saveStates"); }
bool CfgBool(lua_State* L, const char* k, bool def) {
    GetCfg(L);
    bool r = def;
    if (lua_istable(L, -1)) { lua_getfield(L, -1, k); if (lua_isboolean(L, -1)) r = lua_toboolean(L, -1) != 0; lua_pop(L, 1); }
    lua_pop(L, 1);
    return r;
}
double CfgNum(lua_State* L, const char* k, double def) {
    GetCfg(L);
    double r = def;
    if (lua_istable(L, -1)) { lua_getfield(L, -1, k); if (lua_isnumber(L, -1)) r = lua_tonumber(L, -1); lua_pop(L, 1); }
    lua_pop(L, 1);
    return r;
}
std::string CfgStr(lua_State* L, const char* k, const char* def) {
    GetCfg(L);
    std::string r = def;
    if (lua_istable(L, -1)) { lua_getfield(L, -1, k); if (lua_isstring(L, -1)) r = lua_tostring(L, -1); lua_pop(L, 1); }
    lua_pop(L, 1);
    return r;
}
// the names in the list saveStates[key]
std::vector<std::string> CfgList(lua_State* L, const char* key) {
    std::vector<std::string> r;
    GetCfg(L);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, key);
        if (lua_istable(L, -1)) {
            const int n = (int)lua_objlen(L, -1);
            for (int i = 1; i <= n; i++) { lua_rawgeti(L, -1, i); if (lua_isstring(L, -1)) r.push_back(lua_tostring(L, -1)); lua_pop(L, 1); }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return r;
}

// ---- the state in memory: registry["ab.saveStates"] = { levels = { [levelPath] = state } }
void GetData(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.saveStates");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L); lua_newtable(L); lua_setfield(L, -2, "levels");
        lua_pushvalue(L, -1); lua_setfield(L, LUA_REGISTRYINDEX, "ab.saveStates");
    }
}
void Escape(std::string& out, const char* s, size_t n) {
    out += '"';
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') out += "\\\""; else if (c == '\\') out += "\\\\"; else if (c == '\n') out += "\\n"; else if (c == '\r') out += "\\r";
        else if (c < 32) { char b[8]; std::snprintf(b, sizeof b, "\\%03d", c); out += b; } else out += (char)c;
    }
    out += '"';
}
void Serialize(lua_State* L, int idx, std::string& out, int depth) {
    idx = AbsIdx(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TNUMBER: { char b[40]; std::snprintf(b, sizeof b, "%.9g", lua_tonumber(L, idx)); out += b; break; }
    case LUA_TBOOLEAN: out += lua_toboolean(L, idx) ? "true" : "false"; break;
    case LUA_TSTRING: { size_t n; const char* s = lua_tolstring(L, idx, &n); Escape(out, s, n); break; }
    case LUA_TTABLE: {
        if (depth > 8) { out += "{}"; break; }
        out += '{';
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            const int kt = lua_type(L, -2), vt = lua_type(L, -1);
            if ((kt == LUA_TSTRING || kt == LUA_TNUMBER) && (vt == LUA_TNUMBER || vt == LUA_TBOOLEAN || vt == LUA_TSTRING || vt == LUA_TTABLE)) {
                out += '[';
                if (kt == LUA_TSTRING) { size_t n; const char* s = lua_tolstring(L, -2, &n); Escape(out, s, n); } else { char b[40]; std::snprintf(b, sizeof b, "%.9g", lua_tonumber(L, -2)); out += b; }
                out += "]=";
                Serialize(L, -1, out, depth + 1);
                out += ',';
            }
            lua_pop(L, 1);
        }
        out += '}';
        break;
    }
    default: out += "nil";
    }
}
std::string FilePath(lua_State* L) { return g.platform->ResolveUserFile(CfgStr(L, "file", "savestates.lua"), true); }
void Write(lua_State* L) {
    if (!g.platform) return;
    GetData(L);
    std::string text = "saveStatesData = ";
    Serialize(L, -1, text, 0);
    text += "\n";
    lua_pop(L, 1);
    std::vector<uint8_t> bytes(text.begin(), text.end());
    if (!WriteFileSafely(FilePath(L), bytes)) std::fprintf(stderr, "saveStates: cannot write %s\n", FilePath(L).c_str());
    g.lastWrite = g.clock;
}
void LoadFile(lua_State* L) {          // once: reads the file into the registry
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.saveStates");
    const bool have = lua_istable(L, -1);
    lua_pop(L, 1);
    if (have || !g.platform) return;
    GetData(L); lua_pop(L, 1);
    std::string text; bool enc = false;
    if (!ReadSaveFile(FilePath(L), text, enc)) return;
    if (luaL_loadbuffer(L, text.data(), text.size(), "=savestates") != 0) { lua_pop(L, 1); return; }
    lua_newtable(L); lua_pushvalue(L, -1);
    lua_setfenv(L, -3);
    const int env = lua_gettop(L);
    lua_pushvalue(L, -2);
    if (lua_pcall(L, 0, 0, 0) == 0) {
        lua_getfield(L, env, "saveStatesData");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "levels");
            if (lua_istable(L, -1)) { lua_pop(L, 1); lua_setfield(L, LUA_REGISTRYINDEX, "ab.saveStates"); }
            else lua_pop(L, 2);
        } else lua_pop(L, 1);
    } else lua_pop(L, 1);
    lua_settop(L, env - 2);
}
bool HasState(lua_State* L, const std::string& id) {
    GetData(L); lua_getfield(L, -1, "levels"); lua_getfield(L, -1, id.c_str());
    const bool r = lua_istable(L, -1);
    lua_pop(L, 3);
    return r;
}
void Forget(lua_State* L, const std::string& id) {
    LoadFile(L);
    if (HasState(L, id)) {
        GetData(L); lua_getfield(L, -1, "levels"); lua_pushnil(L); lua_setfield(L, -2, id.c_str()); lua_pop(L, 2);
        Write(L);
    }
    if (id == g.curId) g.suspended = true;        // nothing is saved again until the next level load (a restart is on its way)
}

// ---- state of the game
bool IsRunning(lua_State* L) {
    GetG(L, "currentGameMode"); GetG(L, "updateGame");
    bool r = !lua_isnil(L, -1) && lua_rawequal(L, -2, -1);
    lua_pop(L, 2);
    if (!r) return false;
    GetG(L, "objects");
    if (lua_istable(L, -1)) { lua_getfield(L, -1, "world"); r = lua_istable(L, -1); lua_pop(L, 1); } else r = false;
    lua_pop(L, 1);
    return r;
}
// the level is set up: the script variable named in saveStates.readyVariable (the slingshot position) is there and not zero
bool IsReady(lua_State* L) {
    const std::string var = CfgStr(L, "readyVariable", "levelStartPosition");
    if (var.empty()) return true;
    GetG(L, var.c_str());
    bool r = true;
    if (lua_istable(L, -1)) { lua_getfield(L, -1, "x"); r = !lua_isnumber(L, -1) || lua_tonumber(L, -1) != 0; lua_pop(L, 1); }
    else if (lua_isnil(L, -1)) r = true;
    else if (lua_isboolean(L, -1)) r = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return r;
}
bool Truthy(lua_State* L, const std::string& name) { GetG(L, name.c_str()); bool r = lua_toboolean(L, -1) != 0; lua_pop(L, 1); return r; }
bool Positive(lua_State* L, const std::string& name) { GetG(L, name.c_str()); bool r = lua_isnumber(L, -1) && lua_tonumber(L, -1) > 0; lua_pop(L, 1); return r; }

// a copy of the table at idx with its plain values (numbers, strings, flags) and, down to `depth`, its tables
void PlainCopy(lua_State* L, int idx, int depth) {
    idx = AbsIdx(L, idx);
    lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        const int kt = lua_type(L, -2), vt = lua_type(L, -1);
        bool keep = kt == LUA_TSTRING || kt == LUA_TNUMBER;
        if (keep && vt == LUA_TNUMBER) { double d = lua_tonumber(L, -1); keep = d == d && d > -1e30 && d < 1e30; }
        else if (keep && vt != LUA_TBOOLEAN && vt != LUA_TSTRING && !(vt == LUA_TTABLE && depth > 0)) keep = false;
        if (keep) {
            lua_pushvalue(L, -2);
            if (vt == LUA_TTABLE) PlainCopy(L, -2, depth - 1); else lua_pushvalue(L, -2);
            lua_settable(L, -5);
        }
        lua_pop(L, 1);
    }
}
// copies the values of the table at src into the table at dst (tables are merged)
void Merge(lua_State* L, int dst, int src, bool skipPlace) {
    dst = AbsIdx(L, dst); src = AbsIdx(L, src);
    lua_pushnil(L);
    while (lua_next(L, src)) {
        if (skipPlace && lua_type(L, -2) == LUA_TSTRING) {
            const char* k = lua_tostring(L, -2);
            if (!std::strcmp(k, "x") || !std::strcmp(k, "y") || !std::strcmp(k, "angle") || !std::strcmp(k, "name") || !std::strcmp(k, "definition")) { lua_pop(L, 1); continue; }
        }
        lua_pushvalue(L, -2);
        lua_gettable(L, dst);
        if (lua_istable(L, -1) && lua_istable(L, -2)) { Merge(L, -1, -2, false); lua_pop(L, 1); }
        else { lua_pop(L, 1); lua_pushvalue(L, -2); lua_pushvalue(L, -2); lua_settable(L, dst); }
        lua_pop(L, 1);
    }
}
// env function call with a name and the given numbers; returns the number of results left on the stack
int CallEnv(lua_State* L, const char* fn, const std::string& name, std::initializer_list<double> nums, int nret) {
    GetG(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    lua_pushstring(L, name.c_str());
    for (double d : nums) lua_pushnumber(L, d);
    if (lua_pcall(L, 1 + (int)nums.size(), nret, 0) != 0) { lua_pop(L, 1); return 0; }
    return nret;
}
double Num(lua_State* L, int idx, double def = 0) { return lua_isnumber(L, idx) ? lua_tonumber(L, idx) : def; }

// ---- capture
bool HasWorld(lua_State* L) {
    GetG(L, "objects");
    bool r = false;
    if (lua_istable(L, -1)) { lua_getfield(L, -1, "world"); r = lua_istable(L, -1); lua_pop(L, 1); }
    lua_pop(L, 1);
    return r;
}

// `leaving`: the game mode has just left updateGame, the objects are still there
bool g_explain = false;     // 3.7.1: a save that was asked for says on the console why it kept nothing
bool Capture(lua_State* L, bool leaving = false) {
    if (!CfgBool(L, "enabled", true) || g.suspended || !g.ready || !(leaving ? HasWorld(L) : IsRunning(L)) || g.curId.empty()) {
        if (g_explain) std::fprintf(stderr, "save state: nothing kept (%s%s%s)\n", g.suspended ? "this level is in skipLevels or saving is off; " : "", g.ready ? "" : "the level was not set up yet; ", "the level is not running");
        return false;
    }
    for (const std::string& n : CfgList(L, "skipWhen")) if (Truthy(L, n)) { if (g_explain) std::fprintf(stderr, "save state: nothing kept, %s is true (the level is finished)\n", n.c_str()); Forget(L, g.curId); return false; }
    for (const std::string& n : CfgList(L, "skipWhenPositive")) if (Positive(L, n)) { if (g_explain) std::fprintf(stderr, "save state: nothing kept, %s is above 0 (the level is failed)\n", n.c_str()); Forget(L, g.curId); return false; }
    LoadFile(L);
    const int top = lua_gettop(L);
    lua_newtable(L);                              // the state
    const int st = lua_gettop(L);
    lua_newtable(L);                              // objects
    GetG(L, "objects"); lua_getfield(L, -1, "world"); lua_remove(L, -2);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
            lua_getfield(L, -1, "x");
            const bool placed = lua_isnumber(L, -1);
            lua_pop(L, 1);
            if (placed) {
                const std::string name = lua_tostring(L, -2);
                lua_getfield(L, -1, "mass"); const bool moves = Num(L, -1) > 0; lua_pop(L, 1);
                lua_newtable(L);                  // o
                PlainCopy(L, -2, 0); lua_setfield(L, -2, "f");
                if (moves) {
                    double vx = 0, vy = 0, w = 0;
                    if (CallEnv(L, "getVelocity", name, {}, 3)) { vx = Num(L, -2); vy = Num(L, -1); lua_pop(L, 3); }
                    if (CallEnv(L, "getAngularVelocity", name, {}, 1)) { w = Num(L, -1); lua_pop(L, 1); }
                    lua_pushboolean(L, 1); lua_setfield(L, -2, "moves");
                    // 3.7.1: the body type (the game keeps the birds that wait as static bodies and makes the next one dynamic): without it a restored bird stays where it is
                    GetG(L, "getBodyType");
                    if (lua_isfunction(L, -1)) {
                        lua_pushstring(L, name.c_str());
                        if (lua_pcall(L, 1, 1, 0) == 0 && lua_isstring(L, -1)) lua_setfield(L, -2, "bt"); else lua_pop(L, 1);
                    } else lua_pop(L, 1);
                    lua_pushnumber(L, vx); lua_setfield(L, -2, "vx");
                    lua_pushnumber(L, vy); lua_setfield(L, -2, "vy");
                    lua_pushnumber(L, w); lua_setfield(L, -2, "w");
                    lua_pushboolean(L, vx == 0 && vy == 0 && w == 0); lua_setfield(L, -2, "rest");
                }
                lua_setfield(L, st + 1, name.c_str());
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);                                // the world table
    lua_setfield(L, st, "objects");
    lua_newtable(L);                              // globals
    for (const std::string& n : CfgList(L, "globals")) {
        GetG(L, n.c_str());
        const int t = lua_type(L, -1);
        if (t == LUA_TNUMBER || t == LUA_TSTRING || t == LUA_TBOOLEAN) lua_setfield(L, -2, n.c_str()); else lua_pop(L, 1);
    }
    lua_setfield(L, st, "globals");
    lua_newtable(L);                              // tables
    for (const std::string& n : CfgList(L, "tables")) {
        GetG(L, n.c_str());
        if (lua_istable(L, -1)) { PlainCopy(L, -1, 3); lua_setfield(L, -3, n.c_str()); }
        lua_pop(L, 1);
    }
    lua_setfield(L, st, "tables");
    lua_newtable(L);                              // objects the variables point to
    for (const std::string& n : CfgList(L, "objectGlobals")) {
        GetG(L, n.c_str());
        if (lua_istable(L, -1)) { lua_getfield(L, -1, "name"); if (lua_isstring(L, -1)) lua_setfield(L, -3, n.c_str()); else lua_pop(L, 1); }
        lua_pop(L, 1);
    }
    lua_setfield(L, st, "refs");
    {   // 3.7.1: the trail that is drawn (kept by the engine, not by a script table)
        std::vector<float> trail;
        g.platform->GetTrajectory(trail);
        if (trail.size() > 1) {
            lua_newtable(L);
            for (size_t i = 0; i < trail.size(); i++) { lua_pushnumber(L, trail[i]); lua_rawseti(L, -2, (int)i + 1); }
            lua_setfield(L, st, "trail");
        }
    }
    GetG(L, "cameraFunction");
    if (lua_isfunction(L, -1)) {
        for (const std::string& n : CfgList(L, "cameras")) {
            GetG(L, n.c_str());
            if (lua_isfunction(L, -1) && lua_rawequal(L, -1, -2)) { lua_pushstring(L, n.c_str()); lua_setfield(L, st, "camera"); }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    GetData(L); lua_getfield(L, -1, "levels");
    lua_pushvalue(L, st); lua_setfield(L, -2, g.curId.c_str());
    lua_settop(L, top);
    return true;
}

// ---- put back
void CallEnvStr(lua_State* L, const char* fn, const std::string& a, const std::string& b) {
    GetG(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushstring(L, a.c_str()); lua_pushstring(L, b.c_str());
    if (lua_pcall(L, 2, 0, 0) != 0) lua_pop(L, 1);
}
void CallEnvSleep(lua_State* L, const std::string& name) {
    GetG(L, "setSleeping");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushstring(L, name.c_str()); lua_pushboolean(L, 1);
    if (lua_pcall(L, 2, 0, 0) != 0) lua_pop(L, 1);
}
// pushes registry data.levels[id]
void PushState(lua_State* L, const std::string& id) { GetData(L); lua_getfield(L, -1, "levels"); lua_getfield(L, -1, id.c_str()); lua_remove(L, -2); lua_remove(L, -2); }
// the string keys of the table at idx
std::vector<std::string> Keys(lua_State* L, int idx) {
    idx = AbsIdx(L, idx);
    std::vector<std::string> r;
    lua_pushnil(L);
    while (lua_next(L, idx)) { if (lua_type(L, -2) == LUA_TSTRING) r.push_back(lua_tostring(L, -2)); lua_pop(L, 1); }
    return r;
}

bool Apply(lua_State* L) {
    LoadFile(L);
    if (!CfgBool(L, "enabled", true) || !IsRunning(L) || g.curId.empty() || !HasState(L, g.curId)) return false;
    const int top = lua_gettop(L);
    PushState(L, g.curId);
    const int st = lua_gettop(L);
    lua_getfield(L, st, "objects");
    const int saved = lua_gettop(L);
    GetG(L, "objects"); lua_getfield(L, -1, "world"); lua_remove(L, -2);
    const int world = lua_gettop(L);
    const std::vector<std::string> objTables = CfgList(L, "objectTables");
    // what was destroyed since the level began
    for (const std::string& name : Keys(L, world)) {
        lua_getfield(L, saved, name.c_str());
        const bool kept = lua_istable(L, -1);
        lua_pop(L, 1);
        if (kept) continue;
        CallEnv(L, "removeObject", name, {}, 0);
        lua_pushnil(L); lua_setfield(L, world, name.c_str());
        for (const std::string& t : objTables) { GetG(L, t.c_str()); if (lua_istable(L, -1)) { lua_pushnil(L); lua_setfield(L, -2, name.c_str()); } lua_pop(L, 1); }
    }
    // the objects that are left
    for (const std::string& name : Keys(L, saved)) {
        lua_getfield(L, world, name.c_str());
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            // an object the level did not start with (the pieces of a Blue Bird that split): made again from its definition, at the place it was saved
            lua_getfield(L, saved, name.c_str()); lua_getfield(L, -1, "f");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "definition");
                const std::string def = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
                lua_pop(L, 1);
                lua_getfield(L, -1, "x"); lua_getfield(L, -2, "y"); lua_getfield(L, -3, "angle");
                const double x = Num(L, -3), y = Num(L, -2), a = Num(L, -1);
                lua_pop(L, 3);
                if (!def.empty()) {
                    PushEnv(L); lua_getfield(L, -1, "createObject"); lua_getfield(L, -2, "blockTable");
                    if (lua_isfunction(L, -2) && lua_istable(L, -1)) {
                        lua_pushstring(L, def.c_str()); lua_pushstring(L, name.c_str()); lua_pushnumber(L, x); lua_pushnumber(L, y);
                        lua_newtable(L); lua_pushnumber(L, a); lua_setfield(L, -2, "angle");
                        if (lua_pcall(L, 6, 0, 0) != 0) lua_pop(L, 1);
                        lua_pop(L, 1);
                    } else lua_pop(L, 3);
                    lua_getfield(L, world, name.c_str());
                    if (lua_istable(L, -1)) {
                        lua_getfield(L, saved, name.c_str()); lua_getfield(L, -1, "f"); lua_getfield(L, -1, "controllable");
                        const bool bird = lua_toboolean(L, -1) != 0;
                        lua_pop(L, 3);
                        if (bird) { GetG(L, "birds"); if (lua_istable(L, -1)) { lua_pushvalue(L, -2); lua_setfield(L, -2, name.c_str()); } lua_pop(L, 1); }
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 2);
            lua_getfield(L, world, name.c_str());
            if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }
        }
        const int obj = lua_gettop(L);
        lua_getfield(L, saved, name.c_str());
        const int o = lua_gettop(L);
        lua_getfield(L, o, "f");
        const int f = lua_gettop(L);
        if (lua_istable(L, f)) {
            lua_getfield(L, f, "sprite");
            const bool hasSprite = lua_isstring(L, -1);
            const std::string want = hasSprite ? lua_tostring(L, -1) : std::string();
            lua_pop(L, 1);
            lua_getfield(L, obj, "sprite");
            const bool differs = hasSprite && (!lua_isstring(L, -1) || want != lua_tostring(L, -1));
            lua_pop(L, 1);
            Merge(L, obj, f, true);
            if (differs) CallEnvStr(L, "setSprite", name, want);
            lua_getfield(L, o, "moves");
            const bool moves = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            if (moves) {
                lua_getfield(L, f, "x"); lua_getfield(L, f, "y"); lua_getfield(L, f, "angle");
                const double x = Num(L, -3), y = Num(L, -2), a = Num(L, -1);
                lua_pop(L, 3);
                lua_getfield(L, o, "vx"); lua_getfield(L, o, "vy"); lua_getfield(L, o, "w"); lua_getfield(L, o, "rest");
                const double vx = Num(L, -4), vy = Num(L, -3), w = Num(L, -2);
                const bool rest = lua_toboolean(L, -1) != 0;
                lua_pop(L, 4);
                lua_getfield(L, o, "bt");
                if (lua_isstring(L, -1)) { GetG(L, "setBodyType"); if (lua_isfunction(L, -1)) { lua_pushstring(L, name.c_str()); lua_pushvalue(L, -3); if (lua_pcall(L, 2, 0, 0) != 0) lua_pop(L, 1); } else lua_pop(L, 1); }
                lua_pop(L, 1);
                CallEnv(L, "setPosition", name, {x, y}, 0);
                CallEnv(L, "setRotation", name, {a}, 0);
                CallEnv(L, "setVelocity", name, {vx, vy}, 0);
                CallEnv(L, "setAngularVelocity", name, {w}, 0);
                lua_getfield(L, f, "controllable");
                const bool bird = lua_toboolean(L, -1) != 0;      // a bird is never put to sleep: it is launched later, and a body that sleeps stays where it is
                lua_pop(L, 1);
                if (rest && !bird) CallEnvSleep(L, name);
            }
        }
        lua_settop(L, obj - 1);
    }
    // 3.7.1: a bird that was made again (or one the level did not start with) has none of the timers the games' animation code counts down for every entry of `birds`
    // (jumpTimer, animTimer: the games give a piece of a split bird 9999); the missing ones are given that value so the update does not stop on an empty field
    GetG(L, "birds");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_istable(L, -1)) {
                for (const char* f : {"jumpTimer", "animTimer"}) {
                    lua_getfield(L, -1, f);
                    if (lua_isnil(L, -1)) { lua_pushnumber(L, 9999); lua_setfield(L, -3, f); }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    // the script variables, tables, objects and the camera
    lua_getfield(L, st, "globals");
    if (lua_istable(L, -1)) {
        PushEnv(L);
        for (const std::string& n : Keys(L, -2)) { lua_getfield(L, -2, n.c_str()); lua_setfield(L, -2, n.c_str()); }
        // 3.7.1: a listed variable that was empty (nil) when the level was saved is not in the saved table; the freshly loaded level may have set it (the bird that is
        // sent to the slingshot first), so it is emptied again
        for (const std::string& n : CfgList(L, "globals")) {
            lua_getfield(L, -2, n.c_str());
            const bool saved = !lua_isnil(L, -1);
            lua_pop(L, 1);
            if (!saved) { lua_pushnil(L); lua_setfield(L, -2, n.c_str()); }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_getfield(L, st, "tables");
    if (lua_istable(L, -1)) {
        for (const std::string& n : Keys(L, -1)) {
            GetG(L, n.c_str());
            if (lua_istable(L, -1)) { lua_getfield(L, -2, n.c_str()); Merge(L, -2, -1, false); lua_pop(L, 1); }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, st, "refs");
    if (lua_istable(L, -1)) {
        PushEnv(L);
        for (const std::string& n : Keys(L, -2)) {
            lua_getfield(L, -2, n.c_str());
            const std::string name = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
            lua_pop(L, 1);
            lua_getfield(L, world, name.c_str());
            lua_setfield(L, -2, n.c_str());
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_getfield(L, st, "trail");
    if (lua_istable(L, -1)) {
        std::vector<float> trail;
        const int n = (int)lua_objlen(L, -1);
        for (int i = 1; i <= n; i++) { lua_rawgeti(L, -1, i); trail.push_back((float)Num(L, -1)); lua_pop(L, 1); }
        g.platform->SetTrajectory(trail);
    }
    lua_pop(L, 1);
    lua_getfield(L, st, "camera");
    if (lua_isstring(L, -1)) { GetG(L, lua_tostring(L, -1)); PushEnv(L); lua_pushvalue(L, -2); lua_setfield(L, -2, "cameraFunction"); lua_pop(L, 2); }
    lua_pop(L, 1);
    GetG(L, "worldScale");                       // the camera's zoom is a variable the engine has to be told about
    if (lua_isnumber(L, -1)) { const double ws = lua_tonumber(L, -1); lua_pop(L, 1); GetG(L, "setWorldScale"); if (lua_isfunction(L, -1)) { lua_pushnumber(L, ws); if (lua_pcall(L, 1, 0, 0) != 0) lua_pop(L, 1); } else lua_pop(L, 1); }
    else lua_pop(L, 1);
    lua_settop(L, top);
    return true;
}

// 3.6: a copy of the running level's state is kept in memory (twice a second, never written): the pause page changes the camera and other variables, and a save made while it is
// open would keep those. A save asked for while the level is not running writes the last copy.
void Snapshot(lua_State* L) {
    if (g.curId.empty()) return;
    const int top = lua_gettop(L);
    GetData(L); lua_getfield(L, -1, "levels");
    lua_getfield(L, -1, g.curId.c_str());          // what was there
    const int old = lua_gettop(L);
    if (Capture(L, false)) {
        lua_getfield(L, old - 1, g.curId.c_str());
        lua_newtable(L);
        lua_pushstring(L, g.curId.c_str()); lua_setfield(L, -2, "id");
        lua_pushvalue(L, -2); lua_setfield(L, -2, "state");
        lua_setfield(L, LUA_REGISTRYINDEX, "ab.snapshot");
        lua_pop(L, 1);
        lua_pushvalue(L, old); lua_setfield(L, old - 1, g.curId.c_str());      // put back what was there
    }
    lua_settop(L, top);
}

bool SaveNow(lua_State* L, bool leaving = false) {
    if (leaving && !IsRunning(L)) {
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.snapshot");
        bool done = false;
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "id");
            const bool same = lua_isstring(L, -1) && g.curId == lua_tostring(L, -1);
            lua_pop(L, 1);
            if (same && !g.suspended && CfgBool(L, "enabled", true)) {
                lua_getfield(L, -1, "state");
                GetData(L); lua_getfield(L, -1, "levels");
                lua_pushvalue(L, -3); lua_setfield(L, -2, g.curId.c_str());
                lua_pop(L, 3);
                done = true;
            }
        }
        lua_pop(L, 1);
        if (done) { Write(L); return true; }
    }
    if (!Capture(L, leaving)) return false;
    Write(L);
    return true;
}

// ---- Lua: saveStates.save(), apply(), forget([id]), clearAll(), has([id])
int s_save(lua_State* L) { g_explain = true; const bool r = SaveNow(L, true); g_explain = false; if (r) std::fprintf(stderr, "save state of %s written\n", g.curId.c_str()); lua_pushboolean(L, r); return 1; }      // asked for by a script: saved whenever the level's objects are there (the pause page too)
int s_apply(lua_State* L) { lua_pushboolean(L, Apply(L)); return 1; }
int s_forget(lua_State* L) { Forget(L, lua_isstring(L, 1) ? lua_tostring(L, 1) : g.curId); return 0; }
int s_has(lua_State* L) { LoadFile(L); lua_pushboolean(L, HasState(L, lua_isstring(L, 1) ? lua_tostring(L, 1) : g.curId)); return 1; }
int s_clearAll(lua_State* L) {
    LoadFile(L);
    lua_pushnil(L); lua_setfield(L, LUA_REGISTRYINDEX, "ab.saveStates");
    GetData(L); lua_pop(L, 1);
    Write(L);
    g.suspended = true;
    return 0;
}

}  // namespace

// Makes the global table `saveStates` (the settings, with their defaults, and the functions).
void RegisterSaveStates(lua_State* L, Platform* platform) {
    g = State();
    g.platform = platform;
    const char* defaults =
        "saveStates = {\n"
        "  enabled = true,            -- false: nothing is saved or put back\n"
        "  skipLevels = {},           -- parts of level paths: these levels are never saved or put back\n"
        "  file = 'savestates.lua',   -- in the save folder\n"
        "  autosave = 0,              -- seconds (real time) between writes while a level runs; 0: no automatic writes\n"
        "  saveOnLeave = false,       -- true: the state is also written when the level is left or the game is closed; false: only when saveStates.save() is called (the leave pop-up does)\n"
        "  snapshotEvery = 0.05,      -- seconds (real time) between the copies kept in memory for a save made while the game is paused; the pause page changes variables, so the copy is what the level was just before\n"
        "  restartWindow = 2,         -- a level that is loaded again within this many seconds after it was left is a restart: its state is cleared\n"
        "  readyVariable = 'levelStartPosition',  -- the level is set up when this variable is there (a table: with x not 0)\n"
        "  globals = { 'currentBirdIndex', 'currentBirdName', 'birdToSlingshotBirdName', 'birdSpecialtyAvailable', 'birdFired', 'birdReady', 'fillInNextBird',\n"
        "    'birdSelected', 'showTapIcon', 'birdsShot', 'birdsCounter', 'score', 'nextBirdTimer', 'allowTrajectoryClearing',\n"
        "    'levelCompleteTimer' },\n"
        "                             -- (3.6) the camera (the screen table, the zoom, the camera function) is not kept: the game's own camera starts again, and a camera put back alone left the view beside the slingshot\n"
        "  tables = { 'scoreTable', 'birdTrajectory', 'otherBirds' },\n"
        "  objectGlobals = { 'flyingBird', 'selectedBird', 'cameraTargetObject' },\n"
        "  objectTables = { 'birds', 'otherBirds', 'levelGoals' },\n"
        "  cameras = {},                -- camera functions to keep: none by default (names of global functions)\n"
        "  skipWhen = { 'levelCompleted' },           -- a level that is finished (variable true) or failed (variable above 0) is not kept\n"
        "  skipWhenPositive = { 'levelFailedTimer' },\n"
        "}\n";
    PushEnv(L);
    const int env = lua_gettop(L);
    if (luaL_loadstring(L, defaults) == 0) {
        lua_pushvalue(L, env); lua_setfenv(L, -2);
        if (lua_pcall(L, 0, 0, 0) != 0) lua_pop(L, 1);
    } else lua_pop(L, 1);
    lua_getfield(L, env, "saveStates");
    if (lua_istable(L, -1)) {
        static const luaL_Reg fns[] = { {"save", s_save}, {"apply", s_apply}, {"forget", s_forget}, {"has", s_has}, {"clearAll", s_clearAll}, {nullptr, nullptr} };
        for (const luaL_Reg* r = fns; r->name; r++) { lua_pushcfunction(L, r->func); lua_setfield(L, -2, r->name); }
    }
    lua_settop(L, env - 1);
}

// loadLevel(file) has loaded a level: its state is put back in the first frame it runs (when there is one)
void SaveStatesLevelLoaded(lua_State* L, const std::string& file) {
    LoadFile(L);
    const bool quick = file == g.lastId && g.clock - g.leftAt < CfgNum(L, "restartWindow", 2);
    g.curId = file;
    g.suspended = false;
    g.ready = false;
    g.pending = CfgBool(L, "enabled", true);
    if (quick && HasState(L, file)) { Forget(L, file); g.suspended = false; }     // a restart: the level begins again from the start
    // 3.6: levels named in saveStates.skipLevels (parts of the level path, as written in the list) are neither saved nor put back
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "saveStates");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "skipLevels");
        if (lua_istable(L, -1)) {
            const int n = (int)lua_objlen(L, -1);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                if (lua_isstring(L, -1) && file.find(lua_tostring(L, -1)) != std::string::npos) { g.suspended = true; g.pending = false; Forget(L, file); }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 2);
}

// every frame, after the scripts' update
void SaveStatesFrame(lua_State* L, double realDt) {
    if (!g.platform) return;
    g.clock += realDt;
    if (!CfgBool(L, "enabled", true)) return;
    const bool running = IsRunning(L);
    if (g.pending && running && IsReady(L)) {
        g.pending = false;
        const bool done = Apply(L);
        g.ready = true;
        if (done) std::fprintf(stderr, "save state of %s put back\n", g.curId.c_str());
    } else if (g.ready && running) {
        if (g.clock - g.lastSnap >= CfgNum(L, "snapshotEvery", 0.05)) {
            g.lastSnap = g.clock;
            const auto t0 = std::chrono::steady_clock::now();
            Snapshot(L);
            if (std::getenv("AB_SNAPTIME")) { static double sum = 0; static int n = 0; sum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); if (++n % 200 == 0) std::fprintf(stderr, "snapshot: %.3f ms on average\n", sum / n); }
        }
        const double every = CfgNum(L, "autosave", 5);
        if (every > 0 && g.clock - g.lastWrite >= every) { if (!SaveNow(L)) g.lastWrite = g.clock; }
    }
    if (g.prevRunning && !running && g.ready) {      // the level was left (the menu, the level select, another level)
        if (CfgBool(L, "saveOnLeave", false)) SaveNow(L, true);
        g.leftAt = g.clock;
        g.lastId = g.curId;
    }
    g.prevRunning = running;
}

// the window is closed or the game ends
void SaveStatesShutdown(lua_State* L) {
    if (!g.platform || !g.ready) return;
    if (IsRunning(L) && CfgBool(L, "saveOnLeave", false)) SaveNow(L);
}

}  // namespace ab
