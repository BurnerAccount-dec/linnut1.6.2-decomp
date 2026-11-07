#include "sevenzip.h"

#include <cstring>

namespace ab {
namespace {

// ---- LZMA decoder (the algorithm of the public domain LZMA specification) ----
struct RangeDecoder {
    const uint8_t* p; const uint8_t* end; uint32_t range = 0xFFFFFFFFu, code = 0; bool bad = false;
    uint8_t Byte() { if (p >= end) { bad = true; return 0; } return *p++; }
    bool Init() { if (Byte() != 0) return false; for (int i = 0; i < 4; i++) code = (code << 8) | Byte(); return !bad && code != range; }
    void Normalize() { if (range < (1u << 24)) { range <<= 8; code = (code << 8) | Byte(); } }
    unsigned Bit(uint16_t& prob) {
        uint32_t bound = (range >> 11) * prob;
        unsigned symbol;
        if (code < bound) { range = bound; prob = (uint16_t)(prob + (((1u << 11) - prob) >> 5)); symbol = 0; }
        else { range -= bound; code -= bound; prob = (uint16_t)(prob - (prob >> 5)); symbol = 1; }
        Normalize();
        return symbol;
    }
    unsigned Direct(int bits) {
        unsigned res = 0;
        while (bits--) {
            range >>= 1; code -= range;
            uint32_t t = 0u - (code >> 31);
            code += range & t;
            res = (res << 1) + (t + 1);
            Normalize();
        }
        return res;
    }
};

unsigned BitTree(RangeDecoder& rc, uint16_t* probs, int bits) {
    unsigned m = 1;
    for (int i = 0; i < bits; i++) m = (m << 1) + rc.Bit(probs[m]);
    return m - (1u << bits);
}
unsigned BitTreeReverse(RangeDecoder& rc, uint16_t* probs, int bits) {
    unsigned m = 1, sym = 0;
    for (int i = 0; i < bits; i++) { unsigned b = rc.Bit(probs[m]); m = (m << 1) + b; sym |= b << i; }
    return sym;
}

struct LenDecoder {
    uint16_t choice, choice2, low[16][8], mid[16][8], high[256];
    void Init() {
        choice = choice2 = 1024;
        for (auto& a : low) for (auto& b : a) b = 1024;
        for (auto& a : mid) for (auto& b : a) b = 1024;
        for (auto& b : high) b = 1024;
    }
    unsigned Decode(RangeDecoder& rc, unsigned posState) {
        if (rc.Bit(choice) == 0) return BitTree(rc, low[posState], 3);
        if (rc.Bit(choice2) == 0) return 8 + BitTree(rc, mid[posState], 3);
        return 16 + BitTree(rc, high, 8);
    }
};

}  // namespace

bool LzmaDecode(const uint8_t* props, const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize) {
    unsigned d = props[0];
    if (d >= 9 * 5 * 5) return false;
    unsigned lc = d % 9; d /= 9; unsigned lp = d % 5, pb = d / 5;
    RangeDecoder rc{in, in + inSize};
    if (!rc.Init()) return false;

    std::vector<uint16_t> literal((size_t)0x300 << (lc + lp), 1024);
    uint16_t isMatch[12][16], isRep[12], isRepG0[12], isRepG1[12], isRepG2[12], isRep0Long[12][16];
    uint16_t posSlot[4][64], posDecoders[115], align[16];
    for (auto& a : isMatch) for (auto& b : a) b = 1024;
    for (auto& a : isRep0Long) for (auto& b : a) b = 1024;
    for (int i = 0; i < 12; i++) isRep[i] = isRepG0[i] = isRepG1[i] = isRepG2[i] = 1024;
    for (auto& a : posSlot) for (auto& b : a) b = 1024;
    for (auto& b : posDecoders) b = 1024;
    for (auto& b : align) b = 1024;
    LenDecoder lenDec, repLenDec; lenDec.Init(); repLenDec.Init();

    unsigned state = 0; uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    size_t pos = 0;
    while (pos < outSize) {
        unsigned posState = (unsigned)pos & ((1u << pb) - 1);
        if (rc.Bit(isMatch[state][posState]) == 0) {
            unsigned prev = pos ? out[pos - 1] : 0;
            uint16_t* probs = &literal[(size_t)0x300 * (((pos & ((1u << lp) - 1)) << lc) + (prev >> (8 - lc)))];
            unsigned sym = 1;
            if (state >= 7) {
                unsigned matchByte = out[pos - rep0 - 1];
                do {
                    unsigned matchBit = (matchByte >> 7) & 1; matchByte <<= 1;
                    unsigned bit = rc.Bit(probs[((1 + matchBit) << 8) + sym]);
                    sym = (sym << 1) | bit;
                    if (matchBit != bit) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | rc.Bit(probs[sym]);
            out[pos++] = (uint8_t)sym;
            state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
        } else {
            unsigned len;
            if (rc.Bit(isRep[state]) != 0) {
                if (pos == 0) return false;
                if (rc.Bit(isRepG0[state]) == 0) {
                    if (rc.Bit(isRep0Long[state][posState]) == 0) {
                        state = state < 7 ? 9 : 11;
                        if (rep0 >= pos) return false;
                        out[pos] = out[pos - rep0 - 1]; pos++;
                        continue;
                    }
                } else {
                    uint32_t dist;
                    if (rc.Bit(isRepG1[state]) == 0) dist = rep1;
                    else {
                        if (rc.Bit(isRepG2[state]) == 0) dist = rep2;
                        else { dist = rep3; rep3 = rep2; }
                        rep2 = rep1;
                    }
                    rep1 = rep0; rep0 = dist;
                }
                len = repLenDec.Decode(rc, posState);
                state = state < 7 ? 8 : 11;
            } else {
                rep3 = rep2; rep2 = rep1; rep1 = rep0;
                len = lenDec.Decode(rc, posState);
                state = state < 7 ? 7 : 10;
                unsigned lenState = len < 4 ? len : 3;
                unsigned slot = BitTree(rc, posSlot[lenState], 6);
                if (slot < 4) rep0 = slot;
                else {
                    unsigned numDirect = (slot >> 1) - 1;
                    uint32_t dist = (2 | (slot & 1)) << numDirect;
                    if (slot < 14) {
                        // reverse bit tree over posDecoders starting at dist - slot
                        uint16_t* probs = posDecoders + dist - slot;
                        unsigned m = 1, sym = 0;
                        for (unsigned i = 0; i < numDirect; i++) { unsigned b = rc.Bit(probs[m]); m = (m << 1) + b; sym |= b << i; }
                        dist += sym;
                    } else {
                        dist += rc.Direct((int)numDirect - 4) << 4;
                        dist += BitTreeReverse(rc, align, 4);
                    }
                    rep0 = dist;
                    if (rep0 == 0xFFFFFFFFu) break;   // end marker
                }
            }
            len += 2;
            if (rep0 >= pos) return false;
            if (len > outSize - pos) len = (unsigned)(outSize - pos);
            for (unsigned i = 0; i < len; i++) { out[pos] = out[pos - rep0 - 1]; pos++; }
        }
        if (rc.bad) return false;
    }
    return pos == outSize;
}

namespace {

struct Reader {
    const uint8_t* p; const uint8_t* end; bool bad = false;
    uint8_t U8() { if (p >= end) { bad = true; return 0; } return *p++; }
    uint64_t Number() {   // 7z variable length number
        uint8_t first = U8(), mask = 0x80; uint64_t value = 0;
        for (int i = 0; i < 8; i++) {
            if ((first & mask) == 0) { uint64_t high = first & (mask - 1u); value += high << (8 * i); return value; }
            value |= (uint64_t)U8() << (8 * i);
            mask >>= 1;
        }
        return value;
    }
    void Skip(uint64_t n) { if ((uint64_t)(end - p) < n) { bad = true; p = end; } else p += n; }
};

struct Folder { uint64_t packPos = 0, packSize = 0, unpackSize = 0; uint8_t props[5] = {0}; bool lzma = false; };

// Reads PackInfo + UnpackInfo of a StreamsInfo; leaves the reader after the StreamsInfo's end marker only as far as needed.
bool ReadStreams(Reader& r, Folder& f) {
    for (;;) {
        uint8_t id = r.U8();
        if (r.bad) return false;
        if (id == 0x00) return f.lzma;
        if (id == 0x06) {   // PackInfo
            f.packPos = r.Number(); uint64_t n = r.Number();
            for (;;) {
                uint8_t t = r.U8();
                if (t == 0x00) break;
                if (t == 0x09) { for (uint64_t i = 0; i < n; i++) { uint64_t s = r.Number(); if (i == 0) f.packSize = s; } }
                else if (t == 0x0A) { if (r.U8()) r.Skip(4 * n); else return false; }
                else return false;
            }
        } else if (id == 0x07) {   // UnpackInfo
            if (r.U8() != 0x0B) return false;
            uint64_t numFolders = r.Number();
            if (r.U8() != 0 || numFolders < 1) return false;
            uint64_t numCoders = r.Number();
            if (numCoders != 1) return false;
            uint8_t flag = r.U8();
            unsigned idSize = flag & 0x0F;
            uint8_t coder[8] = {0};
            for (unsigned i = 0; i < idSize && i < 8; i++) coder[i] = r.U8();
            if (flag & 0x10) return false;   // complex coder
            uint64_t propSize = 0;
            if (flag & 0x20) propSize = r.Number();
            if (idSize == 3 && coder[0] == 3 && coder[1] == 1 && coder[2] == 1 && propSize == 5) {
                for (int i = 0; i < 5; i++) f.props[i] = r.U8();
                f.lzma = true;
            } else return false;
            if (r.U8() != 0x0C) return false;
            for (uint64_t i = 0; i < numFolders; i++) { uint64_t s = r.Number(); if (i == 0) f.unpackSize = s; }
            return !r.bad;   // the rest (CRCs, substreams, files) is not needed for a single file
        } else return false;
    }
}

}  // namespace

bool SevenZipExtractFirst(const std::vector<uint8_t>& a, std::vector<uint8_t>& out) {
    static const uint8_t sig[6] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
    if (a.size() < 32 || std::memcmp(a.data(), sig, 6) != 0) return false;
    auto u64 = [&](size_t o) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | a[o + i]; return v; };
    uint64_t hOff = u64(12), hSize = u64(20);
    if (32 + hOff + hSize > a.size() || hSize == 0) return false;
    std::vector<uint8_t> header(a.begin() + 32 + hOff, a.begin() + 32 + hOff + hSize);
    for (int round = 0; round < 2; round++) {
        Reader r{header.data(), header.data() + header.size()};
        uint8_t id = r.U8();
        if (id == 0x17) {   // the header is itself packed: a StreamsInfo describes where
            Folder f;
            if (!ReadStreams(r, f) || 32 + f.packPos + f.packSize > a.size()) return false;
            std::vector<uint8_t> unpacked((size_t)f.unpackSize);
            if (!LzmaDecode(f.props, a.data() + 32 + f.packPos, (size_t)f.packSize, unpacked.data(), unpacked.size())) return false;
            header.swap(unpacked);
            continue;
        }
        if (id != 0x01) return false;
        if (r.U8() != 0x04) return false;   // MainStreamsInfo
        Folder f;
        if (!ReadStreams(r, f) || 32 + f.packPos + f.packSize > a.size()) return false;
        out.assign((size_t)f.unpackSize, 0);
        return LzmaDecode(f.props, a.data() + 32 + f.packPos, (size_t)f.packSize, out.data(), out.size());
    }
    return false;
}

}  // namespace ab
