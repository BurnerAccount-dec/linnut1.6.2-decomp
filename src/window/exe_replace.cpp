#include "window/exe_replace.h"

#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <cstdio>
#include <cstring>
#include <vector>

namespace ab {

namespace {

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

bool WriteAll(const std::string& path, const std::vector<unsigned char>& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

// replaces `to` with the file `from`: written beside it first, so a failure leaves `to` as it was
bool ReplaceFile(const std::string& from, const std::string& to) {
    std::vector<unsigned char> data;
    if (!ReadAll(from, data)) return false;
    const std::string tmp = to + ".new";
    if (!WriteAll(tmp, data)) { std::remove(tmp.c_str()); return false; }
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) { std::remove(tmp.c_str()); return false; }
#else
    chmod(tmp.c_str(), 0755);
    if (std::rename(tmp.c_str(), to.c_str()) != 0) { std::remove(tmp.c_str()); return false; }
#endif
    return true;
}

bool HasExeExtension(const std::string& p) {
    if (p.size() < 5) return false;
    const char* e = p.c_str() + p.size() - 4;
    return (e[0] == '.') && (e[1] | 32) == 'e' && (e[2] | 32) == 'x' && (e[3] | 32) == 'e';
}

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

static int Restore(const std::string& target, const std::string& backup, bool confirm) {
    if (!Ask("Put the original back?\n\n" + target + "\n\nThe copy saved before the replacement is " + backup + ".", confirm)) return 1;
    if (!ReplaceFile(backup, target)) {
        Say(true, "Could not write " + target + ". Is the game still running? Close it and try again.");
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

int ReplaceOrRestoreExe(const std::string& target, bool confirm) {
    const std::string self = SelfExePath();
    if (self.empty()) { Say(true, "Cannot tell where this exe is, so it cannot copy itself."); return 2; }
    std::vector<unsigned char> targetData, selfData;
    if (!HasExeExtension(target)) { Say(true, target + " is not an .exe."); return 2; }
    if (!ReadAll(target, targetData)) { Say(true, "Cannot read " + target + "."); return 2; }
    if (SameFile(target, self)) { Say(true, "That is this program itself. Drag the exe you want to replace onto it, not the engine."); return 1; }
    if (!ReadAll(self, selfData)) { Say(true, "Cannot read this exe (" + self + ")."); return 2; }
    const std::string backup = target + ".original";
    std::vector<unsigned char> backupData;
    const bool isEngine = targetData == selfData || (targetData.size() == selfData.size() && std::memcmp(targetData.data(), selfData.data(), selfData.size()) == 0);
    if (isEngine) {
        if (!ReadAll(backup, backupData)) { Say(false, target + " already is the engine, and no saved original was found next to it, so there is nothing to put back."); return 1; }
        return Restore(target, backup, confirm);    // dropping it a second time undoes the replacement
    }
    if (!Ask("Replace this exe with the engine?\n\n" + target + "\n\nThe original is kept as\n" + backup + "\nand dropping the exe on the engine again puts it back.\n\nThe engine then starts from this exe's folder, the way the original did.", confirm)) return 1;
    if (!WriteAll(backup, targetData)) { Say(true, "Cannot save the original as " + backup + ", so nothing was changed."); return 2; }
    if (!ReplaceFile(self, target)) {
        std::remove(backup.c_str());
        Say(true, "Could not write " + target + ". Is the game still running? Close it and try again. Nothing was changed.");
        return 2;
    }
    Say(false, "Done: " + target + " is now the engine.\nThe original is saved as " + backup + ".\nDrop " + target + " on the engine again to put it back.");
    return 0;
}

}  // namespace ab
