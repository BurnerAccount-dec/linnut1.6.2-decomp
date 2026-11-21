#include "window/exe_replace.h"

#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ab {

// Present in every engine exe and in no retail exe: how a replaced exe is told from an original (a replaced exe is no longer byte for byte the engine, it carries the
// original's icons).
extern const char kEngineSignature[];
const char kEngineSignature[] = "AB-ENGINE-EXE-SIGNATURE-6d1f0c52";

namespace {

bool ReadAll(const std::string& path, std::vector<unsigned char>& out);

bool ContainsSignature(const std::vector<unsigned char>& d) {
    const size_t n = sizeof kEngineSignature - 1;
    if (d.size() < n) return false;
    for (size_t i = 0; i + n <= d.size(); i++)
        if (d[i] == (unsigned char)kEngineSignature[0] && std::memcmp(&d[i], kEngineSignature, n) == 0) return true;
    return false;
}

#ifdef _WIN32
// The icons (RT_GROUP_ICON, RT_ICON) and the version information of `from` are written into `to` (a copy of the engine that is not running).
struct ResCopy { HANDLE update; HMODULE module; bool any; };
BOOL CALLBACK CopyResName(HMODULE m, LPCSTR type, LPSTR name, LONG_PTR param) {
    ResCopy* rc = (ResCopy*)param;
    HRSRC r = FindResourceA(m, name, type);
    if (!r) return TRUE;
    HGLOBAL g = LoadResource(m, r);
    void* data = g ? LockResource(g) : nullptr;
    if (!data) return TRUE;
    if (UpdateResourceA(rc->update, type, name, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), data, SizeofResource(m, r))) rc->any = true;
    return TRUE;
}
BOOL CALLBACK CopyResType(HMODULE m, LPSTR type, LONG_PTR param) {
    // 3 = RT_ICON, 14 = RT_GROUP_ICON, 16 = RT_VERSION
    if (type == MAKEINTRESOURCEA(3) || type == MAKEINTRESOURCEA(14) || type == MAKEINTRESOURCEA(16)) EnumResourceNamesA(m, type, CopyResName, param);
    return TRUE;
}
bool CopyIdentity(const std::string& from, const std::string& to) {
    HMODULE m = LoadLibraryExA(from.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!m) return false;
    HANDLE u = BeginUpdateResourceA(to.c_str(), FALSE);
    if (!u) { FreeLibrary(m); return false; }
    ResCopy rc = {u, m, false};
    EnumResourceTypesA(m, CopyResType, (LONG_PTR)&rc);
    const bool ok = EndUpdateResourceA(u, rc.any ? FALSE : TRUE) != 0;
    FreeLibrary(m);
    return ok && rc.any;
}
#else
bool CopyIdentity(const std::string&, const std::string&) { return false; }
#endif

// config.lua beside the exe: the lines for `key` are replaced by `key = value`
void SetConfigBool(const std::string& exePath, const char* key, bool value) {
    size_t cut = exePath.find_last_of("/\\");
    const std::string path = (cut == std::string::npos ? std::string() : exePath.substr(0, cut + 1)) + "config.lua";
    std::vector<unsigned char> raw;
    std::string text;
    if (ReadAll(path, raw)) text.assign(raw.begin(), raw.end());
    std::string out, line;
    const std::string k = key;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i == text.size() || text[i] == '\n') {
            size_t b = line.find_first_not_of(" \t");
            const bool mine = b != std::string::npos && line.compare(b, k.size(), k) == 0 && line.find_first_not_of(" \t", b + k.size()) != std::string::npos && line[line.find_first_not_of(" \t", b + k.size())] == '=';
            if (!mine && !(i == text.size() && line.empty())) out += line + "\n";
            line.clear();
        } else if (text[i] != '\r') line += text[i];
    }
    out += k + " = " + (value ? "true" : "false") + "\n";
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(out.data(), 1, out.size(), f); std::fclose(f); }
}

bool ReadAll(const std::string& path, std::vector<unsigned char>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

unsigned long g_lastError = 0;       // the Windows error of the last failed write or replace (0 on other systems)

bool WriteAll(const std::string& path, const std::vector<unsigned char>& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
#ifdef _WIN32
        g_lastError = (errno == EACCES) ? 5 : GetLastError();
#endif
        return false;
    }
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

// replaces `to` with the file `from`: written beside it first, so a failure leaves `to` as it was
// replaces `to` with the file `from`: written beside it first, so a failure leaves `to` as it was
bool ReplaceFile(const std::string& from, const std::string& to, const std::string& identityFrom = std::string()) {
    std::vector<unsigned char> data;
    if (!ReadAll(from, data)) return false;
    const std::string tmp = to + ".new";
    if (!WriteAll(tmp, data)) {
#ifdef _WIN32
        g_lastError = GetLastError();
#endif
        std::remove(tmp.c_str());
        return false;
    }
    if (!identityFrom.empty()) CopyIdentity(identityFrom, tmp);      // 3.9: the original's icons and version information (a failure leaves the engine's own, which has none)
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        g_lastError = GetLastError();
        // an exe that is running cannot be overwritten, but it can be renamed: move it aside, put the new file in its place, and drop the old one now or at the next restart
        const std::string aside = to + ".old";
        std::remove(aside.c_str());
        if (MoveFileExA(to.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING) && MoveFileExA(tmp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            if (!DeleteFileA(aside.c_str())) MoveFileExA(aside.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
            return true;
        }
        g_lastError = GetLastError();
        MoveFileExA(aside.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING);   // put the original back if only half of it worked
        std::remove(tmp.c_str());
        return false;
    }
#else
    chmod(tmp.c_str(), 0755);
    if (std::rename(tmp.c_str(), to.c_str()) != 0) { std::remove(tmp.c_str()); return false; }
#endif
    return true;
}

// Windows refused access (a protected folder such as Program Files): the same job is started again with the "Run as administrator" prompt of Windows, which the user
// answers. Returns the exit code of that run, or -1 when this is not the case (already an administrator, not Windows, the prompt was declined).
int RetryElevated(const std::string& args) {
#ifdef _WIN32
    if (g_lastError != 5 || IsUserAnAdmin()) return -1;
    const std::string self = SelfExePath();
    if (self.empty()) return -1;
    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "runas";
    sei.lpFile = self.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei) || !sei.hProcess) return -1;       // declined, or no way to ask
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 2;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return (int)code;
#else
    (void)args;
    return -1;
#endif
}

// the sentence that follows "Could not write <file>." : what the error code means
std::string WhyNot() {
    if (g_lastError == 5) return " Windows refused access: the file is in a protected folder (Program Files), so run the engine as administrator (right-click, Run as administrator), or the file is read-only or held by a virus scanner.";
    if (g_lastError == 32 || g_lastError == 33) return " The file is in use: a copy of the game may still be running (look for AngryBirds.exe or AngryBirdsEngine.exe in the Task Manager) or a window left open by an earlier start.";
    if (g_lastError != 0) return " Windows error " + std::to_string(g_lastError) + ".";
    return std::string();
}

bool HasExtension(const std::string& p, const char* ext) {      // ext like ".exe", compared without regard to case
    const size_t n = std::strlen(ext);
    if (p.size() <= n) return false;
    for (size_t i = 0; i < n; ++i) if ((p[p.size() - n + i] | 32) != (ext[i] | 32)) return false;
    return true;
}
bool HasExeExtension(const std::string& p) { return HasExtension(p, ".exe"); }
bool IsPackage(const std::string& p) { return HasExtension(p, ".apk") || HasExtension(p, ".ipa"); }

bool SameFile(const std::string& a, const std::string& b) {
#ifdef _WIN32
    char fa[MAX_PATH * 2], fb[MAX_PATH * 2];
    if (!GetFullPathNameA(a.c_str(), sizeof fa, fa, nullptr) || !GetFullPathNameA(b.c_str(), sizeof fb, fb, nullptr)) return false;
    return _stricmp(fa, fb) == 0;
#else
    struct stat sa, sb;
    return stat(a.c_str(), &sa) == 0 && stat(b.c_str(), &sb) == 0 && sa.st_ino == sb.st_ino && sa.st_dev == sb.st_dev;
#endif
}

void Say(bool error, const std::string& text) {
    std::printf("%s\n", text.c_str());
    SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, "Angry Birds engine", text.c_str(), nullptr);
}

bool Ask(const std::string& text, bool confirm) {
    if (!confirm) return true;
    const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"}, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"}};
    SDL_MessageBoxData box = {SDL_MESSAGEBOX_WARNING, nullptr, "Angry Birds engine", text.c_str(), 2, buttons, nullptr};
    int pressed = 0;
    if (SDL_ShowMessageBox(&box, &pressed) != 0) return false;      // no way to ask: do nothing
    return pressed == 1;
}

}  // namespace

std::string SelfExePath() {
#ifdef _WIN32
    char buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof buf);
    return n > 0 && n < sizeof buf ? std::string(buf, n) : std::string();
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, (size_t)n) : std::string();
#endif
}

int AskPackageAction(const std::string& package) {
    const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Play it"}, {0, 1, "Replace with the engine"}, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Cancel"}};
    const std::string text = package + "\n\nPlay this package with the engine, or replace the package with the engine (the original is kept as " + package + ".original, and dropping the package on the engine again puts it back)?";
    SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, nullptr, "Angry Birds engine", text.c_str(), 3, buttons, nullptr};
    int pressed = 0;
    if (SDL_ShowMessageBox(&box, &pressed) != 0) return 0;      // no way to ask: play it, as before
    return pressed;
}

static int Restore(const std::string& target, const std::string& backup, bool confirm) {
    if (!Ask("Put the original back?\n\n" + target + "\n\nThe copy saved before the replacement is " + backup + ".", confirm)) return 1;
    if (!ReplaceFile(backup, target)) {
        const int r = RetryElevated("--restore-exe \"" + target + "\" --yes");
        if (r >= 0) return r;
        Say(true, "Could not write " + target + "." + WhyNot() + " Nothing was changed.");
        return 2;
    }
    std::remove(backup.c_str());
    Say(false, "Done: " + target + " is the original again.");
    return 0;
}

int RestoreExe(const std::string& target, bool confirm) {
    const std::string backup = target + ".original";
    std::vector<unsigned char> probe;
    if (!ReadAll(backup, probe)) { Say(true, "There is no saved original (" + backup + "), nothing to restore."); return 2; }
    return Restore(target, backup, confirm);
}

int ReplaceOrRestoreExe(const std::string& target, bool confirm, int debugTools) {
    const std::string self = SelfExePath();
    if (self.empty()) { Say(true, "Cannot tell where this exe is, so it cannot copy itself."); return 2; }
    std::vector<unsigned char> targetData, selfData;
    const bool package = IsPackage(target);          // 3.9: an .apk or .ipa takes the engine's place the same way, under its own file name
    if (!HasExeExtension(target) && !package) { Say(true, target + " is not an .exe, .apk or .ipa."); return 2; }
    if (!ReadAll(target, targetData)) { Say(true, "Cannot read " + target + "."); return 2; }
    if (SameFile(target, self)) { Say(true, "That is this program itself. Drag the exe you want to replace onto it, not the engine."); return 1; }
    if (!ReadAll(self, selfData)) { Say(true, "Cannot read this exe (" + self + ")."); return 2; }
    const std::string backup = target + ".original";
    std::vector<unsigned char> backupData;
    const bool isEngine = ContainsSignature(targetData) || targetData == selfData || (targetData.size() == selfData.size() && std::memcmp(targetData.data(), selfData.data(), selfData.size()) == 0);
    if (isEngine) {
        if (!ReadAll(backup, backupData)) { Say(false, target + " already is the engine, and no saved original was found next to it, so there is nothing to put back."); return 1; }
        return Restore(target, backup, confirm);    // dropping it a second time undoes the replacement
    }
    if (package) {
        if (!Ask("Replace this package with the engine?\n\n" + target + "\n\nThe file gets the engine's contents under its own name. The engine is a Windows program, so a phone or tablet cannot install or run it; use this for tools and setups that expect the package at this place.\n\nThe original is kept as\n" + backup + "\nand dropping the package on the engine again puts it back.", confirm)) return 1;
    } else if (!Ask("Replace this exe with the engine?\n\n" + target + "\n\nThe original is kept as\n" + backup + "\nand dropping the exe on the engine again puts it back.\n\nThe engine then starts from this exe's folder, the way the original did.", confirm)) return 1;
    if (package) debugTools = -1;
    if (debugTools < 0 && confirm && !package) {
        const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Keep them on"}, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Turn them off"}};
        SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, nullptr, "Angry Birds engine",
            "Keep the command prompt and the developers' tools?\n\nOn: the command prompt window, the debug keys and Ctrl + minus / plus (speed) work.\nOff: the game plays like the retail one.\n\nThe choice is written to config.lua beside the exe (debugTools = true / false) and can be changed there at any time.",
            2, buttons, nullptr};
        int pressed = 1;
        if (SDL_ShowMessageBox(&box, &pressed) != 0) pressed = 1;
        debugTools = pressed;
    }
    const std::string elevatedArgs = "--replace-exe \"" + target + "\" --yes" + (debugTools < 0 ? std::string() : debugTools ? " --debug-tools on" : " --debug-tools off");
    if (!WriteAll(backup, targetData)) {
        const int r = RetryElevated(elevatedArgs);       // a protected folder: Windows asks for administrator rights and the job is done there
        if (r >= 0) return r;
        Say(true, "Cannot save the original as " + backup + ", so nothing was changed." + WhyNot());
        return 2;
    }
    if (!ReplaceFile(self, target, package ? std::string() : backup)) {
        std::remove(backup.c_str());
        const int r = RetryElevated(elevatedArgs);
        if (r >= 0) return r;
        Say(true, "Could not write " + target + "." + WhyNot() + " Nothing was changed.");
        return 2;
    }
    if (debugTools >= 0 && !package) { SetConfigBool(target, "debugTools", debugTools != 0); SetConfigBool(target, "console", debugTools != 0); }
    Say(false, "Done: " + target + " is now the engine.\nThe original is saved as " + backup + ".\nDrop " + target + " on the engine again to put it back.");
    return 0;
}

}  // namespace ab
