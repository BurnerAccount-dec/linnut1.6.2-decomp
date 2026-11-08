// Registers the Lua functions the original exe exposes (about 130) and forwards them to a Platform.
#pragma once
extern "C" {
#include "lua.h"
}
#include <string>

#include "lua/platform.h"

namespace ab {

// Registers the global functions plus the `res` and `particles` tables on L.
// The Platform must outlive the Lua state.
void RegisterApi(lua_State* L, Platform* platform);

// Runs a data file in a fresh table and stores that table as the script global `name`.
bool LoadTableFile(lua_State* L, const char* name, const std::string& path, std::string* error, bool append = false);
bool LoadRioTables(lua_State* L);

// Calls the Lua handlers registered with engine.on(event, fn): the event name, up to two strings, then up to two numbers. Does nothing (and costs one table
// lookup) while no handler is registered.
void EmitHook(lua_State* L, const char* event, const char* s1 = nullptr, const char* s2 = nullptr, int numbers = 0, double a = 0, double b = 0);

}  // namespace ab
