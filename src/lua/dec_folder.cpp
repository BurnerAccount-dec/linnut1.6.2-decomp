#include "lua/dec_folder.h"

#include <cstdio>
#include <fstream>
#include <iterator>

#include "lua/aes.h"

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace ab {

namespace {

std::string g_dataRoot, g_decRoot;

std::string Slashes(std::string s) {
    for (char& c : s) if (c == '\\') c = '/';
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

// names of the entries of a folder, folders with a trailing '/'
std::vector<std::string> List(const std::string& dir) {
    std::vector<std::string> names;
#ifdef _WIN32
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA((dir + "/*").c_str(), &f);
    if (h == INVALID_HANDLE_VALUE) return names;
    do {
        std::string n = f.cFileName;
        if (n == "." || n == "..") continue;
        names.push_back((f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? n + "/" : n);
    } while (FindNextFileA(h, &f));
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return names;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        struct stat st;
        bool isDir = stat((dir + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        names.push_back(isDir ? n + "/" : n);
    }
    closedir(d);
#endif
    return names;
}

bool EndsWith(const std::string& s, const char* tail) {
    size_t n = std::char_traits<char>::length(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

void MakeDirectory(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

void MakePath(const std::string& dir) {          // every folder of the path
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/') MakeDirectory(dir.substr(0, i));
}

void Walk(const std::string& src, const std::string& dst, int& count, int& text) {
    for (const std::string& n : List(src)) {
        if (n.back() == '/') { Walk(src + "/" + n.substr(0, n.size() - 1), dst + "/" + n.substr(0, n.size() - 1), count, text); continue; }
        if (!EndsWith(n, ".lua")) continue;
        std::vector<uint8_t> bytes;
        if (!ReadLuaFile(src + "/" + n, bytes) || bytes.empty()) continue;
        MakePath(dst);
        std::ofstream out((dst + "/" + n).c_str(), std::ios::binary);
        out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        count++;
        if (bytes[0] != 0x1b) text++;
    }
}

}  // namespace

int DecryptTree(const std::string& dataRoot, const std::string& decRoot, int& textFiles) {
    int count = 0;
    textFiles = 0;
    Walk(Slashes(dataRoot), Slashes(decRoot), count, textFiles);
    return count;
}

void UseDecFolder(const std::string& dataRoot, const std::string& decRoot) {
    g_dataRoot = Slashes(dataRoot);
    g_decRoot = Slashes(decRoot);
}

bool ReadDecOverride(const std::string& path, std::vector<uint8_t>& bytes) {
    if (g_dataRoot.empty()) return false;
    std::string p = Slashes(path);
    if (p.size() <= g_dataRoot.size() + 1 || p.compare(0, g_dataRoot.size(), g_dataRoot) != 0 || p[g_dataRoot.size()] != '/') return false;
    std::ifstream f((g_decRoot + p.substr(g_dataRoot.size())).c_str(), std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !bytes.empty();
}

}  // namespace ab
