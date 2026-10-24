// AES-256 decryption in CBC mode with a zero IV and PKCS#7 padding: the cipher the exe uses for
// its level files (and the *_1.lua scripts). Key: the 32 characters the Game constructor writes at
// this+0x2c4 (0x4058d2..0x405a43), see kLevelKey. Decrypted levels are Lua 5.1 bytecode.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ab {

extern const char kLevelKey[33];
// Key of the save files (settings.lua, highscores.lua written with saveLuaFile(..., appData = true)):
// the string the Game constructor writes at this+0x2b8.
extern const char kSaveKey[33];

// Returns false when the size is not a multiple of 16 or the padding is wrong.
bool AesDecryptCbc(const uint8_t key[32], const std::vector<uint8_t>& in, std::vector<uint8_t>& out);

// Encrypts with the same cipher (zero IV, PKCS#7 padding added here).
void AesEncryptCbc(const uint8_t key[32], const std::vector<uint8_t>& in, std::vector<uint8_t>& out);

// Reads a save file. `encrypted` tells whether it was an original encrypted one (decrypts with kSaveKey
// to text); otherwise the text is the file as it is. Returns false when the file cannot be opened.
bool ReadSaveFile(const std::string& path, std::string& text, bool& encrypted);

// Reads a Lua file. If it is an encrypted one (it decrypts with kLevelKey to Lua bytecode or to plain
// Lua text) the decrypted content is returned, otherwise the file as it is (plain text or plain
// bytecode). Returns false when the file cannot be opened.
bool ReadLuaFile(const std::string& path, std::vector<uint8_t>& bytes);

}  // namespace ab
