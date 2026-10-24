#include "lua/lua_frame_host.h"

#include <cstdio>

extern "C" {
#include "lauxlib.h"
}

namespace ab {

void LuaFrameHost::PushObjectsEntry(const char* which, const std::string& name) {
    lua_State* L = scripts_.State();
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "objects");
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) return;                    // leaves the non-table on the stack
    lua_getfield(L, -1, which);
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) return;
    lua_getfield(L, -1, name.c_str());
    lua_remove(L, -2);
}

void LuaFrameHost::SetObjectNumber(int id, const char* key, float value) {
    lua_State* L = scripts_.State();
    PushObjectsEntry("world", ObjectName(id));
    if (lua_istable(L, -1)) {
        lua_pushnumber(L, value);
        lua_setfield(L, -2, key);
    }
    lua_pop(L, 1);
}

void LuaFrameHost::SetObjectBool(int id, const char* key, bool value) {
    lua_State* L = scripts_.State();
    PushObjectsEntry("world", ObjectName(id));
    if (lua_istable(L, -1)) {
        lua_pushboolean(L, value);
        lua_setfield(L, -2, key);
    }
    lua_pop(L, 1);
}

void LuaFrameHost::SetGameBool(const char* key, bool value) {
    lua_State* L = scripts_.State();
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_pushboolean(L, value);
    lua_setfield(L, -2, key);
    lua_pop(L, 1);
}

void LuaFrameHost::SetJointNumber(int jointIndex, const char* key, float value) {
    lua_State* L = scripts_.State();
    PushObjectsEntry("joints", JointName(jointIndex));
    if (lua_istable(L, -1)) {
        lua_pushnumber(L, value);
        lua_setfield(L, -2, key);
    }
    lua_pop(L, 1);
}

// objects.world[name].strength = 0, then deadBlocks[name] = objects.world[name]
void LuaFrameHost::MarkBlockDead(int id) {
    lua_State* L = scripts_.State();
    PushObjectsEntry("world", ObjectName(id));
    if (lua_istable(L, -1)) {
        lua_pushnumber(L, 0);
        lua_setfield(L, -2, "strength");
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "deadBlocks");
        lua_remove(L, -2);
        if (lua_istable(L, -1)) {
            lua_pushvalue(L, -2);
            lua_setfield(L, -2, ObjectName(id).c_str());
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

void LuaFrameHost::LogBirdVelocity(int id, float vx, float vy) {
    std::printf("Bird %s new velocity is x: %f, y: %f\n", ObjectName(id).c_str(), vx, vy);
}

}  // namespace ab
