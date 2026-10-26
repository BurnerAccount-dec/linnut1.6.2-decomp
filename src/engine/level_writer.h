// The editor's saveLevel(file): writes the level being edited (the global `objects`) as a Lua file that loadLevel reads back.
#pragma once
#include <string>

struct lua_State;

namespace ab {

// Writes `objects` to `path` (replacing any file there). `seasons` adds the per-block strength Seasons levels carry.
// Returns false and sets `error` when the level could not be written.
bool WriteLevelFile(lua_State* L, const std::string& path, bool seasons, std::string& error);

bool DirectoryExists(const std::string& path);
void MakeDirectory(const std::string& path);   // one level; an existing folder is left alone

}  // namespace ab
