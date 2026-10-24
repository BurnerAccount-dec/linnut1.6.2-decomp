#include "engine/png_decoder.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ab {
namespace {

// ---- inflate (RFC 1951), the slow simple way: bit by bit canonical Huffman decoding
struct BitReader {
    const uint8_t* p;
    size_t size, pos = 0;
    uint32_t bits = 0;
    int count = 0;
    bool error = false;
    int Bit() {
        if (count == 0) {
            if (pos >= size) { error = true; return 0; }
            bits = p[pos++];
            count = 8;
        }
        int b = bits & 1;
        bits >>= 1;
        count--;
        return b;
    }
    uint32_t Bits(int n) {            // least significant bit first
        uint32_t v = 0;
        for (int i = 0; i < n; i++) v |= (uint32_t)Bit() << i;
        return v;
    }
};

struct Huffman {
    uint16_t counts[16];
    uint16_t symbols[320];
    void Build(const uint8_t* lengths, int n) {
        std::memset(counts, 0, sizeof counts);
        for (int i = 0; i < n; i++) counts[lengths[i]]++;
        counts[0] = 0;
        uint16_t offsets[16];
        offsets[1] = 0;
        for (int i = 1; i < 15; i++) offsets[i + 1] = offsets[i] + counts[i];
        for (int i = 0; i < n; i++)
            if (lengths[i]) symbols[offsets[lengths[i]]++] = (uint16_t)i;
    }
    int Decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; len++) {
            code |= br.Bit();
            int count = counts[len];
            if (code - count < first) return symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
            if (br.error) return -1;
        }
        return -1;
    }
};

const uint16_t kLenBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const uint16_t kLenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const uint16_t kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const uint16_t kDistExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

bool InflateBlock(BitReader& br, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist) {
    for (;;) {
        int sym = lit.Decode(br);
        if (sym < 0 || br.error) return false;
        if (sym < 256) {
            out.push_back((uint8_t)sym);
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            int len = kLenBase[sym] + (int)br.Bits(kLenExtra[sym]);
            int ds = dist.Decode(br);
            if (ds < 0 || ds >= 30) return false;
            size_t d = kDistBase[ds] + br.Bits(kDistExtra[ds]);
            if (d > out.size()) return false;
            size_t from = out.size() - d;
            for (int i = 0; i < len; i++) out.push_back(out[from + i]);
        }
    }
}

bool Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    BitReader br{data, size};
    int last;
    do {
        last = br.Bit();
        int type = (int)br.Bits(2);
        if (br.error) return false;
        if (type == 0) {
            br.count = 0;                                   // stored block: skip to a byte boundary
            if (br.pos + 4 > size) return false;
            unsigned len = data[br.pos] | (data[br.pos + 1] << 8);
            br.pos += 4;
            if (br.pos + len > size) return false;
            out.insert(out.end(), data + br.pos, data + br.pos + len);
            br.pos += len;
        } else if (type == 1) {
            uint8_t l[288];
            for (int i = 0; i < 144; i++) l[i] = 8;
            for (int i = 144; i < 256; i++) l[i] = 9;
            for (int i = 256; i < 280; i++) l[i] = 7;
            for (int i = 280; i < 288; i++) l[i] = 8;
            Huffman lit, dist;
            lit.Build(l, 288);
            uint8_t d[30];
            for (int i = 0; i < 30; i++) d[i] = 5;
            dist.Build(d, 30);
            if (!InflateBlock(br, out, lit, dist)) return false;
        } else if (type == 2) {
            int nlen = (int)br.Bits(5) + 257, ndist = (int)br.Bits(5) + 1, ncode = (int)br.Bits(4) + 4;
            static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t lengths[320] = {0};
            for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)br.Bits(3);
            Huffman codeLen;
            codeLen.Build(lengths, 19);
            uint8_t all[320] = {0};
            int idx = 0;
            while (idx < nlen + ndist) {
                int sym = codeLen.Decode(br);
                if (sym < 0) return false;
                if (sym < 16) {
                    all[idx++] = (uint8_t)sym;
                } else {
                    int rep, val = 0;
                    if (sym == 16) { if (idx == 0) return false; val = all[idx - 1]; rep = 3 + (int)br.Bits(2); }
                    else if (sym == 17) rep = 3 + (int)br.Bits(3);
                    else rep = 11 + (int)br.Bits(7);
                    if (idx + rep > nlen + ndist) return false;
                    while (rep--) all[idx++] = (uint8_t)val;
                }
            }
            Huffman lit, dist;
            lit.Build(all, nlen);
            dist.Build(all + nlen, ndist);
            if (!InflateBlock(br, out, lit, dist)) return false;
        } else {
            return false;
        }
    } while (!last);
    return true;
}

uint32_t Be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

int Paeth(int a, int b, int c) {
    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

}  // namespace

bool DecodePng(const std::vector<uint8_t>& f, Image& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (f.size() < 33 || std::memcmp(f.data(), sig, 8) != 0) return fail("not a PNG file");
    int width = 0, height = 0, depth = 0, colorType = 0, interlace = 0;
    std::vector<uint8_t> idat, palette, trns;
    size_t pos = 8;
    while (pos + 12 <= f.size()) {
        uint32_t len = Be32(&f[pos]);
        const uint8_t* type = &f[pos + 4];
        const uint8_t* data = &f[pos + 8];
        if (pos + 12 + len > f.size()) return fail("chunk runs past the end");
        if (!std::memcmp(type, "IHDR", 4)) {
            width = (int)Be32(data); height = (int)Be32(data + 4);
            depth = data[8]; colorType = data[9]; interlace = data[12];
        } else if (!std::memcmp(type, "PLTE", 4)) {
            palette.assign(data, data + len);
        } else if (!std::memcmp(type, "tRNS", 4)) {
            trns.assign(data, data + len);
        } else if (!std::memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), data, data + len);
        } else if (!std::memcmp(type, "IEND", 4)) {
            break;
        }
        pos += 12 + len;
    }
    if (width <= 0 || height <= 0) return fail("no IHDR");
    if (depth != 8) return fail("only 8-bit PNG files are supported");
    if (interlace != 0) return fail("interlaced PNG files are not supported");
    int channels;
    switch (colorType) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: return fail("unknown colour type");
    }
    if (idat.size() < 6) return fail("no image data");
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(width * channels + 1) * height);
    if (!Inflate(idat.data() + 2, idat.size() - 2, raw)) return fail("bad compressed data");   // 2 bytes of zlib header
    size_t stride = (size_t)width * channels;
    if (raw.size() < (stride + 1) * height) return fail("image data too short");

    // undo the row filters in place
    std::vector<uint8_t> pix(stride * height);
    for (int y = 0; y < height; y++) {
        int filter = raw[y * (stride + 1)];
        const uint8_t* src = &raw[y * (stride + 1) + 1];
        uint8_t* dst = &pix[y * stride];
        const uint8_t* up = y ? &pix[(y - 1) * stride] : nullptr;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)channels ? dst[i - channels] : 0;
            int b = up ? up[i] : 0;
            int c = (up && i >= (size_t)channels) ? up[i - channels] : 0;
            int v = src[i];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += Paeth(a, b, c); break;
                default: return fail("bad row filter");
            }
            dst[i] = (uint8_t)v;
        }
    }

    out.width = width;
    out.height = height;
    out.rgba.assign((size_t)width * height * 4, 255);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t* s = &pix[y * stride + (size_t)x * channels];
            uint8_t* d = &out.rgba[((size_t)y * width + x) * 4];
            switch (colorType) {
                case 0: d[0] = d[1] = d[2] = s[0]; break;
                case 2: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; break;
                case 3: {
                    size_t i = s[0];
                    if (i * 3 + 2 >= palette.size()) return fail("palette index out of range");
                    d[0] = palette[i * 3]; d[1] = palette[i * 3 + 1]; d[2] = palette[i * 3 + 2];
                    d[3] = i < trns.size() ? trns[i] : 255;
                    break;
                }
                case 4: d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; break;
                case 6: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; break;
            }
        }
    }
    return true;
}

bool LoadPngFile(const std::string& path, Image& out, std::string* error) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::vector<uint8_t> bytes;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    fclose(f);
    return DecodePng(bytes, out, error);
}


// An uncompressed PNG (stored deflate blocks): big files, but no compressor is needed. For looking at frames.
bool WritePngFile(const char* path, int width_, int height_, const uint8_t* pixels) {
    // an uncompressed PNG: stored deflate blocks, so no compressor is needed
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    auto be32 = [](uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; };
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        made = true;
    }
    auto crc = [&](const uint8_t* d, size_t n, uint32_t c) {
        c ^= 0xffffffffu;
        for (size_t i = 0; i < n; i++) c = table[(c ^ d[i]) & 0xff] ^ (c >> 8);
        return c ^ 0xffffffffu;
    };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        uint8_t len[4];
        be32(len, (uint32_t)data.size());
        fwrite(len, 1, 4, f);
        std::vector<uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        fwrite(body.data(), 1, body.size(), f);
        uint8_t c[4];
        be32(c, crc(body.data(), body.size(), 0));
        fwrite(c, 1, 4, f);
    };
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr(13);
    be32(&ihdr[0], width_); be32(&ihdr[4], height_);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk("IHDR", ihdr);
    std::vector<uint8_t> raw;
    for (int y = 0; y < height_; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), pixels + (size_t)y * width_ * 4, pixels + (size_t)(y + 1) * width_ * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(n & 0xff); z.push_back(n >> 8); z.push_back(~n & 0xff); z.push_back((~n >> 8) & 0xff);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint8_t ad[4];
    be32(ad, (b << 16) | a);
    z.insert(z.end(), ad, ad + 4);
    chunk("IDAT", z);
    chunk("IEND", {});
    fclose(f);
    return true;
}

}  // namespace ab
