#include "lua/aes.h"
#include <fstream>
#include <iterator>

namespace ab {

const char kLevelKey[33] = "USCaPQpA4TSNVxMI1v9SK9UC0yZuAnb2";
const char kSaveKey[33] = "44iUY5aTrlaYoet9lapRlaK1Ehlec5i0";

namespace {

uint8_t sbox[256], inv[256];

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
                    s[c * 4]     = (uint8_t)(Mul(a0, 14) ^ Mul(a1, 11) ^ Mul(a2, 13) ^ Mul(a3, 9));
                    s[c * 4 + 1] = (uint8_t)(Mul(a0, 9) ^ Mul(a1, 14) ^ Mul(a2, 11) ^ Mul(a3, 13));
                    s[c * 4 + 2] = (uint8_t)(Mul(a0, 13) ^ Mul(a1, 9) ^ Mul(a2, 14) ^ Mul(a3, 11));
                    s[c * 4 + 3] = (uint8_t)(Mul(a0, 11) ^ Mul(a1, 13) ^ Mul(a2, 9) ^ Mul(a3, 14));
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

bool ReadSaveFile(const std::string& path, std::string& text, bool& encrypted) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()), plain;
    encrypted = false;
    if (AesDecryptCbc((const uint8_t*)kSaveKey, bytes, plain)) {
        encrypted = true;
        for (size_t i = 0; i < plain.size() && i < 64; i++)
            if (plain[i] < 0x20 && plain[i] != '\t' && plain[i] != '\n' && plain[i] != '\r') encrypted = false;
    }
    if (encrypted) bytes = plain;
    text.assign(bytes.begin(), bytes.end());
    return true;
}

bool ReadLuaFile(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    std::vector<uint8_t> plain;
    if (AesDecryptCbc((const uint8_t*)kLevelKey, bytes, plain) && plain.size() > 4) {
        // the decrypted file is either bytecode (starts with 0x1b) or plain Lua text; a file that was
        // not encrypted at all turns into noise, which has control characters near the start
        bool text = true;
        for (size_t i = 0; i < plain.size() && i < 64; i++)
            if (plain[i] < 0x20 && plain[i] != '\t' && plain[i] != '\n' && plain[i] != '\r') text = false;
        if (plain[0] == 0x1b || text) bytes = plain;
    }
    return true;
}

}  // namespace ab
