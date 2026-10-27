#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iterator>
#include <vector>
#include "lua/aes.h"
#include <cstring>
#include "lua/lua_api.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lualib.h"

}

namespace ab {
bool LoadTableFile(lua_State* L, const char* name, const std::string& path, std::string* error, bool append);
namespace {

Platform* P(lua_State* L) { return static_cast<Platform*>(lua_touserdata(L, lua_upvalueindex(1))); }
float N(lua_State* L, int i) { return (float)luaL_checknumber(L, i); }
float NOpt(lua_State* L, int i, float def) { return (float)luaL_optnumber(L, i, def); }
std::string S(lua_State* L, int i) { return lua_isnoneornil(L, i) ? std::string() : std::string(luaL_checkstring(L, i)); }   // a nil name is an empty one
bool B(lua_State* L, int i) { return lua_toboolean(L, i) != 0; }
// The script environment table (registry["ab.env"]); engine functions and values live there.
void PushEnv(lua_State* L) { lua_getfield(L, LUA_REGISTRYINDEX, "ab.env"); }

// ---- table field readers (for createJoint / setJointParameters)
bool Has(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    bool has = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return has;
}
float FieldN(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    return v;
}
bool FieldB(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    bool v = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return v;
}
std::string FieldS(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return v;
}

// ---- window / system
int l_requestExit(lua_State* L) { P(L)->RequestExit(); return 0; }
int l_print(lua_State* L) {
    std::string out;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) out += "\t";
        if (lua_isstring(L, i)) out += lua_tostring(L, i);
        else if (lua_isboolean(L, i)) out += lua_toboolean(L, i) ? "true" : "false";
        else out += luaL_typename(L, i);
    }
    P(L)->Print(out);
    return 0;
}
int l_printGlobals(lua_State* L) {
    PushEnv(L);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) P(L)->Print(std::string(lua_tostring(L, -2)) + " (" + luaL_typename(L, -1) + ")");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 0;
}
int l_setBGColor(lua_State* L) { P(L)->SetBGColor(N(L, 1), N(L, 2), N(L, 3)); return 0; }
int l_captureMouse(lua_State* L) { P(L)->CaptureMouse(B(L, 1)); return 0; }
int l_isMouseCaptured(lua_State* L) { lua_pushboolean(L, P(L)->IsMouseCaptured()); return 1; }
int l_setFullScreenMode(lua_State* L) { P(L)->SetFullScreen(B(L, 1)); return 0; }
int l_isInFullScreenMode(lua_State* L) { lua_pushboolean(L, P(L)->IsFullScreen()); return 1; }
int l_setResolution(lua_State* L) { P(L)->SetResolution(N(L, 1), N(L, 2)); return 0; }
int l_setIsMultitouchMouseWheelSimulationEnabled(lua_State* L) { P(L)->SetMouseWheelSimulation(B(L, 1)); return 0; }
int l_doesMouseClickSetsTouchCount(lua_State* L) { lua_pushboolean(L, P(L)->MouseClickSetsTouchCount()); return 1; }
int l_setEditing(lua_State* L) { P(L)->SetEditing(B(L, 1)); return 0; }
int l_setGameOn(lua_State* L) { P(L)->SetGameOn(B(L, 1)); return 0; }

// createThemeSprite(name, sprite, x, y, scaleX, scaleY, angle, speedX, layer) and friends
bool RioMode(lua_State* L);
int l_createThemeSprite(lua_State* L) {
    if (RioMode(L)) {      // Rio: ..., angle, layer, angleVel, flipped
        P(L)->CreateThemeSpriteRio(S(L, 1), S(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), (int)N(L, 8), N(L, 9), B(L, 10));
        return 0;
    }
    P(L)->CreateThemeSprite(S(L, 1), S(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), N(L, 8), (int)N(L, 9));
    return 0;
}
int l_modifyThemeSprite(lua_State* L) {
    P(L)->ModifyThemeSprite(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), (int)N(L, 7));
    return 0;
}
int l_removeThemeSprite(lua_State* L) { P(L)->RemoveThemeSprite(S(L, 1), (int)N(L, 2)); return 0; }
int l_updateThemeSprite(lua_State* L) { P(L)->UpdateThemeSprites(N(L, 1)); return 0; }
int l_setThemeSprite(lua_State* L) { P(L)->SetThemeSprite(S(L, 1), S(L, 2), (int)N(L, 3)); return 0; }
int l_rotateThemeSprites(lua_State* L) { P(L)->RotateThemeSprites(N(L, 1)); return 0; }
int l_updateBackgroundAndForegroundScrollingNative(lua_State* L) { P(L)->ScrollThemeLayers(N(L, 1), B(L, 2)); return 0; }

// Seasons: loadLevelFile(path, date) asked a server for a level of that date and getLoadStatus() reported
// {status = ...} until the answer came. Levels are local here, so the answer is ready at once (status 1).
int l_getLoadStatus(lua_State* L) {
    lua_newtable(L);
    lua_pushnumber(L, 1);
    lua_setfield(L, -2, "status");
    return 1;
}

int l_throwException(lua_State* L) { P(L)->Print("script exception: " + S(L, 1)); return 0; }

int l_loadLuaFileToObject(lua_State* L) {
    // loadLuaFileToObject(path, object, name): runs the file in a table of its own that falls back to
    // `object` for names it does not define, then stores that table as object[name]
    std::string path = S(L, 1);
    bool toObject = lua_istable(L, 2) && lua_isstring(L, 3);
    std::vector<uint8_t> bytes;
    int rc = 1;
    if (ab::ReadLuaFile(path, bytes)) rc = luaL_loadbuffer(L, (const char*)bytes.data(), bytes.size(), ("@" + path).c_str());
    else lua_pushstring(L, ("cannot open " + path).c_str());
    if (rc == 0) {
        if (toObject) {
            if (*lua_tostring(L, 3) == 0) {
                lua_pushvalue(L, 2);                    // 2.0.0, name "": the file runs in the object itself
            } else {
                lua_newtable(L);                        // the file's table
                lua_newtable(L);                        // its metatable { __index = object }
                lua_pushvalue(L, 2);
                lua_setfield(L, -2, "__index");
                lua_setmetatable(L, -2);
                // an object without a metatable of its own falls back to the globals, so the menu files
                // (Angry Birds Rio) also find `gamelua`, `res` and the engine functions by bare name
                if (!lua_getmetatable(L, 2)) {
                    PushEnv(L);
                    if (!lua_rawequal(L, -1, 2)) {
                        lua_newtable(L);
                        lua_pushvalue(L, -2);
                        lua_setfield(L, -2, "__index");
                        lua_setmetatable(L, 2);
                    }
                    lua_pop(L, 1);
                } else lua_pop(L, 1);
                lua_pushvalue(L, -1);
                lua_setfield(L, 2, lua_tostring(L, 3)); // object[name] = the file's table
            }
            lua_setfenv(L, -2);
        } else if (lua_isstring(L, 2) && *lua_tostring(L, 2)) {
            // loadLuaFile(path, name): the file runs in a table of its own (names it does not define come from
            // the globals) and that table becomes the global `name`, e.g. cutscenes
            PushEnv(L);                                 // the globals
            lua_newtable(L);
            lua_newtable(L);
            lua_pushvalue(L, -3);
            lua_setfield(L, -2, "__index");
            lua_setmetatable(L, -2);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, lua_tostring(L, 2));    // globals[name] = the file's table
            lua_remove(L, -2);
            lua_setfenv(L, -2);
        } else {
            PushEnv(L);
            lua_setfenv(L, -2);
        }
        rc = lua_pcall(L, 0, 0, 0);
        if (rc == 0) {
            // debug build: MainMenu.init / MainMenu.layout run with releaseBuild false (shows the 2.0.0 EDITOR button)
            lua_getfield(L, LUA_REGISTRYINDEX, "ab.view");
            PushEnv(L);
            lua_getfield(L, -1, "MainMenu");
            if (lua_istable(L, -1) && lua_istable(L, -3)) {
                for (const char* name : {"init", "layout"}) {
                    lua_pushstring(L, name);
                    lua_rawget(L, -2);
                    if (lua_isfunction(L, -1)) { lua_pushvalue(L, -4); lua_setfenv(L, -2); }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 3);
        }
    }
    if (rc != 0) {
        P(L)->Print("loadLuaFileToObject failed: " + path + ": " + std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
    }
    return 0;
}

// ---- files
// blocks.lua / particles.lua are re-read on every loadLevel so the tables start fresh
void ReloadTable(lua_State* L, const char* name, const char* file) {
    PushEnv(L);
    lua_getfield(L, -1, "scriptPath");
    std::string path = std::string(lua_isstring(L, -1) ? lua_tostring(L, -1) : "scripts") + file;
    lua_pop(L, 2);
    std::string err;
    if (!ab::LoadTableFile(L, name, path, &err, false)) P(L)->Print("loadLevel: " + err);
}

// Angry Birds Rio has no blocks.lua: the exe's loadLevel reads materials, damage factors, pigs, level goals, score
// objects, birds and the blocks_*.lua files, the last ones all into blockTable, then themes and groups.
bool HasSingleBlocksFile(lua_State* L) {
    PushEnv(L);
    lua_getfield(L, -1, "scriptPath");
    std::string path = std::string(lua_isstring(L, -1) ? lua_tostring(L, -1) : "scripts") + "/blocks.lua";
    lua_pop(L, 2);
    std::vector<uint8_t> bytes;
    return ab::ReadLuaFile(path, bytes) && !bytes.empty();
}

void ReloadAllTables(lua_State* L) {
    if (HasSingleBlocksFile(L)) {
        ReloadTable(L, "blockTable", "/blocks.lua");
        ReloadTable(L, "particleTable", "/particles.lua");
        return;
    }
    ab::LoadRioTables(L);
}

// loadLevel(file) (0x40d2c0): the level file runs in a fresh, empty environment table; that table
// becomes the global `loadedObjects`. A level that cannot be read leaves loadedObjects nil, which
// the scripts handle ("level was not found").
int l_loadLevel(lua_State* L) {
    std::string file = S(L, 1);
    P(L)->BeginLevelLoad();
    ReloadAllTables(L);

    bool ok = false;
    lua_newtable(L);                                    // the level's environment
    if (file.size() < 4 || file.compare(file.size() - 4, 4, ".lua") != 0) file += ".lua";   // the scripts leave it off
    std::string path = P(L)->ResolveDataFile(file);
    // The exe decrypts the file (AES-256-CBC) before running it; a plain-text level is used as it is.
    std::vector<uint8_t> bytes;
    int loaded = 1;
    std::string loadError = "No such file or directory";
    ab::ReadLuaFile(path, bytes);
    if (!bytes.empty()) {
        loaded = luaL_loadbuffer(L, (const char*)bytes.data(), bytes.size(), path.c_str());
        if (loaded != 0) { loadError = lua_tostring(L, -1); lua_pop(L, 1); }
    }
    if (loaded == 0) {
        lua_pushvalue(L, -2);
        lua_setfenv(L, -2);
        if (lua_pcall(L, 0, 0, 0) == 0) ok = true;
        else { P(L)->Print("loadLevel: " + std::string(lua_tostring(L, -1))); lua_pop(L, 1); }
    } else {
        P(L)->Print("loadLevel: cannot read " + path + ": " + loadError);
    }
    if (!ok) { lua_pop(L, 1); lua_pushnil(L); }
    else {
        // The scripts empty loadedObjects once they have taken what they use. The editor's saveLevel writes back what the file
        // held that no script keeps (trainCarts, weather...), so the top-level entries stay in the registry.
        lua_newtable(L);
        lua_pushnil(L);
        while (lua_next(L, -3)) {
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_settable(L, -5);
            lua_pop(L, 1);
        }
        lua_setfield(L, LUA_REGISTRYINDEX, "ab.lastLevel");
    }
    PushEnv(L);
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, "loadedObjects");
    lua_pop(L, 2);
    P(L)->EndLevelLoad();
    lua_pushboolean(L, ok);
    return 1;
}
// loadLevelEditor(file) (Seasons, 0x40db30): the editor asks for the plain-text copy of a level before it runs loadLevelInternal.
// ReadLuaFile already takes plain and encrypted levels alike, so all that is left is to report a level that is missing.
int l_loadLevelEditor(lua_State* L) {
    std::string file = S(L, 1);
    if (file.size() < 4 || file.compare(file.size() - 4, 4, ".lua") != 0) file += ".lua";
    std::vector<uint8_t> bytes;
    ab::ReadLuaFile(P(L)->ResolveDataFile(file), bytes);
    if (bytes.empty()) P(L)->Print("loadLevelEditor: cannot read " + P(L)->ResolveDataFile(file));
    lua_pushboolean(L, !bytes.empty());
    return 1;
}
int l_saveLevel(lua_State* L) { P(L)->SaveLevel(S(L, 1)); return 0; }
int l_createDirectory(lua_State* L) { P(L)->CreateDirectory(S(L, 1)); return 0; }
int l_checkDirectory(lua_State* L) { lua_pushboolean(L, P(L)->CheckDirectory(S(L, 1))); return 1; }
int l_checkForLuaFile(lua_State* L) { lua_pushboolean(L, P(L)->CheckForLuaFile(S(L, 1))); return 1; }

// Writes `value` (on top of the stack) as Lua source.
void Serialize(lua_State* L, std::string& out, int depth) {
    switch (lua_type(L, -1)) {
        case LUA_TNUMBER: { char b[64]; std::snprintf(b, sizeof b, "%.9g", (double)lua_tonumber(L, -1)); out += b; break; }
        case LUA_TBOOLEAN: out += lua_toboolean(L, -1) ? "true" : "false"; break;
        case LUA_TSTRING: {
            size_t len; const char* s = lua_tolstring(L, -1, &len);
            out += '"';
            for (size_t i = 0; i < len; i++) {
                unsigned char c = (unsigned char)s[i];
                if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c < 32) { char b[8]; std::snprintf(b, sizeof b, "\\%03d", c); out += b; }
                else out += (char)c;
            }
            out += '"';
            break;
        }
        case LUA_TTABLE: {
            if (depth > 32) { out += "nil"; break; }
            out += "{";
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                int kt = lua_type(L, -2), vt = lua_type(L, -1);
                if ((kt == LUA_TSTRING || kt == LUA_TNUMBER) &&
                    (vt == LUA_TNUMBER || vt == LUA_TBOOLEAN || vt == LUA_TSTRING || vt == LUA_TTABLE)) {
                    out += "[";
                    lua_pushvalue(L, -2);          // copy the key so lua_next keeps working
                    Serialize(L, out, depth + 1);
                    lua_pop(L, 1);
                    out += "]=";
                    Serialize(L, out, depth + 1);
                    out += ",";
                }
                lua_pop(L, 1);
            }
            out += "}";
            break;
        }
        default: out += "nil"; break;
    }
}

int l_saveLuaFile(lua_State* L) {
    // saveLuaFile(fileName, tableName, appData): writes `tableName = <global tableName>` to fileName
    std::string file = S(L, 1), name = S(L, 2);
    bool appData = B(L, 3);
    PushEnv(L);
    lua_getfield(L, -1, name.c_str());
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
    std::string path = P(L)->ResolveUserFile(file, appData);
    // an existing original file stays in its format: one `key = value` line per entry, encrypted if it was
    // (some originals are not encrypted: their text starts with an entry, not with `tableName = {`)
    std::string old;
    bool encrypted = false;
    const char* saveKey = ab::kSaveKey;
    ab::ReadSaveFile(path, old, encrypted, &saveKey);   // 1.x and Seasons saves have their own keys
    size_t start = old.find_first_not_of(" \t\r\n");
    bool original = encrypted || (start != std::string::npos && old.compare(start, name.size() + 3, name + " = ") != 0);
    std::string text;
    if (original) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                text += lua_tostring(L, -2);
                text += " = ";
                Serialize(L, text, 0);
                text += "\n";
            }
            lua_pop(L, 1);
        }
    } else {
        text = name + " = ";
        Serialize(L, text, 0);
        text += "\n";
    }
    lua_pop(L, 1);
    std::vector<uint8_t> bytes(text.begin(), text.end());
    if (original) {
        std::string backup = path + ".original-backup";        // the first overwrite keeps the file the game wrote
        if (FILE* b = std::fopen(backup.c_str(), "rb")) std::fclose(b);
        else if (FILE* in = std::fopen(path.c_str(), "rb")) {
            if (FILE* b = std::fopen(backup.c_str(), "wb")) {
                int c;
                while ((c = std::fgetc(in)) != EOF) std::fputc(c, b);
                std::fclose(b);
            }
            std::fclose(in);
        }
        if (encrypted) {
            std::vector<uint8_t> enc;
            ab::AesEncryptCbc((const uint8_t*)saveKey, bytes, enc);
            bytes = enc;
        }
    }
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    } else {
        P(L)->Print("saveLuaFile: cannot write " + path);
    }
    return 0;
}

// ---- physics
// objects.world / objects.joints (the exe keeps these tables as `Game+0x1a4`'s fields)
bool PushObjectsTable(lua_State* L, const char* which) {
    PushEnv(L);
    lua_getfield(L, -1, "objects");
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
    lua_getfield(L, -1, which);
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
    return true;
}
void SetNum(lua_State* L, const char* k, float v) { lua_pushnumber(L, v); lua_setfield(L, -2, k); }
void SetBool(lua_State* L, const char* k, bool v) { lua_pushboolean(L, v); lua_setfield(L, -2, k); }
void SetStr(lua_State* L, const char* k, const std::string& v) { lua_pushlstring(L, v.data(), v.size()); lua_setfield(L, -2, k); }

// objects.world[name] = { name, sprite, type, x, y, width, height | radius, angle, density, friction,
//                         restitution, mass, xVel, yVel, z_order }   (key order of the exe's helper)
void WriteWorldEntry(lua_State* L, const BodyDef& d, const char* type, float mass) {
    if (!PushObjectsTable(L, "world")) return;
    lua_newtable(L);
    SetStr(L, "name", d.name);
    SetStr(L, "sprite", d.sprite);
    SetStr(L, "type", type);
    SetNum(L, "x", d.x);
    SetNum(L, "y", d.y);
    if (std::string(type) == "circle") {
        SetNum(L, "radius", d.w);
    } else {
        SetNum(L, "width", d.w);
        SetNum(L, "height", d.h);
    }
    SetNum(L, "angle", 0);
    SetNum(L, "density", d.density);
    SetNum(L, "friction", d.friction);
    SetNum(L, "restitution", d.restitution);
    SetNum(L, "mass", mass);
    SetNum(L, "xVel", 0);
    SetNum(L, "yVel", 0);
    SetNum(L, "z_order", d.zOrder);
    // Angry Birds Rio's entries also carry the animation counters (the scripts count them down)
    SetNum(L, "animTimer", 0);
    SetNum(L, "animFrame", 0);
    SetNum(L, "animThresholdTimer", 0);
    lua_setfield(L, -2, d.name.c_str());
    lua_pop(L, 1);
}

BodyDef ReadBody(lua_State* L, bool circle) {
    BodyDef d;
    d.name = S(L, 1);
    d.sprite = S(L, 2);
    d.x = N(L, 3);
    d.y = N(L, 4);
    int i = 5;
    d.w = N(L, i++);
    if (!circle) d.h = N(L, i++);
    d.density = N(L, i++);
    d.friction = N(L, i++);
    d.restitution = N(L, i++);
    // Angry Birds Rio's circles take the collision flag as well (a boolean followed by the controllable boolean)
    if (!circle || (lua_type(L, i) == LUA_TBOOLEAN && lua_type(L, i + 1) == LUA_TBOOLEAN)) d.collision = B(L, i++);
    d.controllable = B(L, i++);
    d.zOrder = N(L, i++);
    return d;
}
int l_createBox(lua_State* L) {
    BodyDef d = ReadBody(L, false);
    WriteWorldEntry(L, d, "box", P(L)->CreateBox(d));
    return 0;
}
int l_createCircle(lua_State* L) {
    BodyDef d = ReadBody(L, true);
    WriteWorldEntry(L, d, "circle", P(L)->CreateCircle(d));
    return 0;
}
int l_createPolygon(lua_State* L) {
    BodyDef d = ReadBody(L, false);
    WriteWorldEntry(L, d, "polygon", P(L)->CreatePolygon(d, P(L)->Vertices()));
    return 0;
}
int l_clearVertices(lua_State* L) { P(L)->ClearVertices(); return 0; }
int l_addVertex(lua_State* L) { P(L)->AddVertex(N(L, 1), N(L, 2)); return 0; }

// The exe reads optional numbers only if the field is a number or a string, and optional flags only
// if the field is a real boolean (so `collideConnected = 0` counts as absent).
bool HasNum(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    bool has = lua_type(L, -1) == LUA_TNUMBER || lua_type(L, -1) == LUA_TSTRING;
    lua_pop(L, 1);
    return has;
}
bool BoolOr(lua_State* L, int t, const char* k, bool def) {
    lua_getfield(L, t, k);
    bool v = lua_type(L, -1) == LUA_TBOOLEAN ? lua_toboolean(L, -1) != 0 : def;
    lua_pop(L, 1);
    return v;
}
float NumOr(lua_State* L, int t, const char* k, float def) { return HasNum(L, t, k) ? FieldN(L, t, k) : def; }

// createJoint{ name, end1, end2, type, coordType, x1, y1, x2, y2, ... } (0x40ecd0)
int l_createJoint(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    JointDef d;
    d.name = FieldS(L, 1, "name");
    d.end1 = FieldS(L, 1, "end1");
    d.end2 = FieldS(L, 1, "end2");
    d.type = (int)FieldN(L, 1, "type");
    d.coordType = HasNum(L, 1, "coordType") ? (int)std::floor(FieldN(L, 1, "coordType") + 0.5f) : 0;
    d.x1 = FieldN(L, 1, "x1"); d.y1 = FieldN(L, 1, "y1");
    d.x2 = FieldN(L, 1, "x2"); d.y2 = FieldN(L, 1, "y2");
    d.collideConnected = BoolOr(L, 1, "collideConnected", false);
    switch (d.type) {
    case 1:
        d.frequency = NumOr(L, 1, "frequency", 4.0f);
        d.dampingRatio = NumOr(L, 1, "dampingRatio", 0.5f);
        break;
    case 3:
        d.motor = BoolOr(L, 1, "motor", false);
        d.motorSpeed = NumOr(L, 1, "motorSpeed", 0.0f);
        d.maxTorque = NumOr(L, 1, "maxTorque", 10000.0f);
        d.limit = BoolOr(L, 1, "limit", false);
        d.lowerLimit = NumOr(L, 1, "lowerLimit", 0.0f);
        d.upperLimit = NumOr(L, 1, "upperLimit", 3.1415927f);
        d.backAndForth = BoolOr(L, 1, "backAndForth", false);
        break;
    case 4:
        d.worldAxisX = NumOr(L, 1, "worldAxisX", 0.0f);
        d.worldAxisY = NumOr(L, 1, "worldAxisY", 0.0f);
        d.limit = BoolOr(L, 1, "limit", true);
        d.lowerLimit = NumOr(L, 1, "lowerLimit", 0.0f);
        d.upperLimit = NumOr(L, 1, "upperLimit", 5.0f);
        d.motor = BoolOr(L, 1, "motor", true);
        d.motorSpeed = NumOr(L, 1, "motorSpeed", 0.0f);
        d.maxTorque = NumOr(L, 1, "maxTorque", 10000.0f);
        d.backAndForth = BoolOr(L, 1, "backAndForth", true);
        break;
    case 5:
        d.destroyTimer = NumOr(L, 1, "destroyTimer", 1.0f);
        break;
    default:
        break;
    }
    if (!P(L)->CreateJoint(d)) return 0;

    // objects.joints[name] = a new table with the effective values
    if (!PushObjectsTable(L, "joints")) return 0;
    lua_newtable(L);
    SetStr(L, "name", d.name);
    SetStr(L, "end1", d.end1);
    SetStr(L, "end2", d.end2);
    SetNum(L, "type", (float)d.type);
    SetNum(L, "coordType", (float)d.coordType);
    SetNum(L, "x1", d.x1); SetNum(L, "y1", d.y1);
    SetNum(L, "x2", d.x2); SetNum(L, "y2", d.y2);
    switch (d.type) {
    case 1:
        SetNum(L, "frequency", d.frequency);
        SetNum(L, "dampingRatio", d.dampingRatio);
        SetBool(L, "collideConnected", d.collideConnected);
        break;
    case 2:
        SetBool(L, "collideConnected", d.collideConnected);
        break;
    case 3:
        SetBool(L, "motor", d.motor);
        SetNum(L, "motorSpeed", d.motorSpeed);
        SetNum(L, "maxTorque", d.maxTorque);
        SetBool(L, "limit", d.limit);
        SetNum(L, "lowerLimit", d.lowerLimit);
        SetNum(L, "upperLimit", d.upperLimit);
        SetBool(L, "backAndForth", d.backAndForth);
        SetBool(L, "collideConnected", d.collideConnected);
        break;
    case 4:
        SetBool(L, "limit", d.limit);
        SetNum(L, "lowerLimit", d.lowerLimit);
        SetNum(L, "upperLimit", d.upperLimit);
        SetBool(L, "motor", d.motor);
        SetNum(L, "motorSpeed", d.motorSpeed);
        SetNum(L, "maxTorque", d.maxTorque);
        SetBool(L, "collideConnected", d.collideConnected);
        SetNum(L, "worldAxisX", d.worldAxisX);
        SetNum(L, "worldAxisY", d.worldAxisY);
        SetBool(L, "backAndForth", d.backAndForth);
        break;
    case 5:
        SetNum(L, "destroyTimer", d.destroyTimer);
        break;
    default:
        break;
    }
    // Rio: a joint the level marks `recreate` (rebuilt after it breaks) keeps the flag, so the editor writes it back
    lua_getfield(L, 1, "recreate");
    if (lua_isboolean(L, -1)) { lua_pushvalue(L, -1); lua_setfield(L, -3, "recreate"); }
    lua_pop(L, 1);
    lua_setfield(L, -2, d.name.c_str());
    lua_pop(L, 1);
    return 0;
}
// createJoints(list) (Angry Birds Rio): creates every joint of a level's joint table; a joint without a name takes its key
int l_createJoints(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "name");
            bool named = lua_isstring(L, -1);
            lua_pop(L, 1);
            if (!named && lua_type(L, -2) == LUA_TSTRING) {
                lua_pushvalue(L, -2);
                lua_setfield(L, -2, "name");
            }
            lua_pushvalue(L, lua_upvalueindex(1));        // the platform
            lua_pushcclosure(L, l_createJoint, 1);
            lua_pushvalue(L, -2);
            lua_call(L, 1, 0);
        }
        lua_pop(L, 1);
    }
    return 0;
}
int l_setJointParameters(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    JointParams p;
    p.name = FieldS(L, 1, "name");
    if ((p.hasMotor = Has(L, 1, "motor"))) p.motor = FieldB(L, 1, "motor");
    if ((p.hasMotorSpeed = Has(L, 1, "motorSpeed"))) p.motorSpeed = FieldN(L, 1, "motorSpeed");
    if ((p.hasMaxTorque = Has(L, 1, "maxTorque"))) p.maxTorque = FieldN(L, 1, "maxTorque");
    if ((p.hasLimit = Has(L, 1, "limit"))) p.limit = FieldB(L, 1, "limit");
    if ((p.hasLowerLimit = Has(L, 1, "lowerLimit"))) p.lowerLimit = FieldN(L, 1, "lowerLimit");
    if ((p.hasUpperLimit = Has(L, 1, "upperLimit"))) p.upperLimit = FieldN(L, 1, "upperLimit");
    if ((p.hasFrequency = Has(L, 1, "frequency"))) p.frequency = FieldN(L, 1, "frequency");
    if ((p.hasDampingRatio = Has(L, 1, "dampingRatio"))) p.dampingRatio = FieldN(L, 1, "dampingRatio");
    P(L)->SetJointParameters(p);
    if (PushObjectsTable(L, "joints")) {            // the joint's table follows
        lua_getfield(L, -1, p.name.c_str());
        if (lua_istable(L, -1)) {
            if (p.hasMotor) SetBool(L, "motor", p.motor);
            if (p.hasMotorSpeed) SetNum(L, "motorSpeed", p.motorSpeed);
            if (p.hasMaxTorque) SetNum(L, "maxTorque", p.maxTorque);
            if (p.hasLimit) SetBool(L, "limit", p.limit);
            if (p.hasLowerLimit) SetNum(L, "lowerLimit", p.lowerLimit);
            if (p.hasUpperLimit) SetNum(L, "upperLimit", p.upperLimit);
            if (p.hasFrequency) SetNum(L, "frequency", p.frequency);
            if (p.hasDampingRatio) SetNum(L, "dampingRatio", p.dampingRatio);
        }
        lua_pop(L, 2);
    }
    return 0;
}
int l_checkJointLimits(lua_State* L) { P(L)->CheckJointLimits(S(L, 1)); return 0; }
int l_destroyJoint(lua_State* L) {
    std::string n = S(L, 1);
    P(L)->DestroyJoint(n);
    if (PushObjectsTable(L, "joints")) {              // joints[name] = nil (0x410e60)
        lua_pushnil(L);
        lua_setfield(L, -2, n.c_str());
        lua_pop(L, 1);
    }
    return 0;
}
bool RioMode(lua_State* L);
int l_removeObject(lua_State* L) {
    std::vector<std::string> fired = P(L)->RemoveObject(S(L, 1));
    if (!fired.empty() && PushObjectsTable(L, "joints")) {
        for (size_t i = 0; i < fired.size(); i++) {
            lua_pushnil(L);
            lua_setfield(L, -2, fired[i].c_str());
        }
        lua_pop(L, 1);
    }
    if (RioMode(L) && PushObjectsTable(L, "joints")) {      // Rio: a joint whose object is gone does not stay in objects.joints (removeBird would follow it to a missing object)
        std::string gone = S(L, 1);
        std::vector<std::string> stale;
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
                bool match = false;
                for (const char* end : {"end1", "end2"}) {
                    lua_getfield(L, -1, end);
                    if (lua_isstring(L, -1) && gone == lua_tostring(L, -1)) match = true;
                    lua_pop(L, 1);
                }
                if (match) stale.push_back(lua_tostring(L, -2));
            }
            lua_pop(L, 1);
        }
        for (const std::string& k : stale) { lua_pushnil(L); lua_setfield(L, -2, k.c_str()); }
        lua_pop(L, 1);
    }
    return 0;
}
int l_applyImpulse(lua_State* L) { P(L)->ApplyImpulse(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5)); return 0; }
int l_applyForce(lua_State* L) { P(L)->ApplyForce(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5)); return 0; }
// the exe also writes x / y (angle for setRotation) into objects.world[name]
void SetWorldField(lua_State* L, const std::string& name, const char* k1, float v1, const char* k2 = nullptr, float v2 = 0) {
    if (!PushObjectsTable(L, "world")) return;
    lua_getfield(L, -1, name.c_str());
    if (lua_istable(L, -1)) {
        SetNum(L, k1, v1);
        if (k2) SetNum(L, k2, v2);
    }
    lua_pop(L, 2);
}
int l_setPosition(lua_State* L) {
    std::string n = S(L, 1);
    float x = N(L, 2), y = N(L, 3);
    if (P(L)->SetPosition(n, x, y)) SetWorldField(L, n, "x", x, "y", y);
    return 0;
}
int l_setRotation(lua_State* L) {
    std::string n = S(L, 1);
    float a = N(L, 2);
    if (P(L)->SetRotation(n, a, a)) SetWorldField(L, n, "angle", a);
    return 0;
}
int l_setVelocity(lua_State* L) { P(L)->SetVelocity(S(L, 1), N(L, 2), N(L, 3)); return 0; }
int l_setAngularVelocity(lua_State* L) { P(L)->SetAngularVelocity(S(L, 1), N(L, 2)); return 0; }
int l_setSleeping(lua_State* L) { P(L)->SetSleeping(S(L, 1), B(L, 2)); return 0; }
int l_getAngle(lua_State* L) { lua_pushnumber(L, P(L)->GetAngle(S(L, 1))); return 1; }
int l_getWorldPoint(lua_State* L) {
    float wx, wy;
    P(L)->GetWorldPoint(S(L, 1), N(L, 2), N(L, 3), wx, wy);
    lua_pushnumber(L, wx);
    lua_pushnumber(L, wy);
    return 2;
}
// Angry Birds Rio
int l_getVelocity(lua_State* L) { lua_pushnumber(L, P(L)->GetSpeed(S(L, 1))); return 1; }
int l_hasMovingObjects(lua_State* L) { lua_pushboolean(L, P(L)->HasMovingObjects(0.5f)); return 1; }
int l_hasMovingObjectsZeroTolerance(lua_State* L) { lua_pushboolean(L, P(L)->HasMovingObjects(0.001f)); return 1; }
int l_getLocalPoint(lua_State* L) {
    float lx, ly;
    P(L)->GetLocalPoint(S(L, 1), N(L, 2), N(L, 3), lx, ly);
    lua_pushnumber(L, lx);
    lua_pushnumber(L, ly);
    return 2;
}
int l_setCollisionEnabled(lua_State* L) { P(L)->SetCollisionEnabled(S(L, 1), B(L, 2)); return 0; }
// setColliderType(name, type) / getColliderType(name) -> type (-1: no such object); setColliderRule(typeA, typeB, collides) / getColliderRule(typeA, typeB)
int l_setColliderType(lua_State* L) { P(L)->SetColliderType(S(L, 1), (int)N(L, 2)); return 0; }
int l_getColliderType(lua_State* L) { lua_pushnumber(L, P(L)->ColliderType(S(L, 1))); return 1; }
// setObjectAlpha(name, alpha) / getObjectAlpha(name): transparency of one object when drawGameNative draws it (0 invisible .. 1 solid; -1: no such object)
int l_setObjectAlpha(lua_State* L) { P(L)->SetObjectAlpha(S(L, 1), N(L, 2)); return 0; }
int l_getObjectAlpha(lua_State* L) { lua_pushnumber(L, P(L)->ObjectAlpha(S(L, 1))); return 1; }
int l_setColliderRule(lua_State* L) { P(L)->SetColliderRule((int)N(L, 1), (int)N(L, 2), B(L, 3)); return 0; }
int l_getColliderRule(lua_State* L) { lua_pushboolean(L, P(L)->ColliderRule((int)N(L, 1), (int)N(L, 2))); return 1; }
// setPolygonRadius(r) / getPolygonRadius(): the skin of the boxes and polygons that are created afterwards (Angry Birds Rio's Box2D 2.2)
int l_setPolygonRadius(lua_State* L) { P(L)->SetPolygonRadius(N(L, 1)); return 0; }
int l_getPolygonRadius(lua_State* L) { lua_pushnumber(L, P(L)->PolygonRadius()); return 1; }
int l_removeJointsFromObject(lua_State* L) { P(L)->RemoveJointsFromObject(S(L, 1)); return 0; }
// getIntersectingObjects{ x, y, left, right, down, up } -> { name, name, ... } (the scripts read the names as the values)
bool RioMode(lua_State* L);
int l_getIntersectingObjects(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    // Seasons passes the sizes of the four sides; Rio passes signed offsets (left and down are negative)
    float sideSign = RioMode(L) ? -1.0f : 1.0f;
    std::vector<std::string> names = P(L)->IntersectingObjects(FieldN(L, 1, "x"), FieldN(L, 1, "y"), sideSign * FieldN(L, 1, "left"),
                                                               FieldN(L, 1, "right"), sideSign * FieldN(L, 1, "down"), FieldN(L, 1, "up"));
    lua_newtable(L);
    bool haveWorld = PushObjectsTable(L, "world");     // only objects the scripts still know (they index objects.world[name])
    int n = 0;
    for (const std::string& name : names) {
        if (haveWorld) {
            lua_getfield(L, -1, name.c_str());
            bool known = lua_istable(L, -1);
            lua_pop(L, 1);
            if (!known) continue;
        }
        lua_pushlstring(L, name.data(), name.size());
        lua_rawseti(L, haveWorld ? -3 : -2, ++n);
    }
    if (haveWorld) lua_pop(L, 1);
    return 1;
}
int l_setObjectParameter(lua_State* L) {
    int id = (int)N(L, 2);
    float value = N(L, 3);
    PushEnv(L);
    lua_getfield(L, -1, "rioMode");
    bool rio = lua_toboolean(L, -1) != 0;
    lua_pop(L, 2);
    // Angry Birds Rio: 6 and 7 are bounce multipliers, not Seasons' "use the scale", and a scale of its own applies as is
    if (rio && (id == 6 || id == 7)) { P(L)->SetObjectParameter(S(L, 1), id + 100, value); return 0; }
    P(L)->SetObjectParameter(S(L, 1), id, value);
    if (rio && id == 5) P(L)->SetObjectParameter(S(L, 1), 6, value != 1.0f ? 1.0f : 0.0f);
    return 0;
}
int l_setPhysicsSimulationScale(lua_State* L) { P(L)->SetPhysicsSimulationScale(N(L, 1)); return 0; }
int l_setPhysicsEnabled(lua_State* L) { P(L)->SetPhysicsEnabled(B(L, 1)); return 0; }
int l_isPhysicsEnabled(lua_State* L) { lua_pushboolean(L, P(L)->IsPhysicsEnabled()); return 1; }
int l_setLevelLimits(lua_State* L) { P(L)->SetLevelLimits(N(L, 1), N(L, 2), N(L, 3), N(L, 4)); return 0; }

// ---- camera
int l_setTopLeft(lua_State* L) { P(L)->SetTopLeft(N(L, 1), N(L, 2)); return 0; }
int l_setWorldScale(lua_State* L) { P(L)->SetWorldScale(N(L, 1)); return 0; }
int l_setMaxWorldScale(lua_State* L) { P(L)->SetMaxWorldScale(N(L, 1)); return 0; }
int l_setMaxTranslation(lua_State* L) { P(L)->SetMaxTranslation(N(L, 1)); return 0; }

// ---- drawing
int l_setRenderState(lua_State* L) {
    float a[8] = {0, 0, 1, 1, 0, 0, 0, 1};
    int argc = lua_gettop(L);
    for (int i = 0; i < argc && i < 8; i++) a[i] = N(L, i + 1);
    P(L)->SetRenderState(argc, a);
    return 0;
}
int l_drawRect(lua_State* L) {
    P(L)->DrawRect(N(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), N(L, 8), B(L, 9));
    return 0;
}
int l_drawLine2D(lua_State* L) {
    P(L)->DrawLine2D(N(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), N(L, 8), N(L, 9));
    return 0;
}
// drawRectLines(left, top, right, bottom, width, r, g, b, a): the four edges of a rectangle
int l_drawRectLines(lua_State* L) {
    float x1 = N(L, 1), y1 = N(L, 2), x2 = N(L, 3), y2 = N(L, 4), w = N(L, 5), r = N(L, 6), g = N(L, 7), b = N(L, 8), a = N(L, 9);
    P(L)->DrawLine2D(x1, y1, x2, y1, w, r, g, b, a);
    P(L)->DrawLine2D(x2, y1, x2, y2, w, r, g, b, a);
    P(L)->DrawLine2D(x2, y2, x1, y2, w, r, g, b, a);
    P(L)->DrawLine2D(x1, y2, x1, y1, w, r, g, b, a);
    return 0;
}
// 2.0.0 asks the device about itself and has an ads / notification layer this engine does not have
int l_osName(lua_State* L) { lua_pushstring(L, "Windows"); return 1; }
int l_osVersion(lua_State* L) { lua_pushstring(L, "6.1"); return 1; }
int l_deviceModel(lua_State* L) { lua_pushstring(L, "PC"); return 1; }
int l_deviceManufacturer(lua_State* L) { lua_pushstring(L, "PC"); return 1; }
int l_currentLocale(lua_State* L) { lua_pushstring(L, "en"); return 1; }

// ---- 2.0.0 additions
// drawBoxNative(components, x, y, width, height, hanchor, vanchor): a 3x3 (or 3x1) box of sprites. The
// corners keep their size, the edges and the middle stretch to fill the rest.
void ParseAnchor(const char* word, int& v, int& h);
int l_drawBoxNative(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    auto comp = [&](const char* key) -> std::string {
        lua_getfield(L, 1, key);
        std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        return v;
    };
    float x = N(L, 2), y = N(L, 3), w = N(L, 4), h = N(L, 5);
    int v = 0, hh = 0;
    if (lua_isstring(L, 6)) ParseAnchor(lua_tostring(L, 6), v, hh);
    if (lua_isstring(L, 7)) ParseAnchor(lua_tostring(L, 7), v, hh);
    float left = hh == 1 ? x - w / 2 : hh == 2 ? x - w : x;
    float top = v == 1 ? y - h / 2 : v == 2 ? y - h : y;
    auto bounds = [&](const std::string& n, float& bw, float& bh) {
        bw = bh = 0;
        if (!n.empty()) P(L)->ResGetSpriteBounds("", n, bw, bh);
    };
    auto draw = [&](const std::string& n, float dx, float dy, float dw, float dh) {
        if (n.empty() || dw <= 0 || dh <= 0) return;
        P(L)->ResDrawSprite(n, dx, dy, 0, 0, true, dw, dh);
    };
    std::string tl = comp("topLeft"), tm = comp("topMiddle"), tr = comp("topRight");
    std::string l = comp("left"), c = comp("center"), r = comp("right");
    std::string bl = comp("bottomLeft"), bm = comp("bottomMiddle"), br = comp("bottomRight");
    float lw, lh, rw, rh, th = 0, bh2 = 0, tw, bw2, unused;
    bounds(l, lw, unused); bounds(r, rw, unused);
    if (tl.empty() && tm.empty() && tr.empty() && bl.empty() && bm.empty() && br.empty()) {
        // three pieces in a row
        float cw = w - lw - rw;
        draw(l, left, top, lw, h);
        draw(c, left + lw, top, cw, h);
        draw(r, left + w - rw, top, rw, h);
        return 0;
    }
    float tlw, tlh, trw, trh, blw, blh, brw, brh;
    bounds(tl, tlw, tlh); bounds(tr, trw, trh); bounds(bl, blw, blh); bounds(br, brw, brh);
    bounds(tm, tw, th); bounds(bm, bw2, bh2);
    float topH = std::max(tlh, std::max(trh, th)), botH = std::max(blh, std::max(brh, bh2));
    float leftW = std::max(tlw, std::max(lw, blw)), rightW = std::max(trw, std::max(rw, brw));
    float midW = w - leftW - rightW, midH = h - topH - botH;
    draw(tl, left, top, leftW, topH);
    draw(tm, left + leftW, top, midW, topH);
    draw(tr, left + w - rightW, top, rightW, topH);
    draw(l, left, top + topH, leftW, midH);
    draw(c, left + leftW, top + topH, midW, midH);
    draw(r, left + w - rightW, top + topH, rightW, midH);
    draw(bl, left, top + h - botH, leftW, botH);
    draw(bm, left + leftW, top + h - botH, midW, botH);
    draw(br, left + w - rightW, top + h - botH, rightW, botH);
    return 0;
}
// getCurrentTime() -> {year, month, day, hour, min, sec}; getTimeDifference(a, b) -> {days, hours, minutes, seconds}
// where a is the later time
int l_getCurrentTime(lua_State* L) {
    time_t t = time(nullptr);
    struct tm* m = localtime(&t);
    lua_newtable(L);
    auto set = [&](const char* k, int v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); };
    set("year", m->tm_year + 1900); set("month", m->tm_mon + 1); set("day", m->tm_mday);
    set("hour", m->tm_hour); set("minutes", m->tm_min); set("seconds", m->tm_sec);
    return 1;
}
int l_getTimeDifference(lua_State* L) {
    auto toTime = [&](int idx) -> double {
        struct tm m = {};
        auto get = [&](const char* k, int def) { lua_getfield(L, idx, k); int v = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : def; lua_pop(L, 1); return v; };
        if (!lua_istable(L, idx)) return 0;
        m.tm_year = get("year", 1970) - 1900; m.tm_mon = get("month", 1) - 1; m.tm_mday = get("day", 1);
        m.tm_hour = get("hour", 0); m.tm_min = get("minutes", 0); m.tm_sec = get("seconds", 0); m.tm_isdst = -1;
        return (double)mktime(&m);
    };
    // 0x420c30: the absolute difference split into days, hours, minutes and seconds
    long total = (long)std::fabs(toTime(1) - toTime(2));
    lua_newtable(L);
    auto set = [&](const char* k, long v) { lua_pushnumber(L, (double)v); lua_setfield(L, -2, k); };
    set("days", total / 86400); set("hours", total / 3600 % 24);
    set("minutes", total / 60 % 60); set("seconds", total % 60);
    return 1;
}
int l_setAlpha(lua_State* L) { P(L)->SetAlpha(N(L, 1)); return 0; }

// drawUITextNative(text, x, y [, scaleX, scaleY [, angle [, scale]]]) is Text.draw (0x41adf0). A clipped text
// calls draw on each of its lines; any other text sets the font and the render state and draws its string.
int l_drawUITextNative(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    auto num = [&](const char* k, float def) { lua_getfield(L, 1, k); float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def; lua_pop(L, 1); return v; };
    auto flag = [&](const char* k) { lua_getfield(L, 1, k); bool v = lua_toboolean(L, -1) != 0; lua_pop(L, 1); return v; };
    auto str = [&](const char* k, const char* def) { lua_getfield(L, 1, k); std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : def; lua_pop(L, 1); return v; };
    int argc = lua_gettop(L);
    float ox = N(L, 2), oy = N(L, 3);
    float sx = argc >= 5 ? N(L, 4) : 1.0f, sy = argc >= 5 ? N(L, 5) : 1.0f;
    float angle = argc >= 6 ? N(L, 6) : 0.0f;
    float scale = argc >= 7 ? N(L, 7) : 1.0f;
    if (!flag("visible")) return 0;
    sx *= num("scaleX", 1) * scale;
    sy *= num("scaleY", 1) * scale;
    float x = num("x", 0) * scale + ox, y = num("y", 0) * scale + oy;
    if (flag("clipped")) {
        lua_getfield(L, 1, "lines");
        int n = (int)lua_objlen(L, -1);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            lua_getfield(L, -1, "draw");
            lua_pushvalue(L, -2);
            lua_pushnumber(L, x); lua_pushnumber(L, y); lua_pushnumber(L, sx); lua_pushnumber(L, sy); lua_pushnumber(L, angle);
            if (lua_pcall(L, 6, 0, 0) != 0) { lua_pop(L, 1); }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return 0;
    }
    P(L)->ResUseFont(str("font", "FONT_BASIC"));
    float pivotX = 0, pivotY = 0;
    lua_getfield(L, 1, "rotationPivotX"); lua_getfield(L, 1, "rotationPivotY");
    if (lua_isnumber(L, -2) && lua_isnumber(L, -1)) { pivotX = (float)lua_tonumber(L, -2); pivotY = (float)lua_tonumber(L, -1); }
    lua_pop(L, 2);
    float a[8] = {0, 0, sx, sy, angle, pivotX, pivotY, 1};
    lua_getfield(L, 1, "alpha");
    if (lua_isnumber(L, -1) && lua_tonumber(L, -1) < 1.0) a[7] = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    P(L)->SetRenderState(8, a);
    x /= sx; y /= sy;
    if (flag("floorCoordinates")) { x = std::floor(x); y = std::floor(y); }
    P(L)->ResDrawString(str("group", ""), str("text", ""), x, y, str("hanchor", "LEFT"), str("vanchor", "TOP"));
    float reset[8] = {0, 0, 1, 1, 0, 0, 0, 1};
    P(L)->SetRenderState(8, reset);
    return 0;
}
int l_emptyString(lua_State* L) { lua_pushstring(L, ""); return 1; }
int l_getTimeDifferenceInSeconds(lua_State* L) {
    lua_getglobal(L, "getTimeDifference");
    lua_pushvalue(L, 1); lua_pushvalue(L, 2);
    lua_call(L, 2, 1);
    double total = 0;
    const char* keys[4] = {"days", "hours", "minutes", "seconds"};
    const double mult[4] = {86400, 3600, 60, 1};
    for (int i = 0; i < 4; i++) { lua_getfield(L, -1, keys[i]); total += lua_tonumber(L, -1) * mult[i]; lua_pop(L, 1); }
    lua_pushnumber(L, total);
    return 1;
}
int l_drawLevelParticlesNative(lua_State* L) { P(L)->DrawLevelParticles((int)N(L, 1)); return 0; }
int l_noop(lua_State*) { return 0; }   // setAlpha, drawLevelParticlesNative (the game view already draws its particles)
int l_setWorldGravity(lua_State* L) { P(L)->SetWorldGravity(N(L, 1), N(L, 2)); return 0; }
int l_clearParticlesNative(lua_State* L) { P(L)->ClearParticles(); return 0; }
int l_drawGameNative(lua_State* L) { P(L)->DrawGame(); return 0; }
int l_drawBackgroundNative(lua_State* L) { P(L)->DrawBackground(); return 0; }
int l_drawForegroundNative(lua_State* L) { P(L)->DrawForeground(); return 0; }
int l_drawParticlesNative(lua_State* L) { P(L)->DrawParticles(false); return 0; }
int l_setSprite(lua_State* L) { P(L)->SetSprite(S(L, 1), S(L, 2)); return 0; }
int l_setMaterial(lua_State* L) { P(L)->SetMaterial(S(L, 1), S(L, 2)); return 0; }
int l_setTexture(lua_State* L) { P(L)->SetTexture(S(L, 1), S(L, 2)); return 0; }
int l_setTheme(lua_State* L) { P(L)->SetTheme(S(L, 1)); return 0; }
int l_startNewTrajectory(lua_State* L) { P(L)->StartNewTrajectory(); return 0; }
int l_addToTrajectory(lua_State* L) { P(L)->AddToTrajectory(N(L, 1), N(L, 2), N(L, 3)); return 0; }
int l_addPuffToTrajectory(lua_State* L) { P(L)->AddPuffToTrajectory(N(L, 1), N(L, 2), N(L, 3)); return 0; }

int l_clipText(lua_State* L) {
    float widest = 0;
    std::vector<std::string> lines = P(L)->ClipText(S(L, 1), S(L, 2), N(L, 3), widest);
    PushEnv(L);
    lua_getfield(L, -1, "clippedText");
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        PushEnv(L);
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, "clippedText");
        lua_pop(L, 1);
    }
    lua_newtable(L);
    for (size_t i = 0; i < lines.size(); i++) {
        lua_pushlstring(L, lines[i].data(), lines[i].size());
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_setfield(L, -2, "lines");
    lua_pushnumber(L, widest);
    lua_setfield(L, -2, "widestLine");
    lua_pop(L, 1);
    return 0;
}

// ---- platform services that a new engine answers with constants
int l_nothing(lua_State*) { return 0; }
int l_false(lua_State* L) { lua_pushboolean(L, 0); return 1; }
// The exe's device id (0x499860): the MAC address of each network adapter as AA-BB-CC-DD-EE-FF, joined by ';'.
// The registration the game keeps in its save file is tied to this id, so it has to be the same text.
std::string DeviceId() {
    std::string id;
#ifdef _WIN32
    ULONG size = 0;
    GetAdaptersInfo(nullptr, &size);
    if (size == 0) return "ab-device-0";
    std::vector<unsigned char> buf(size);
    IP_ADAPTER_INFO* a = (IP_ADAPTER_INFO*)buf.data();
    if (GetAdaptersInfo(a, &size) != NO_ERROR) return "ab-device-0";
    for (; a; a = a->Next) {
        static const char* hex = "0123456789ABCDEF";
        if (!id.empty() || a != (IP_ADAPTER_INFO*)buf.data()) id += ';';
        for (UINT i = 0; i < a->AddressLength; i++) {
            id += hex[a->Address[i] >> 4];
            id += hex[a->Address[i] & 15];
            if (i + 1 < a->AddressLength) id += '-';
        }
    }
#else
    id = "ab-device-0";
#endif
    return id;
}
int l_getDeviceID(lua_State* L) { lua_pushstring(L, DeviceId().c_str()); return 1; }
int l_areDeviceIDsEqual(lua_State* L) {
    const char* a = lua_tostring(L, 1);
    const char* b = lua_tostring(L, 2);
    lua_pushboolean(L, a && b && std::string(a) == b);
    return 1;
}

// 2.0.0 (0x499ac0): the id the save file kept matches when any one of its adapters is also one of this machine's
int l_verifyDeviceID(lua_State* L) {
    const char* kept = lua_tostring(L, 1);
    bool match = false;
    if (kept && *kept) {
        auto split = [](const std::string& s) { std::vector<std::string> v; size_t p = 0; while (p <= s.size()) { size_t q = s.find(';', p); if (q == std::string::npos) q = s.size(); if (q > p) v.push_back(s.substr(p, q - p)); p = q + 1; } return v; };
        std::vector<std::string> a = split(kept), b = split(DeviceId());
        for (const std::string& x : a) for (const std::string& y : b) if (x == y) match = true;
    }
    lua_pushboolean(L, match);
    return 1;
}

// ---- res.*
int r_setPath(lua_State* L) { P(L)->ResSetPath(S(L, 1)); return 0; }
int r_createSpriteSheet(lua_State* L) { P(L)->ResCreateSpriteSheet(S(L, 1)); return 0; }
int r_releaseSpriteSheet(lua_State* L) { P(L)->ResReleaseSpriteSheet(S(L, 1)); return 0; }
int r_createCompoSpriteSet(lua_State* L) { P(L)->ResCreateCompoSpriteSet(S(L, 1)); return 0; }
int r_releaseCompoSpriteSet(lua_State* L) { P(L)->ResReleaseCompoSpriteSet(S(L, 1)); return 0; }
int r_createBitmapFont(lua_State* L) { P(L)->ResCreateBitmapFont(S(L, 1)); return 0; }
int r_useFont(lua_State* L) { P(L)->ResUseFont(S(L, 1)); return 0; }
int r_createTextGroupSet(lua_State* L) { P(L)->ResCreateTextGroupSet(S(L, 1)); return 0; }
int r_loadLocale(lua_State* L) { P(L)->ResLoadLocale(S(L, 1), S(L, 2)); return 0; }
int r_useLocale(lua_State* L) { P(L)->ResUseLocale(S(L, 1)); return 0; }
int r_getString(lua_State* L) { lua_pushstring(L, P(L)->ResGetString(S(L, 1), S(L, 2)).c_str()); return 1; }
// An anchor word sets the vertical or the horizontal anchor, whichever it names (game::parseAnchorString, 0x43b4b0).
void ParseAnchor(const char* word, int& v, int& h) {
    static const char* vertical[] = {"TOP", "VCENTER", "BOTTOM", "BASELINE", "VPIVOT"};
    static const char* horizontal[] = {"LEFT", "HCENTER", "RIGHT", "HPIVOT"};
    for (int i = 0; i < 5; i++) if (std::strcmp(word, vertical[i]) == 0) v = i;
    for (int i = 0; i < 4; i++) if (std::strcmp(word, horizontal[i]) == 0) h = i;
}
int r_drawSprite(lua_State* L) {
    // drawSprite(sprite, x, y [, vAnchor, hAnchor [, width, height]]) or, with a sheet name in front,
    // drawSprite(sheet, sprite, x, y, ...): a string as the second argument means the sheet form ("" = any sheet)
    if (lua_isnil(L, 1) && lua_type(L, 2) == LUA_TSTRING) { lua_pushstring(L, ""); lua_replace(L, 1); }   // a nil sheet means any sheet
    int o = lua_type(L, 2) == LUA_TSTRING ? 1 : 0;
    int argc = lua_gettop(L) - o;
    int v = 4, h = 3;                       // default: draw so that the sprite's pivot is at (x, y)
    if (argc > 3) ParseAnchor(luaL_checkstring(L, 4 + o), v, h);
    if (argc > 4) ParseAnchor(luaL_checkstring(L, 5 + o), v, h);
    bool sized = argc >= 7;
    P(L)->ResDrawSprite(S(L, 1 + o), N(L, 2 + o), N(L, 3 + o), v, h, sized, sized ? N(L, 6 + o) : 0.0f, sized ? N(L, 7 + o) : 0.0f);
    return 0;
}
// drawCompoSprite is drawSprite (0x4307f0); the name is looked up among the sprites and the composites
int r_drawCompoSprite(lua_State* L) { return r_drawSprite(L); }
int r_drawString(lua_State* L) {
    P(L)->ResDrawString(S(L, 1), S(L, 2), N(L, 3), N(L, 4), S(L, 5), S(L, 6));
    return 0;
}
int r_setClipRect(lua_State* L) { P(L)->ResSetClipRect(N(L, 1), N(L, 2), N(L, 3), N(L, 4)); return 0; }
// getSpriteBounds(sheet, sprite) or getSpriteBounds(sprite): "" as the sheet means any sheet
int r_getSpriteBounds(lua_State* L) {
    if (lua_isnil(L, 1) && lua_gettop(L) >= 2) { lua_pushstring(L, ""); lua_replace(L, 1); }   // a nil sheet means any sheet
    float w, h;
    if (lua_gettop(L) >= 2) P(L)->ResGetSpriteBounds(S(L, 1), S(L, 2), w, h);
    else P(L)->ResGetSpriteBounds("", S(L, 1), w, h);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}
int r_getSpritePivot(lua_State* L) {
    if (lua_isnil(L, 1) && lua_gettop(L) >= 2) { lua_pushstring(L, ""); lua_replace(L, 1); }   // a nil sheet means any sheet
    float x, y;
    if (lua_gettop(L) >= 2) P(L)->ResGetSpritePivot(S(L, 1), S(L, 2), x, y);
    else P(L)->ResGetSpritePivot("", S(L, 1), x, y);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}
int r_getCompoSpriteBounds(lua_State* L) {
    if (lua_isnil(L, 1) && lua_gettop(L) >= 2) { lua_pushstring(L, ""); lua_replace(L, 1); }   // a nil sheet means any sheet
    float x1, y1, x2, y2;
    if (lua_gettop(L) >= 2) P(L)->ResGetCompoSpriteBounds(S(L, 1), S(L, 2), x1, y1, x2, y2);
    else P(L)->ResGetCompoSpriteBounds("", S(L, 1), x1, y1, x2, y2);
    lua_pushnumber(L, x1);
    lua_pushnumber(L, y1);
    lua_pushnumber(L, x2);
    lua_pushnumber(L, y2);
    return 4;
}
int r_getStringWidth(lua_State* L) { lua_pushnumber(L, P(L)->ResGetStringWidth(S(L, 1))); return 1; }
int r_getFontHeight(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontHeight()); return 1; }
int r_getFontTracking(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontTracking()); return 1; }
int r_getFontLeading(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontLeading()); return 1; }
int r_getFontMaxAscending(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontMaxAscending()); return 1; }
int r_getFontMaxDescending(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontMaxDescending()); return 1; }
int r_createAudioOutput(lua_State* L) { P(L)->ResCreateAudioOutput(N(L, 1), N(L, 2), N(L, 3)); return 0; }
int r_startAudioOutput(lua_State* L) { P(L)->ResStartAudioOutput(); return 0; }
int r_stopAudioOutput(lua_State* L) { P(L)->ResStopAudioOutput(); return 0; }
int r_createAudio(lua_State* L) { P(L)->ResCreateAudio(S(L, 1), S(L, 2)); return 0; }
int r_playAudio(lua_State* L) { P(L)->ResPlayAudio(S(L, 1), NOpt(L, 2, 1), B(L, 3), (int)NOpt(L, 4, 0)); return 0; }
int r_stopAudio(lua_State* L) { P(L)->ResStopAudio(S(L, 1)); return 0; }
int r_stopAllAudio(lua_State* L) { P(L)->ResStopAllAudio(); return 0; }
int r_isAudioPlaying(lua_State* L) { lua_pushboolean(L, P(L)->ResIsAudioPlaying(S(L, 1))); return 1; }
// note the order: setTrackVolume(volume, track)
int r_setTrackVolume(lua_State* L) { P(L)->ResSetTrackVolume(N(L, 1), (int)N(L, 2)); return 0; }
int r_getTrackVolume(lua_State* L) { lua_pushnumber(L, P(L)->ResGetTrackVolume((int)N(L, 1))); return 1; }
int r_openURL(lua_State* L) { P(L)->ResOpenURL(S(L, 1)); return 0; }

// ---- particles.*
bool RioMode(lua_State* L) {
    PushEnv(L);
    lua_getfield(L, -1, "rioMode");
    bool rio = lua_toboolean(L, -1) != 0;
    lua_pop(L, 2);
    return rio;
}
int p_addParticles(lua_State* L) {
    // Angry Birds Rio: the background confetti is the one emitter that ignores the limits, and it is drawn in the level's
    // two layers (drawLevelParticlesNative), so it goes to the level list, which the limits do not count
    if (RioMode(L) && lua_toboolean(L, 8) != 0) {
        P(L)->AddLevelParticles(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), true);
        return 0;
    }
    // Angry Birds Rio: a bird or crate landing on the ground puffs one dust particle to the left and one to the right
    // (the script's default amount of 10 would make a cloud)
    float amount = N(L, 2);
    if (RioMode(L)) {
        std::string type = S(L, 1);
        if (type.compare(0, 10, "groundDust") == 0 || type.compare(0, 15, "groundCrateDust") == 0) amount = 1;
    }
    // Seasons adds two booleans: ignore the particle limits, and put the particles in the second list
    P(L)->AddParticles(S(L, 1), amount, N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), lua_toboolean(L, 8) != 0, lua_toboolean(L, 9) != 0);
    return 0;
}
// 0x42cae0: addLevelParticles(type, amount, x, y, w, h, angle, ignoreLimits) -> first list
int p_addLevelParticles(lua_State* L) {
    P(L)->AddLevelParticles(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), lua_toboolean(L, 8) != 0);
    return 0;
}
int p_setSoftLimit(lua_State* L) { P(L)->SetParticleSoftLimit(N(L, 1), N(L, 2)); return 0; }
int p_setHardLimit(lua_State* L) { P(L)->SetParticleHardLimit(N(L, 1)); return 0; }

struct Entry { const char* name; lua_CFunction fn; };

// table == 0 registers into the script environment, otherwise into the table at that stack index.
void Register(lua_State* L, Platform* p, int table, const Entry* entries, size_t count) {
    bool pushedEnv = (table == 0);
    if (pushedEnv) { PushEnv(L); table = lua_gettop(L); }
    for (size_t i = 0; i < count; i++) {
        lua_pushlightuserdata(L, p);
        lua_pushcclosure(L, entries[i].fn, 1);
        lua_setfield(L, table, entries[i].name);
    }
    if (pushedEnv) lua_pop(L, 1);
}

// The Rio level editor files every block under `group` / `index` (blockTable.groups names the ten groups, with their keys).
// The shipped definition files carry neither for most blocks (the list a block was defined in is all that is left), so the editor's
// palette is built from the list: a block belongs to the group named like its list, at its position in it. Only the editor reads
// the two fields.
void FileInEditorPalette(lua_State* L, int key, int entry, int position) {
    static const char* const groups[] = {"staticBlocks", "staticFragile", "glassBlocks", "woodBlocks", "stoneBlocks",
                                         "miscBlocks", "birds", "levelGoals", "decorationBlocks", "scoreObjects"};
    if (lua_type(L, key) != LUA_TSTRING) return;
    std::string list = lua_tostring(L, key);
    lua_getfield(L, entry, "group");
    std::string own = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    auto known = [&](const std::string& name) { for (const char* g : groups) if (name == g) return true; return false; };
    if (own.empty() || (!known(own) && known(list))) {
        lua_pushstring(L, list.c_str());
        lua_setfield(L, entry, "group");
    }
    lua_pushnumber(L, position);
    lua_setfield(L, entry, "index");
}

}  // namespace

void RegisterApi(lua_State* L, Platform* p) {
    static const Entry globals[] = {
        {"requestExit", l_requestExit}, {"print", l_print}, {"printGlobals", l_printGlobals},
        {"setBGColor", l_setBGColor}, {"captureMouse", l_captureMouse}, {"isMouseCaptured", l_isMouseCaptured},
        {"setFullScreenMode", l_setFullScreenMode}, {"isInFullScreenMode", l_isInFullScreenMode},
        {"setResolution", l_setResolution},
        {"setIsMultitouchMouseWheelSimulationEnabled", l_setIsMultitouchMouseWheelSimulationEnabled},
        {"doesMouseClickSetsTouchCount", l_doesMouseClickSetsTouchCount},
        {"setEditing", l_setEditing}, {"setGameOn", l_setGameOn},
        {"loadLuaFileToObject", l_loadLuaFileToObject}, {"loadLuaFile", l_loadLuaFileToObject},   // 2.0.0: the same without an object
        {"loadLevel", l_loadLevel}, {"loadLevelEditor", l_loadLevelEditor}, {"saveLevel", l_saveLevel}, {"saveLuaFile", l_saveLuaFile},
        {"createDirectory", l_createDirectory}, {"checkDirectory", l_checkDirectory}, {"checkForLuaFile", l_checkForLuaFile},
        {"createBox", l_createBox}, {"createCircle", l_createCircle}, {"createPolygon", l_createPolygon},
        {"clearVertices", l_clearVertices}, {"addVertex", l_addVertex},
        {"getVelocity", l_getVelocity}, {"hasMovingObjects", l_hasMovingObjects},
        {"hasMovingObjectsZeroTolerance", l_hasMovingObjectsZeroTolerance}, {"getLocalPoint", l_getLocalPoint},
        {"setCollisionEnabled", l_setCollisionEnabled},
        {"setObjectAlpha", l_setObjectAlpha}, {"getObjectAlpha", l_getObjectAlpha}, {"setColliderType", l_setColliderType}, {"getColliderType", l_getColliderType}, {"setColliderRule", l_setColliderRule}, {"getColliderRule", l_getColliderRule},
        {"setPolygonRadius", l_setPolygonRadius}, {"getPolygonRadius", l_getPolygonRadius}, {"removeJointsFromObject", l_removeJointsFromObject},
        {"getIntersectingObjects", l_getIntersectingObjects},
        {"setParticleSystemLimits", l_nothing}, {"clearParticleCache", l_nothing}, {"enableInGameParticlesNative", l_nothing},
        {"updateBackgroundAndForegroundScrollingNative", l_updateBackgroundAndForegroundScrollingNative}, {"rotateThemeSprites", l_rotateThemeSprites},
        {"setThemeSprite", l_setThemeSprite}, {"drawMenuParticlesNative", l_nothing}, {"updateMenuParticlesNative", l_nothing}, {"isHDVersion", l_false},
        {"createJoint", l_createJoint}, {"createJoints", l_createJoints}, {"setJointParameters", l_setJointParameters},
        {"checkJointLimits", l_checkJointLimits}, {"destroyJoint", l_destroyJoint},
        {"removeObject", l_removeObject}, {"applyImpulse", l_applyImpulse}, {"applyForce", l_applyForce},
        {"setPosition", l_setPosition}, {"setRotation", l_setRotation}, {"setVelocity", l_setVelocity},
        {"setAngularVelocity", l_setAngularVelocity}, {"setSleeping", l_setSleeping},
        {"getAngle", l_getAngle}, {"getWorldPoint", l_getWorldPoint}, {"setObjectParameter", l_setObjectParameter},
        {"setPhysicsSimulationScale", l_setPhysicsSimulationScale}, {"setPhysicsEnabled", l_setPhysicsEnabled},
        {"isPhysicsEnabled", l_isPhysicsEnabled}, {"setLevelLimits", l_setLevelLimits},
        {"setTopLeft", l_setTopLeft}, {"setWorldScale", l_setWorldScale}, {"setMaxWorldScale", l_setMaxWorldScale},
        {"setMaxTranslation", l_setMaxTranslation},
        {"setRenderState", l_setRenderState}, {"drawRect", l_drawRect}, {"drawLine2D", l_drawLine2D},
        {"drawTexturedRect", l_nothing}, {"drawSome", l_nothing},
        {"drawGameNative", l_drawGameNative}, {"drawBackgroundNative", l_drawBackgroundNative},
        {"drawForegroundNative", l_drawForegroundNative}, {"drawParticlesNative", l_drawParticlesNative},
        {"setSprite", l_setSprite}, {"setMaterial", l_setMaterial}, {"setTexture", l_setTexture}, {"setTheme", l_setTheme},
        {"clipText", l_clipText},
        {"startNewTrajectory", l_startNewTrajectory}, {"addToTrajectory", l_addToTrajectory},
        {"addPuffToTrajectory", l_addPuffToTrajectory},
        // platform services
        {"openRegistrationDialog", l_nothing}, {"registerKey", l_nothing}, {"getDeviceID", l_getDeviceID}, {"verifyDeviceID", l_verifyDeviceID}, {"areDeviceIDsEqual", l_areDeviceIDsEqual},
        {"requestLatestVersionNumber", l_nothing}, {"checkForUpdates", l_nothing}, {"callVersionUpdater", l_nothing},
        {"goToTaskSwitcherLua", l_nothing}, {"activateCrystalUI", l_nothing}, {"activateCrystalUIAtProfile", l_nothing},
        {"deactivateCrystalUI", l_nothing}, {"showCrystalSplash", l_nothing}, {"isCrystalSplashShowing", l_false},
        {"isCrystalUIShowing", l_false}, {"userEnabledCrystal", l_false}, {"avoidCrystalBackgroundActivity", l_nothing},
        {"initGameCenter", l_nothing}, {"getLeaderboardScoresForPlayers", l_nothing},
        {"getLeaderboardScoresForRange", l_nothing}, {"showLeaderboards", l_nothing}, {"showAchievements", l_nothing},
        {"postHighscore", l_nothing}, {"unlockAchievement", l_nothing},
        {"logFlurryEvent", l_nothing}, {"logFlurryEventWithParam", l_nothing}, {"logFlurryEventWithParams", l_nothing},
        {"playVideo", l_nothing},
        // Angry Birds Seasons additions (not implemented yet)
        {"clearParticlesNative", l_clearParticlesNative}, {"loadLevelFile", l_nothing}, {"getLoadStatus", l_getLoadStatus},
        {"createThemeSprite", l_createThemeSprite},
        {"modifyThemeSprite", l_modifyThemeSprite},
        {"removeThemeSprite", l_removeThemeSprite},
        {"updateThemeSprite", l_updateThemeSprite},
        {"drawLayer", l_nothing},
        {"drawRectLines", l_drawRectLines}, {"drawBoxNative", l_drawBoxNative}, {"getCurrentTime", l_getCurrentTime},
        {"getTimeDifference", l_getTimeDifference}, {"setAlpha", l_setAlpha}, {"drawUITextNative", l_drawUITextNative},
        {"getTImeDifferenceInSeconds", l_getTimeDifferenceInSeconds}, {"checkRegistrationResult", l_false}, {"getClipboardText", l_emptyString},
        {"canOpenProgram", l_false}, {"openProgram", l_nothing}, {"hasLocationCapability", l_false}, {"drawLevelParticlesNative", l_drawLevelParticlesNative},
        {"setWorldGravity", l_setWorldGravity},
        {"getOSName", l_osName}, {"getOSVersion", l_osVersion}, {"getModel", l_deviceModel},
        {"getManufacturer", l_deviceManufacturer}, {"getCurrentLocale", l_currentLocale},
        {"initLocales", l_nothing}, {"setRetinaResolution", l_nothing},
        // Angry Birds Rio: ads, analytics and the NFC eagle are not there on the PC
        {"flushMedioEvents", l_nothing}, {"logMedioAnonymousIDtoFlurry", l_nothing}, {"logMedioEvent", l_nothing},
        {"hideBannerAd", l_nothing}, {"showBannerAd", l_nothing}, {"requestBannerAd", l_nothing},
        {"requestInterstitialAd", l_nothing}, {"requestAndShowInterstitialAd", l_nothing}, {"showInterstitialAd", l_nothing},
        {"toggleNFC", l_nothing}, {"enableSmoothZooming", l_nothing}, {"setGameParameters", l_nothing},
        {"throwException", l_throwException}, {"setNotificationCallback", l_nothing}, {"requestAd", l_nothing}, {"showAd", l_nothing}, {"hideAd", l_nothing},
        {"requestExpandableAd", l_nothing}, {"showExpandableAd", l_nothing}, {"stopAds", l_nothing},
        {"clearParticles", l_clearParticlesNative},
        {"iapBuyItem", l_nothing},
        {"iapGetItemAt", l_nothing},
        {"iapGetItemCount", l_nothing},
        {"iapInitItemPurchase", l_nothing},
        {"iapInitTimeOut", l_nothing},
        {"requestAd", l_nothing},
        {"showAdvertisement", l_nothing},
        {"hideAdvertisement", l_nothing},
        {"requestInterstitial", l_nothing},
        {"showInterstitial", l_nothing},
        {"requestVideoAd", l_nothing},
        {"requestAndShowVideo", l_nothing},
        {"showVideoAdvertisement", l_nothing},
        {"enableiAds", l_nothing},
        {"disableiAds", l_nothing},
        {"addNotificationAfter", l_nothing},
        {"removeNotification", l_nothing},
        {"isWebViewLoading", l_false},
        {"videoReady", l_false},
        {"iAdsSupported", l_false},
    };
    Register(L, p, 0, globals, sizeof(globals) / sizeof(globals[0]));

    static const Entry res[] = {
        {"setPath", r_setPath}, {"createSpriteSheet", r_createSpriteSheet}, {"releaseSpriteSheet", r_releaseSpriteSheet},
        {"createCompoSpriteSet", r_createCompoSpriteSet}, {"releaseCompoSpriteSet", r_releaseCompoSpriteSet},
        {"createBitmapFont", r_createBitmapFont}, {"useFont", r_useFont},
        {"createTextGroupSet", r_createTextGroupSet}, {"loadLocale", r_loadLocale}, {"useLocale", r_useLocale},
        {"getString", r_getString}, {"drawSprite", r_drawSprite}, {"drawCompoSprite", r_drawCompoSprite},
        {"drawString", r_drawString}, {"setClipRect", r_setClipRect},
        {"getSpriteBounds", r_getSpriteBounds}, {"getSpritePivot", r_getSpritePivot},
        {"getCompoSpriteBounds", r_getCompoSpriteBounds},
        {"getStringWidth", r_getStringWidth}, {"getFontHeight", r_getFontHeight}, {"getFontLeading", r_getFontLeading}, {"getFontTracking", r_getFontTracking},
        {"getFontMaxAscending", r_getFontMaxAscending}, {"getFontMaxDescending", r_getFontMaxDescending},
        {"createAudioOutput", r_createAudioOutput}, {"startAudioOutput", r_startAudioOutput},
        {"stopAudioOutput", r_stopAudioOutput}, {"createAudio", r_createAudio}, {"playAudio", r_playAudio},
        {"stopAudio", r_stopAudio}, {"stopAllAudio", r_stopAllAudio}, {"isAudioPlaying", r_isAudioPlaying},
        {"setTrackVolume", r_setTrackVolume}, {"getTrackVolume", r_getTrackVolume}, {"openURL", r_openURL},
    };
    lua_newtable(L);
    Register(L, p, lua_gettop(L), res, sizeof(res) / sizeof(res[0]));
    lua_setglobal(L, "res");   // real global table, reached by scripts as _G.res

    static const Entry particles[] = {
        {"addParticles", p_addParticles}, {"addLevelParticles", p_addLevelParticles}, {"setSoftLimit", p_setSoftLimit}, {"setHardLimit", p_setHardLimit},
    };
    lua_newtable(L);
    Register(L, p, lua_gettop(L), particles, sizeof(particles) / sizeof(particles[0]));
    lua_setglobal(L, "particles");
}

// Runs a data file in a fresh, empty environment table and registers that table as the script
// global `name` (the exe's load_lua_file, 0x40ae20: blocks.lua -> blockTable, particles.lua -> particleTable).
bool LoadTableFile(lua_State* L, const char* name, const std::string& path, std::string* error, bool append) {
    // append: the file runs in the table that already is the global `name` (Rio fills blockTable from many files)
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, name);
    if (append && lua_istable(L, -1)) lua_remove(L, -2);
    else { lua_pop(L, 2); lua_newtable(L); }
    bool ok = false;
    // The exe decrypts the file (AES-256-CBC) before running it; a plain-text level is used as it is.
    std::vector<uint8_t> bytes;
    int loaded = 1;
    std::string loadError = "No such file or directory";
    ab::ReadLuaFile(path, bytes);
    if (!bytes.empty()) {
        loaded = luaL_loadbuffer(L, (const char*)bytes.data(), bytes.size(), path.c_str());
        if (loaded != 0) { loadError = lua_tostring(L, -1); lua_pop(L, 1); }
    }
    if (loaded == 0) {
        lua_pushvalue(L, -2);
        lua_setfenv(L, -2);
        if (lua_pcall(L, 0, 0, 0) == 0) ok = true;
    }
    if (!ok) {
        if (error) *error = path + ": " + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "error");
        lua_pop(L, 2);
        return false;
    }
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, name);
    lua_pop(L, 2);
    return true;
}

// Angry Birds Rio (1.4.x): the data comes in many files instead of blocks.lua (the exe's loadLevel, strings
// "Loading materials..." to "Loading particles...").
bool LoadRioTables(lua_State* L) {
    std::string base;
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "scriptPath");
    base = lua_isstring(L, -1) ? lua_tostring(L, -1) : "scripts";
    lua_pop(L, 2);
    std::string err;
    auto load = [&](const char* name, const char* file, bool append) {
        if (!LoadTableFile(L, name, base + file, &err, append)) {
            lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
            lua_pop(L, 1);
            std::fprintf(stderr, "loadLevel: %s\n", err.c_str());
            return false;
        }
        return true;
    };
    bool ok = true;
    // A new blockTable. Each file builds lists of definitions (pigs, birds, woodBlocks...); the exe files every
    // definition under blockTable.blocks[definition]. Plain numbers a file defines (monkeyIdleTimeIntervalMin...)
    // stay globals.
    PushEnv(L);
    lua_newtable(L);
    lua_newtable(L);
    lua_setfield(L, -2, "blocks");
    lua_setfield(L, -2, "blockTable");
    lua_pop(L, 1);
    const char* blockFiles[] = {"/pigs.lua", "/levelgoals.lua", "/scoreobjects.lua", "/birds.lua", "/blocks_hazard.lua",
                                "/blocks_wood.lua", "/blocks_stone.lua", "/blocks_glass.lua", "/blocks_misc.lua",
                                "/blocks_static.lua", "/blocks_staticfragile.lua", "/blocks_decorations.lua"};
    for (const char* f : blockFiles) {
        std::vector<uint8_t> bytes;
        ab::ReadLuaFile(base + f, bytes);
        if (bytes.empty() || luaL_loadbuffer(L, (const char*)bytes.data(), bytes.size(), f) != 0) {
            std::fprintf(stderr, "loadLevel: cannot load %s%s\n", f, bytes.empty() ? "" : (std::string(": ") + lua_tostring(L, -1)).c_str());
            if (!bytes.empty()) lua_pop(L, 1);
            ok = false;
            continue;
        }
        lua_newtable(L);                                // the file's own globals: [chunk, T]
        lua_newtable(L);
        PushEnv(L);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        lua_setfenv(L, -3);
        lua_pushvalue(L, -2);
        if (lua_pcall(L, 0, 0, 0) != 0) {
            std::fprintf(stderr, "loadLevel: %s: %s\n", f, lua_tostring(L, -1));
            lua_pop(L, 3);
            ok = false;
            continue;
        }
        lua_remove(L, -2);                              // [T]
        PushEnv(L);
        lua_insert(L, -2);                              // [env, T]
        lua_getfield(L, -2, "blockTable");
        lua_getfield(L, -1, "blocks");                  // [env, T, blockTable, blocks]
        lua_pushnil(L);
        while (lua_next(L, -4)) {                       // T's key, value
            if (lua_isnumber(L, -1) && lua_type(L, -2) == LUA_TSTRING) {
                lua_pushvalue(L, -2);
                lua_pushvalue(L, -2);
                lua_settable(L, -8);                    // env[key] = number
            } else if (lua_istable(L, -1)) {
                int n = (int)lua_objlen(L, -1);
                for (int i = 1; i <= n; i++) {
                    lua_rawgeti(L, -1, i);
                    lua_getfield(L, -1, "definition");
                    if (lua_isstring(L, -1)) {
                        FileInEditorPalette(L, lua_gettop(L) - 3, lua_gettop(L) - 1, i);
                        lua_pushvalue(L, -1);
                        lua_pushvalue(L, -3);
                        lua_settable(L, -7);            // blocks[definition] = entry
                    }
                    lua_pop(L, 2);
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 4);
    }
    // the exe adds the ground itself: an immovable box
    PushEnv(L);
    lua_getfield(L, -1, "blockTable");
    lua_getfield(L, -1, "blocks");
    lua_newtable(L);
    lua_pushstring(L, "Ground"); lua_setfield(L, -2, "definition");
    lua_pushstring(L, "box"); lua_setfield(L, -2, "type");
    lua_pushstring(L, ""); lua_setfield(L, -2, "sprite");
    lua_pushstring(L, ""); lua_setfield(L, -2, "texture");
    lua_pushstring(L, "immovable"); lua_setfield(L, -2, "material");
    lua_setfield(L, -2, "Ground");
    lua_pop(L, 3);
    // materials, damage factors, themes and groups each fill a table of their own, which the scripts read
    // as blockTable.materials, blockTable.damageFactors, blockTable.themes and blockTable.groups
    struct Nested { const char* name; const char* file; };
    const Nested nested[] = {{"materials", "/materials.lua"}, {"damageFactors", "/damagefactors.lua"},
                             {"themes", "/themes.lua"}, {"groups", "/groups.lua"}};
    for (const Nested& n : nested) {
        ok = load(n.name, n.file, false) && ok;
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "blockTable");
        lua_getfield(L, -2, n.name);
        if (lua_istable(L, -2)) lua_setfield(L, -2, n.name); else lua_pop(L, 1);
        lua_pop(L, 2);
    }
    ok = load("particleTable", "/particles.lua", false) && ok;
    return ok;
}

}  // namespace ab
