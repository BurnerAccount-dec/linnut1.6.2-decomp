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

// Returns false when the size is not a multiple of 16 or the padding is wrong.
bool AesDecryptCbc(const uint8_t key[32], const std::vector<uint8_t>& in, std::vector<uint8_t>& out);

// Reads a Lua file. If it is an encrypted one (it decrypts with kLevelKey to Lua bytecode or to plain
// Lua text) the decrypted content is returned, otherwise the file as it is (plain text or plain
// bytecode). Returns false when the file cannot be opened.
bool ReadLuaFile(const std::string& path, std::vector<uint8_t>& bytes);

}  // namespace ab
