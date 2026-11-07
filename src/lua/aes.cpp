#include "lua/aes.h"
#include "lua/sevenzip.h"
#include "lua/dec_folder.h"
#include <fstream>
#include <iterator>

namespace ab {

const char kLevelKey[33] = "USCaPQpA4TSNVxMI1v9SK9UC0yZuAnb2";
const char kSeasonsKey[33] = "zePhest5faQuX2S2Apre@4reChAtEvUt";   // Game+0x31c in Angry Birds Seasons
const char kSpaceKey[33] = "RmgdZ0JenLFgWwkYvCL2lSahFbEhFec4";   // Angry Birds Space scripts (found by the community; every Space .lua decrypts to Lua 5.1 bytecode)
const char kSaveKey[33] = "44iUY5aTrlaYoet9lapRlaK1Ehlec5i0";
const char kSpaceSaveKey[33] = "TpeczKQL07HVdPbVUhAr6FjUsmRctyc5";     // Space save files (the exe builds it byte by byte at 0x496180)
const char kStarWarsKey[33] = "An8t3mn8U6spiQ0zHHr3a1loDrRa3mtE";   // Angry Birds Star Wars scripts: AES, then a 7z archive (LZMA) holding the compiled script
const char kStarWars2Key[33] = "B0pm3TAlzkN9ghzoe2NizEllPdN0hQni";  // Angry Birds Star Wars II scripts, same container
const char kStarWarsSaveKey[33] = "e83Tph0R3aZ2jGK6eS91uLvQpL33vzNi";   // Star Wars save files (built byte by byte in the exe, like Space's)
const char kSeasonsSaveKey[33] = "brU4u=EbR4s_A3APu6U#7B!axAm*We#5";   // Seasons settings.lua / highscores.lua

namespace {

uint8_t sbox[256], inv[256];
uint8_t m9[256], m11[256], m13[256], m14[256];   // InvMixColumns multipliers (decrypting 90 KB of blocks.lua took seconds without them)

uint8_t Mul2(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

uint8_t Mul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) r ^= a;
        a = Mul2(a);
        b >>= 1;
    }
    return r;
}

// The S-box: multiplicative inverse in GF(2^8), then the affine transform.
void InitTables() {
    static bool done = false;
    if (done) return;
    for (int i = 0; i < 256; i++) {
        uint8_t x = 0;
        if (i) for (int j = 1; j < 256; j++) if (Mul((uint8_t)i, (uint8_t)j) == 1) { x = (uint8_t)j; break; }
        uint8_t s = x;
        for (int k = 1; k <= 4; k++) s ^= (uint8_t)((x << k) | (x >> (8 - k)));
        s ^= 0x63;
        sbox[i] = s;
        inv[s] = (uint8_t)i;
    }
    for (int i = 0; i < 256; i++) {
        m9[i] = Mul((uint8_t)i, 9); m11[i] = Mul((uint8_t)i, 11); m13[i] = Mul((uint8_t)i, 13); m14[i] = Mul((uint8_t)i, 14);
    }
    done = true;
}

// key expansion: 15 round keys of 16 bytes
void ExpandKey(const uint8_t key[32], uint8_t w[240]) {
    InitTables();
    for (int i = 0; i < 32; i++) w[i] = key[i];
    uint8_t rcon = 1;
    for (int i = 8; i < 60; i++) {
        uint8_t t[4] = {w[(i - 1) * 4], w[(i - 1) * 4 + 1], w[(i - 1) * 4 + 2], w[(i - 1) * 4 + 3]};
        if (i % 8 == 0) {
            uint8_t first = t[0];
            t[0] = (uint8_t)(sbox[t[1]] ^ rcon); t[1] = sbox[t[2]]; t[2] = sbox[t[3]]; t[3] = sbox[first];
            rcon = Mul2(rcon);
        } else if (i % 8 == 4) {
            for (int j = 0; j < 4; j++) t[j] = sbox[t[j]];
        }
        for (int j = 0; j < 4; j++) w[i * 4 + j] = (uint8_t)(w[(i - 8) * 4 + j] ^ t[j]);
    }
}

}  // namespace

bool AesDecryptCbc(const uint8_t key[32], const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    if (in.empty() || in.size() % 16 != 0) return false;
    uint8_t w[240];
    ExpandKey(key, w);

    out.resize(in.size());
    uint8_t prev[16] = {0};
    for (size_t pos = 0; pos < in.size(); pos += 16) {
        uint8_t s[16];
        for (int i = 0; i < 16; i++) s[i] = (uint8_t)(in[pos + i] ^ w[14 * 16 + i]);
        for (int round = 13; round >= 0; round--) {
            // inverse shift rows (state is column major: s[col * 4 + row])
            uint8_t t[16];
            for (int c = 0; c < 4; c++)
                for (int r = 0; r < 4; r++) t[((c + r) % 4) * 4 + r] = s[c * 4 + r];
            for (int i = 0; i < 16; i++) s[i] = (uint8_t)(inv[t[i]] ^ w[round * 16 + i]);
            if (round > 0) {
                for (int c = 0; c < 4; c++) {
                    uint8_t a0 = s[c * 4], a1 = s[c * 4 + 1], a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
                    s[c * 4]     = (uint8_t)(m14[a0] ^ m11[a1] ^ m13[a2] ^ m9[a3]);
                    s[c * 4 + 1] = (uint8_t)(m9[a0] ^ m14[a1] ^ m11[a2] ^ m13[a3]);
                    s[c * 4 + 2] = (uint8_t)(m13[a0] ^ m9[a1] ^ m14[a2] ^ m11[a3]);
                    s[c * 4 + 3] = (uint8_t)(m11[a0] ^ m13[a1] ^ m9[a2] ^ m14[a3]);
                }
            }
        }
        for (int i = 0; i < 16; i++) { out[pos + i] = (uint8_t)(s[i] ^ prev[i]); prev[i] = in[pos + i]; }
    }
    uint8_t pad = out.back();
    if (pad == 0 || pad > 16) return false;
    for (int i = 0; i < pad; i++) if (out[out.size() - 1 - i] != pad) return false;
    out.resize(out.size() - pad);
    return true;
}

void AesEncryptCbc(const uint8_t key[32], const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    uint8_t w[240];
    ExpandKey(key, w);
    std::vector<uint8_t> data = in;
    uint8_t pad = (uint8_t)(16 - data.size() % 16);
    data.insert(data.end(), pad, pad);
    out.resize(data.size());
    uint8_t prev[16] = {0};
    for (size_t pos = 0; pos < data.size(); pos += 16) {
        uint8_t s[16];
        for (int i = 0; i < 16; i++) s[i] = (uint8_t)(data[pos + i] ^ prev[i] ^ w[i]);
        for (int round = 1; round <= 14; round++) {
            uint8_t t[16];
            for (int c = 0; c < 4; c++)
                for (int r = 0; r < 4; r++) t[c * 4 + r] = sbox[s[((c + r) % 4) * 4 + r]];   // sub bytes + shift rows
            if (round < 14) {
                for (int c = 0; c < 4; c++) {
                    uint8_t a0 = t[c * 4], a1 = t[c * 4 + 1], a2 = t[c * 4 + 2], a3 = t[c * 4 + 3];
                    s[c * 4]     = (uint8_t)(Mul2(a0) ^ (Mul2(a1) ^ a1) ^ a2 ^ a3);
                    s[c * 4 + 1] = (uint8_t)(a0 ^ Mul2(a1) ^ (Mul2(a2) ^ a2) ^ a3);
                    s[c * 4 + 2] = (uint8_t)(a0 ^ a1 ^ Mul2(a2) ^ (Mul2(a3) ^ a3));
                    s[c * 4 + 3] = (uint8_t)((Mul2(a0) ^ a0) ^ a1 ^ a2 ^ Mul2(a3));
                }
            } else {
                for (int i = 0; i < 16; i++) s[i] = t[i];
            }
            for (int i = 0; i < 16; i++) s[i] ^= w[round * 16 + i];
        }
        for (int i = 0; i < 16; i++) { out[pos + i] = s[i]; prev[i] = s[i]; }
    }
}

bool ReadSaveFile(const std::string& path, std::string& text, bool& encrypted, const char** keyUsed) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()), plain;
    encrypted = false;
    if (keyUsed) *keyUsed = kSaveKey;
    const char* keys[] = {kSaveKey, kSeasonsSaveKey, kSpaceSaveKey, kStarWarsSaveKey};
    for (size_t k = 0; k < 4 && !encrypted; k++) {
        if (!AesDecryptCbc((const uint8_t*)keys[k], bytes, plain)) continue;
        encrypted = true;
        for (size_t i = 0; i < plain.size() && i < 64; i++)
            if (plain[i] < 0x20 && plain[i] != '\t' && plain[i] != '\n' && plain[i] != '\r') encrypted = false;
        if (encrypted && keyUsed) *keyUsed = keys[k];
    }
    if (encrypted) bytes = plain;
    else if (!bytes.empty() && bytes.size() % 16 == 0) {
        // not text and no known save key opens it (a game whose save key is not known yet): it is somebody's real save, so it is
        // kept as <file>.original and the game starts with empty saves instead of choking on the cipher text
        bool binary = false;
        for (size_t i = 0; i < bytes.size() && i < 64; i++)
            if (bytes[i] < 0x20 && bytes[i] != '\t' && bytes[i] != '\n' && bytes[i] != '\r') binary = true;
        if (binary) {
            std::ifstream probe((path + ".original").c_str(), std::ios::binary);
            if (!probe) { std::ofstream keep((path + ".original").c_str(), std::ios::binary); keep.write((const char*)bytes.data(), (std::streamsize)bytes.size()); }
            bytes.clear();
        }
    }
    text.assign(bytes.begin(), bytes.end());
    return true;
}

bool ReadLuaFile(const std::string& path, std::vector<uint8_t>& bytes) {
    if (ReadDecOverride(path, bytes)) return true;       // --use-dec: an edited plain copy in the dec folder wins
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    std::vector<uint8_t> plain;
    const char* keys[] = {kLevelKey, kSeasonsKey, kSpaceKey, kStarWarsKey, kStarWars2Key};
    const size_t kKeys = 5;
    static size_t firstKey = 0;                  // the key that worked last time goes first: a wrong key costs a whole file
    for (size_t n = 0; n < kKeys; n++) {
        size_t k = (n + firstKey) % kKeys;
        if (!AesDecryptCbc((const uint8_t*)keys[k], bytes, plain) || plain.size() <= 4) continue;
        // the decrypted file is either bytecode (starts with 0x1b) or plain Lua text; a file that was
        // not encrypted at all (or a wrong key) turns into noise, which has control characters near the start
        bool text = true;
        for (size_t i = 0; i < plain.size() && i < 64; i++)
            if (plain[i] < 0x20 && plain[i] != '\t' && plain[i] != '\n' && plain[i] != '\r') text = false;
        if (plain.size() > 6 && plain[0] == '7' && plain[1] == 'z' && plain[2] == 0xBC && plain[3] == 0xAF) {   // Star Wars: a 7z archive with the script inside
            std::vector<uint8_t> unpacked;
            if (SevenZipExtractFirst(plain, unpacked)) { bytes = unpacked; firstKey = k; break; }
            continue;
        }
        if (plain[0] == 0x1b || text) { bytes = plain; firstKey = k; break; }
    }
    return true;
}

}  // namespace ab
