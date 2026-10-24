// AES-256 decryption in CBC mode with a zero IV and PKCS#7 padding: the cipher the exe uses for
// its level files (and the *_1.lua scripts). Key: the 32 characters the Game constructor writes at
// this+0x2c4 (0x4058d2..0x405a43), see kLevelKey. Decrypted levels are Lua 5.1 bytecode.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ab {

extern const char kLevelKey[33];

// Returns false when the size is not a multiple of 16 or the padding is wrong.
bool AesDecryptCbc(const uint8_t key[32], const std::vector<uint8_t>& in, std::vector<uint8_t>& out);

}  // namespace ab
