// The "dec" folder: plain copies of the game's scripts and levels.
//   AngryBirdsEngine.exe --decrypt [folder]    writes every .lua file of the game's data folder, decrypted, to <exe folder>\dec (or folder)
//   AngryBirdsEngine.exe --use-dec [folder]    reads the scripts and levels from that folder first, so edited copies are used
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ab {

// Decrypts every .lua file below `dataRoot` into `decRoot`, keeping the folder layout. Returns the number of files written;
// `textFiles` counts those that came out as readable Lua source (the rest is compiled Lua 5.1 bytecode).
int DecryptTree(const std::string& dataRoot, const std::string& decRoot, int& textFiles);

// From now on ReadLuaFile looks for a file below `dataRoot` in `decRoot` first.
void UseDecFolder(const std::string& dataRoot, const std::string& decRoot);

// ReadLuaFile's hook: true (and the file's bytes, not decrypted again) if the dec folder has the file.
bool ReadDecOverride(const std::string& path, std::vector<uint8_t>& bytes);

}  // namespace ab
