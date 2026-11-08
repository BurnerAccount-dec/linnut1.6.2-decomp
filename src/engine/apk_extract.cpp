#include "engine/apk_extract.h"

#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <direct.h>
#define AB_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define AB_MKDIR(p) mkdir(p, 0755)
#endif

namespace ab {
namespace {

struct Entry { std::string name; uint32_t method, csize, usize, localOffset; };

uint32_t U16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool ReadAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

void MakeDirs(const std::string& path) {          // every folder of a file path (the last part is the file name)
    for (size_t i = 1; i < path.size(); i++)
        if (path[i] == '/' || path[i] == '\\') AB_MKDIR(path.substr(0, i).c_str());
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    MakeDirs(path);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok;
}

bool ListZip(const std::vector<uint8_t>& z, std::vector<Entry>& out) {
    if (z.size() < 22) return false;
    size_t eocd = std::string::npos;
    for (size_t i = z.size() - 22; ; i--) {
        if (U32(&z[i]) == 0x06054b50) { eocd = i; break; }
        if (i == 0 || z.size() - i > 70000) break;
    }
    if (eocd == std::string::npos) return false;
    uint32_t count = U16(&z[eocd + 10]), at = U32(&z[eocd + 16]);
    for (uint32_t k = 0; k < count; k++) {
        if ((size_t)at + 46 > z.size() || U32(&z[at]) != 0x02014b50) return false;
        Entry e;
        e.method = U16(&z[at + 10]); e.csize = U32(&z[at + 20]); e.usize = U32(&z[at + 24]); e.localOffset = U32(&z[at + 42]);
        uint32_t nl = U16(&z[at + 28]), xl = U16(&z[at + 30]), cl = U16(&z[at + 32]);
        if ((size_t)at + 46 + nl > z.size()) return false;
        e.name.assign((const char*)&z[at + 46], nl);
        out.push_back(e);
        at += 46 + nl + xl + cl;
    }
    return true;
}

bool Unpack(const std::vector<uint8_t>& z, const Entry& e, std::vector<uint8_t>& out) {
    if ((size_t)e.localOffset + 30 > z.size() || U32(&z[e.localOffset]) != 0x04034b50) return false;
    size_t start = e.localOffset + 30 + U16(&z[e.localOffset + 26]) + U16(&z[e.localOffset + 28]);
    if (start + e.csize > z.size()) return false;
    out.clear();
    if (e.method == 0) { out.assign(&z[start], &z[start] + e.csize); return true; }
    if (e.method == 8) return Inflate(&z[start], e.csize, out);
    return false;
}

bool EndsWith(const std::string& s, const char* t) {
    size_t n = std::strlen(t);
    return s.size() >= n && s.compare(s.size() - n, n, t) == 0;
}

}  // namespace

bool ExtractApk(const std::string& apkPath, const std::string& outDir, std::string* error) {
    const std::string marker = outDir + "/.extracted";
    { std::vector<uint8_t> m; if (ReadAll(marker, m)) return true; }
    std::vector<uint8_t> zip;
    if (!ReadAll(apkPath, zip)) { if (error) *error = "cannot open " + apkPath; return false; }
    std::vector<Entry> entries;
    if (!ListZip(zip, entries)) { if (error) *error = apkPath + " is not a readable zip (.apk) file"; return false; }
    // an Android .apk keeps the data in assets/data; an iOS .ipa in Payload/<name>.app/data_iphone (or data_ipad)
    std::string prefix = "assets/data/";
    for (const Entry& e : entries) {
        if (e.name.compare(0, 8, "Payload/") != 0) continue;
        size_t app = e.name.find(".app/", 8);
        if (app == std::string::npos) continue;
        size_t d = app + 5;
        if (e.name.compare(d, 5, "data_") == 0) { size_t slash = e.name.find('/', d); if (slash != std::string::npos) { prefix = e.name.substr(0, slash + 1); break; } }
    }
    int files = 0;
    for (const Entry& e : entries) {
        if (e.name.compare(0, prefix.size(), prefix) != 0 || e.name.back() == '/') continue;
        std::vector<uint8_t> bytes;
        if (!Unpack(zip, e, bytes)) { if (error) *error = "cannot unpack " + e.name; return false; }
        std::string rel = "data/" + e.name.substr(prefix.size());
        if (EndsWith(rel, ".pvr.zip")) {                          // a texture kept in its own zip: the .pvr inside is what the game opens
            std::vector<Entry> inner;
            if (ListZip(bytes, inner) && !inner.empty()) {
                std::vector<uint8_t> pvr;
                if (Unpack(bytes, inner[0], pvr)) {
                    if (WriteFile(outDir + "/" + rel.substr(0, rel.size() - 4), pvr)) files++;
                    continue;
                }
            }
        }
        if (!WriteFile(outDir + "/" + rel, bytes)) { if (error) *error = "cannot write " + outDir + "/" + rel; return false; }
        files++;
    }
    if (files == 0) { if (error) *error = apkPath + " has no assets/data (.apk) or Payload/*.app/data_* (.ipa) folder (not an Angry Birds app?)"; return false; }
    std::string note = "extracted from " + apkPath + "\n";
    WriteFile(marker, std::vector<uint8_t>(note.begin(), note.end()));
    return true;
}

}  // namespace ab
