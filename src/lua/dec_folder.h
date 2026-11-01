// The "dec" folder: plain copies of the game's scripts and levels.
//   AngryBirdsEngine.exe --decrypt [folder]    writes every .lua file of the game's data folder, decrypted, to <exe folder>\dec (or folder)
//   AngryBirdsEngine.exe --use-dec [folder]    reads the scripts and levels from that folder first, so edited copies are used
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ab {

// What DecryptTree did: the dec folder holds plain-text Lua only (v2.2).
struct DecStats { int written = 0, fromBytecode = 0, failed = 0; std::vector<std::string> failedFiles; };

// Decrypts every .lua file below `dataRoot` into `decRoot`, keeping the folder layout. A file that is compiled Lua 5.1 bytecode (1.6.3.1, 2.0.0,
// Seasons, Rio, the iOS .ipa) is decompiled to text with unluac: `java -jar <unluacJar> file`, the same call the AB360 LUA Manager makes. A file
// that cannot be turned into text is not written to the dec folder (the game then keeps using its own copy). Returns the number of files written.
int DecryptTree(const std::string& dataRoot, const std::string& decRoot, const std::vector<std::string>& unluacJars, DecStats& stats);

// The unluac jars to try, best first: AB_UNLUAC, tools\unluac.jar (1.2.2) and tools\unluac-alt.jar (a version that can decompile what 1.2.2 cannot, but
// writes goto) beside the exe and in the working folder. A decompiled file counts only if Lua 5.1 loads it.
std::vector<std::string> FindUnluac(const std::string& exeFolder);

// From now on ReadLuaFile looks for a file below `dataRoot` in `decRoot` first.
void UseDecFolder(const std::string& dataRoot, const std::string& decRoot);

// ReadLuaFile's hook: true (and the file's bytes, not decrypted again) if the dec folder has the file.
bool ReadDecOverride(const std::string& path, std::vector<uint8_t>& bytes);

}  // namespace ab
