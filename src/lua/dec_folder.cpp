#include "lua/dec_folder.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "lua/aes.h"
extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctime>
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

bool FileExists(const std::string& p) { std::ifstream f(p.c_str(), std::ios::binary); return (bool)f; }

// Runs a shell command and waits at most `seconds` for it (0: as long as it takes). Returns the exit code, or -1 when the time ran out and the command (and
// everything it started) was killed. v3.3: unluac 1.2.2 never finishes on some scripts (Seasons 4.1.0's scripts/menu_flow.lua is one: it loops for ever on a
// loop it cannot rebuild), and the run waited for it for ever, so the second decompiler (unluac-alt, which does finish) never got its turn.
int RunWithTimeout(const std::string& cmd, int seconds) {
#ifdef _WIN32
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;       // closing the job kills java too, not only cmd.exe
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
    std::string line = "cmd.exe /c " + cmd;
    std::vector<char> buf(line.begin(), line.end());
    buf.push_back(0);
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { if (job) CloseHandle(job); return std::system(cmd.c_str()); }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    DWORD wait = WaitForSingleObject(pi.hProcess, seconds > 0 ? (DWORD)seconds * 1000 : INFINITE);
    int rc = -1;
    if (wait == WAIT_OBJECT_0) { DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); rc = (int)code; }
    else if (job) TerminateJobObject(job, 1); else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);
    return rc;
#else
    pid_t pid = fork();
    if (pid < 0) return std::system(cmd.c_str());
    if (pid == 0) { setsid(); execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr); _exit(127); }
    const time_t start = time(nullptr);
    for (;;) {
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        if (r < 0) return 1;
        if (seconds > 0 && time(nullptr) - start >= seconds) { kill(-pid, SIGKILL); waitpid(pid, &status, 0); return -1; }
        usleep(50000);
    }
#endif
}

// `java -jar unluac.jar in > out`: true if the output is not empty. AB_DEC_TIMEOUT=<seconds> changes how long one try may take (default 90, 0: no limit);
// a script that a decompiler cannot finish in that time is given up on and the next decompiler tries it.
bool Decompile(const std::string& jar, const std::string& in, const std::string& out, const std::string& err) {
    // -Xmx / -Xss: the big scripts need more memory and a deeper stack than Java gives by default
    std::string cmd = "java -Xmx1g -Xss64m -XX:+ExitOnOutOfMemoryError -jar \"" + jar + "\" \"" + in + "\" > \"" + out + "\" 2> \"" + err + "\"";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";                                    // cmd.exe /c strips the outer pair of quotes
    for (char& c : cmd) if (c == '/') c = '\\';
#endif
    int seconds = 90;
    if (const char* t = std::getenv("AB_DEC_TIMEOUT")) seconds = std::atoi(t);
    const int rc = RunWithTimeout(cmd, seconds);
    if (rc == -1) { std::printf("dec: %s gave up after %d s on %s\n", jar.c_str(), seconds, in.c_str()); std::fflush(stdout); }
    std::ifstream o(out.c_str(), std::ios::binary | std::ios::ate);
    return rc == 0 && o && o.tellg() > 0;
}

// the decompiled text only counts if Lua 5.1 (the engine's Lua) can load it: unluac writes goto for control flow it cannot rebuild
bool LoadsAsLua51(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    lua_State* L = luaL_newstate();
    bool ok = luaL_loadbuffer(L, text.data(), text.size(), "dec") == 0;
    lua_close(L);
    return ok;
}

void Walk(const std::string& src, const std::string& dst, const std::vector<std::string>& jars, DecStats& stats) {
    for (const std::string& n : List(src)) {
        if (n.back() == '/') { Walk(src + "/" + n.substr(0, n.size() - 1), dst + "/" + n.substr(0, n.size() - 1), jars, stats); continue; }
        if (!EndsWith(n, ".lua")) continue;
        std::vector<uint8_t> bytes;
        if (!ReadLuaFile(src + "/" + n, bytes) || bytes.empty()) continue;
        MakePath(dst);
        std::string target = dst + "/" + n;
        if (bytes[0] != 0x1b) {                                  // plain text already (Classic 1.6.2 is encrypted text)
            // ReadLuaFile hands back the file unchanged when no known key opens it: that is still encrypted, not text
            bool text = true;
            for (size_t i = 0; i < bytes.size() && i < 64; i++)
                if (bytes[i] < 0x20 && bytes[i] != '\t' && bytes[i] != '\n' && bytes[i] != '\r') text = false;
            if (!text) { stats.failed++; stats.failedFiles.push_back(src + "/" + n); continue; }
            std::ofstream out(target.c_str(), std::ios::binary);
            out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
            stats.written++;
            continue;
        }
        // compiled Lua 5.1: write the bytecode beside the target for unluac, decompile to the target, remove the bytecode
        std::string tmp = target + ".luac", err = target + ".err";
        bool ok = false;
        if (!jars.empty()) {
            { std::ofstream t(tmp.c_str(), std::ios::binary); t.write((const char*)bytes.data(), (std::streamsize)bytes.size()); }
            std::printf("dec: decompiling %s ...\n", (src + "/" + n).c_str());
            std::fflush(stdout);
            for (size_t j = 0; j < jars.size() && !ok; j++) ok = Decompile(jars[j], tmp, target, err) && LoadsAsLua51(target);
            if (!std::getenv("AB_DEC_KEEP")) std::remove(tmp.c_str());     // AB_DEC_KEEP=1 leaves the .luac files (the compiled scripts) beside the text
        }
        std::remove(err.c_str());
        if (ok) { stats.written++; stats.fromBytecode++; }
        else { std::remove(target.c_str()); stats.failed++; stats.failedFiles.push_back(src + "/" + n); }
    }
}

}  // namespace

std::vector<std::string> ListFolder(const std::string& dir) { return List(Slashes(dir)); }

std::vector<std::string> FindUnluac(const std::string& exeFolder) {
    std::vector<std::string> candidates, found;
    if (const char* e = std::getenv("AB_UNLUAC")) {
        candidates.push_back(e);
        std::string dir = e;                                             // v3.3: the alternative decompiler beside it is the next one to try
        const size_t cut = dir.find_last_of("/\\");
        candidates.push_back((cut == std::string::npos ? std::string() : dir.substr(0, cut + 1)) + "unluac-alt.jar");
    }
    for (const char* name : {"tools/unluac.jar", "unluac.jar", "tools/unluac-alt.jar", "unluac-alt.jar"}) { candidates.push_back(exeFolder + name); candidates.push_back(name); }
    for (const std::string& c : candidates) {
        if (!FileExists(c)) continue;
        bool dup = false;
        for (const std::string& f : found) dup = dup || f == c;
        if (!dup) found.push_back(c);
    }
    return found;
}

int DecryptTree(const std::string& dataRoot, const std::string& decRoot, const std::vector<std::string>& unluacJars, DecStats& stats) {
    Walk(Slashes(dataRoot), Slashes(decRoot), unluacJars, stats);
    return stats.written;
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
