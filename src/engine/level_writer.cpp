#include "engine/level_writer.h"

#include <cstdio>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif
#include <fstream>
#include <map>
#include <set>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace ab {
namespace {

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out + "\"";
}

std::string Number(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

// Entries of the table at stack index `t`, in key order (numeric keys first, then names); functions and userdata are skipped.
std::vector<std::pair<std::string, int>> SortedKeys(lua_State* L, int t) {
    std::map<std::string, int> names;
    std::map<double, int> numbers;
    lua_pushnil(L);
    while (lua_next(L, t)) {
        int vt = lua_type(L, -1);
        bool saveable = vt == LUA_TNUMBER || vt == LUA_TSTRING || vt == LUA_TBOOLEAN || vt == LUA_TTABLE;
        if (saveable && lua_type(L, -2) == LUA_TNUMBER) numbers[lua_tonumber(L, -2)] = 1;
        else if (saveable && lua_type(L, -2) == LUA_TSTRING) names[lua_tostring(L, -2)] = 1;
        lua_pop(L, 1);
    }
    std::vector<std::pair<std::string, int>> keys;
    for (auto& n : numbers) keys.push_back({Number(n.first), 1});
    for (auto& n : names) keys.push_back({n.first, 0});
    return keys;
}

// Writes the value on top of the stack. `only` (if not null) limits the keys of a table to those names.
void WriteValue(lua_State* L, std::string& out, int depth, const std::set<std::string>* only = nullptr) {
    switch (lua_type(L, -1)) {
        case LUA_TNUMBER: out += Number(lua_tonumber(L, -1)); return;
        case LUA_TBOOLEAN: out += lua_toboolean(L, -1) ? "true" : "false"; return;
        case LUA_TSTRING: out += Quote(lua_tostring(L, -1)); return;
        case LUA_TTABLE: break;
        default: out += "nil"; return;
    }
    if (depth > 8) { out += "{}"; return; }       // a table that refers to itself
    int t = lua_gettop(L);
    std::string pad((depth + 1) * 2, ' ');
    out += "{\n";
    for (auto& key : SortedKeys(L, t)) {
        if (only && !only->count(key.first)) continue;
        if (key.second) lua_pushnumber(L, std::atof(key.first.c_str()));
        else lua_pushstring(L, key.first.c_str());
        lua_gettable(L, t);
        out += pad + (key.second ? "[" + key.first + "]" : "[" + Quote(key.first) + "]") + " = ";
        WriteValue(L, out, depth + 1);
        out += ",\n";
        lua_pop(L, 1);
    }
    out += std::string(depth * 2, ' ') + "}";
}

// Moves `from` over `to` (std::rename refuses to replace a file on Windows).
bool ReplaceFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
    SetFileAttributesA(to.c_str(), FILE_ATTRIBUTE_NORMAL);       // a level copied off a disc can be read-only
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

}  // namespace

bool DirectoryExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

void MakeDirectory(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

bool WriteLevelFile(lua_State* L, const std::string& path, bool seasons, std::string& error) {
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "objects");
    if (!lua_istable(L, -1)) { lua_settop(L, top); error = "there is no level to save"; return false; }
    int objects = lua_gettop(L);

    // the editor keeps the live state of every block here; a level file holds the part that is authored
    std::set<std::string> blockFields = {"name", "definition", "x", "y", "angle", "startNumber", "respawn"};
    if (seasons) blockFields.insert("strength");
    // runtime tables of `objects` that belong to the running level, not to the file
    std::set<std::string> skipped = {"world", "loadedObjects", "deadBlocks", "levelParticles", "filename"};

    std::string fileName = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
    std::string out = "theme = ";
    lua_getfield(L, objects, "theme");
    out += lua_isstring(L, -1) ? Quote(lua_tostring(L, -1)) : "\"theme1\"";
    lua_pop(L, 1);
    out += "\nfilename = " + Quote(fileName) + "\n";

    for (auto& key : SortedKeys(L, objects)) {
        if (key.second || key.first == "theme" || skipped.count(key.first)) continue;
        lua_getfield(L, objects, key.first.c_str());
        bool emptyList = lua_istable(L, -1) && lua_objlen(L, -1) == 0 && SortedKeys(L, lua_gettop(L)).empty();
        if (emptyList && key.first != "joints" && key.first != "counts") { lua_pop(L, 1); continue; }   // editor leftovers such as themeSprites = {}
        if (lua_istable(L, -1) || key.first == "physicsToWorld" || lua_type(L, -1) == LUA_TBOOLEAN || lua_type(L, -1) == LUA_TSTRING) {
            out += key.first + " = ";
            WriteValue(L, out, 0);
            out += "\n";
        }
        lua_pop(L, 1);
    }

    // what the level file held that the scripts do not keep in `objects` (trainCarts, weather, worldGravity...)
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.lastLevel");
    if (lua_istable(L, -1)) {
        int last = lua_gettop(L);
        for (auto& key : SortedKeys(L, last)) {
            if (key.second || key.first == "filename" || skipped.count(key.first)) continue;
            lua_getfield(L, objects, key.first.c_str());
            bool kept = !lua_isnil(L, -1);
            lua_pop(L, 1);
            if (kept) continue;
            lua_getfield(L, last, key.first.c_str());
            out += key.first + " = ";
            WriteValue(L, out, 0);
            out += "\n";
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // world: every block, the ground included (the original files list it too)
    out += "world = {\n";
    lua_getfield(L, objects, "world");
    if (lua_istable(L, -1)) {
        int world = lua_gettop(L);
        for (auto& key : SortedKeys(L, world)) {
            lua_getfield(L, world, key.first.c_str());
            if (lua_istable(L, -1)) {
                out += "  [" + Quote(key.first) + "] = ";
                WriteValue(L, out, 1, &blockFields);
                out += ",\n";
            }
            lua_pop(L, 1);
        }
    }
    out += "}\n";
    lua_settop(L, top);

    // written next to the target and moved over it, so a failed write never leaves half a level
    std::string temp = path + ".tmp";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f) { error = "cannot write " + temp; return false; }
        f << out;
        f.flush();
        if (!f) { error = "cannot write " + temp; return false; }
    }
    if (!ReplaceFile(temp, path)) { error = "cannot replace " + path; std::remove(temp.c_str()); return false; }
    return true;
}

}  // namespace ab
