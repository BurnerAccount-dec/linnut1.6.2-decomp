// Runs the real scripts with the software renderer and writes frames as PNG files, so the output of
// the drawing code can be looked at without a window.
// Usage: ab_render_test <scripts folder> <data folder> <output prefix> <frame> [<frame> ...]
// Every frame advances the game by 1/30 s; a PNG is written after each listed frame number.
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "engine/game_platform.h"

using namespace ab;

int main(int argc, char** argv) {
    if (argc < 5) { std::printf("usage: %s <scripts> <data> <output prefix> <frame>...\n", argv[0]); return 2; }
    BootConfig cfg;
    cfg.scriptPath = argv[1];
    cfg.imagePath = "images/pc_build";
    cfg.fontPath = "fonts/pc_build";
    GamePlatform platform(argv[2]);
    SoftwareBackend backend;
    platform.SetBackend(&backend, (int)cfg.screenWidth, (int)cfg.screenHeight);
    ScriptHost scripts(platform, cfg);
    platform.Attach(scripts);
    if (!scripts.LoadScripts()) { std::printf("LOAD FAILED: %s\n", scripts.LastError().c_str()); return 1; }
    if (!scripts.CreateStartUpAssets()) { std::printf("createStartUpAssets FAILED: %s\n", scripts.LastError().c_str()); return 1; }
    std::set<int> wanted;
    int last = 0;
    for (int i = 4; i < argc; i++) { wanted.insert(std::atoi(argv[i])); last = std::max(last, std::atoi(argv[i])); }
    for (int frame = 1; frame <= last; frame++) {
        platform.BeginFrame();
        platform.RunFrame(1.0f / 30.0f);
        platform.EndFrame();
        if (!scripts.LastError().empty()) { std::printf("frame %d: %s\n", frame, scripts.LastError().c_str()); return 1; }
        if (wanted.count(frame)) {
            char name[512];
            std::snprintf(name, sizeof name, "%s_%04d.png", argv[3], frame);
            backend.WritePng(name);
            std::printf("wrote %s\n", name);
        }
    }
    return 0;
}
