// The editor's saveLevel(file): writes the level being edited (the global `objects`) as a Lua file that loadLevel reads back.
#pragma once
#include <string>
#include <utility>
#include <vector>

struct lua_State;

namespace ab {

// Writes `objects` to `path` (replacing any file there). `seasons` adds the per-block strength Seasons levels carry.
// Returns false and sets `error` when the level could not be written.
// `text`, when given, also receives the level's text (the editor keeps its temp copies in memory as well).
bool WriteLevelFile(lua_State* L, const std::string& path, bool seasons, std::string& error, std::string* text = nullptr);

bool DirectoryExists(const std::string& path);
// (3.8) the names in a folder, with true for a sub-folder ("." and ".." left out), sorted by name; false when the folder cannot be read
bool ListDirectory(const std::string& path, std::vector<std::pair<std::string, bool>>& out);
void MakeDirectory(const std::string& path);   // one level; an existing folder is left alone

}  // namespace ab
