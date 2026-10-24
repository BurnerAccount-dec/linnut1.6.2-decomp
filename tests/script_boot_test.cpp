// Boots the real game scripts headless: load them, run createStartUpAssets and initialize, then
// a few update frames. Usage: ab_script_boot <scripts folder> [user folder] [frames]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "lua/script_host.h"

extern "C" {
#include "lauxlib.h"
}

using namespace ab;

struct LogPlatform : Platform {
    void Print(const std::string& text) override { std::printf("[lua] %s\n", text.c_str()); }
};

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s <scripts folder> [user folder] [frames]\n", argv[0]); return 2; }
    BootConfig cfg;
    cfg.scriptPath = argv[1];
    if (argc > 2) cfg.userPath = argv[2];
    LogPlatform platform;
    ScriptHost host(platform, cfg);
    host.TraceMissingGlobals(true);

    if (!host.LoadScripts()) { std::printf("LOAD FAILED: %s\n", host.LastError().c_str()); return 1; }
    std::printf("scripts loaded\n");
    if (!host.CreateStartUpAssets()) { std::printf("createStartUpAssets FAILED: %s\n", host.LastError().c_str()); return 1; }
    std::printf("createStartUpAssets ok\n");
    // The scripts call initialize() themselves once the splash screens are done, so just run frames.
    int frames = 3000;
    if (argc > 3) frames = std::atoi(argv[3]);
    for (int i = 0; i < frames; i++) {
        if (!host.Update(1.0f / 30.0f, 1.0f / 30.0f)) { std::printf("update %d FAILED: %s\n", i, host.LastError().c_str()); return 1; }
    }
    std::printf("%d updates ok\n", frames);

    // where did the scripts get to?
    lua_State* L = host.State();
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    const char* probes[] = {"assetsCreated", "currentGameMode", "currentMenuPage", "currentTheme"};
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        lua_getfield(L, -1, probes[i]);
        std::printf("probe %s: %s\n", probes[i], luaL_typename(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    for (size_t i = 0; i < host.MissingGlobals().size(); i++) std::printf("missing global: %s\n", host.MissingGlobals()[i].c_str());
    return 0;
}
