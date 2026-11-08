#include <fstream>
#include <cstdlib>
#include "lua/script_host.h"
#include "lua/aes.h"

#include <cstring>
#include <cstdio>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lualib.h"
}

#include "lua/lua_api.h"

namespace ab {

extern const unsigned char kEditorClassicLua[];
extern const unsigned int kEditorClassicLuaSize;
extern const unsigned char kHatcheryNativesLua[];
extern const unsigned int kHatcheryNativesLuaSize;
extern const unsigned char kSpaceNativesLua[];
extern const unsigned int kSpaceNativesLuaSize;
extern const unsigned char kSpaceEditorLua[];
extern const unsigned int kSpaceEditorLuaSize;

namespace {
// Angry Birds 1.6.3.1 ships its level editor with updateEditor emptied out (the menus, saving and every helper are still there);
// 1.6.2 has the full function. A game whose updateEditor is only a stub gets the 1.6.2 one, so the editor works in every version.
void EnsureClassicEditor(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "updateEditor");
    bool stub = false;
    if (lua_isfunction(L, -1)) {
        lua_getglobal(L, "string");
        lua_getfield(L, -1, "dump");
        lua_pushvalue(L, -3);
        if (lua_pcall(L, 1, 1, 0) == 0) stub = lua_objlen(L, -1) < 400;
        lua_pop(L, 1);                       // the dump (or the error)
        lua_pop(L, 1);                       // string
    }
    lua_pop(L, 1);                           // updateEditor
    if (stub && luaL_loadbuffer(L, (const char*)kEditorClassicLua, kEditorClassicLuaSize, "editor_classic.lua") == 0) {
        lua_pushvalue(L, -2);                // the environment
        lua_setfenv(L, -2);
        if (lua_pcall(L, 0, 0, 0) != 0) { std::fprintf(stderr, "editor: %s\n", lua_tostring(L, -1)); lua_pop(L, 1); }
        else std::fprintf(stderr, "editor: this game's updateEditor is empty, using the Angry Birds 1.6.2 editor\n");
    } else if (stub) { std::fprintf(stderr, "editor: %s\n", lua_tostring(L, -1)); lua_pop(L, 1); }
    lua_pop(L, 1);                           // the environment
}

int MissingGlobalHook(lua_State* L) {
    // _G.__index(key): remember the key, return nil
    ScriptHost* host = static_cast<ScriptHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (lua_type(L, 2) == LUA_TSTRING) {
        const std::string name = lua_tostring(L, 2);
        const std::vector<std::string>& m = host->MissingGlobals();
        bool seen = false;
        for (size_t i = 0; i < m.size(); i++) if (m[i] == name) seen = true;
        if (!seen) const_cast<std::vector<std::string>&>(m).push_back(name);
    }
    return 0;
}
}  // namespace

ScriptHost::ScriptHost(Platform& platform, const BootConfig& config)
    : platform_(platform), config_(config), L_(luaL_newstate()) {
    luaL_openlibs(L_);

    // The scripts run in their own environment table (the exe's Game root table, `this`). Engine
    // functions and values are visible there by bare name; the standard libraries, `res` and
    // `particles` are only reachable through `_G`, which is the real global table.
    lua_newtable(L_);
    lua_pushvalue(L_, -1);
    lua_setfield(L_, LUA_REGISTRYINDEX, "ab.env");
    lua_pushvalue(L_, LUA_GLOBALSINDEX);
    lua_setfield(L_, -2, "_G");
    lua_pushvalue(L_, -1);
    lua_setfield(L_, -2, "this");
    // options.lua sets gamelua.releaseBuild, gamelua.gameVersionNumber ...; the scripts read them by bare
    // name, so gamelua is the environment itself
    lua_pushvalue(L_, -1);
    lua_setfield(L_, -2, "gamelua");
    lua_pop(L_, 1);

    RegisterApi(L_, &platform_);
    // Angry Birds Rio's scripts use `res` and `particles` by bare name (the later versions go through _G)
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    lua_getglobal(L_, "res");
    lua_setfield(L_, -2, "res");
    lua_getglobal(L_, "particles");
    lua_setfield(L_, -2, "particles");
    lua_pop(L_, 1);

    // Angry Birds Island HD (Hatchery) tile manager natives (hatchery_natives.lua); only functions, nothing runs until a script calls them
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    if (luaL_loadbuffer(L_, (const char*)kHatcheryNativesLua, kHatcheryNativesLuaSize, "hatchery_natives.lua") == 0) {
        lua_pushvalue(L_, -2);
        if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "hatchery natives: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
    } else { std::fprintf(stderr, "hatchery natives: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
    lua_pop(L_, 1);

    SetString("scriptPath", config_.scriptPath);
    if (IsSpaceDataFolder()) {          // Angry Birds Space (the PC game, or a single-folder build)
        if (!config_.commonScriptPath.empty()) SetString("commonScriptPath", config_.commonScriptPath);
        lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
        if (luaL_loadbuffer(L_, (const char*)kSpaceNativesLua, kSpaceNativesLuaSize, "space_natives.lua") == 0) {
            lua_pushvalue(L_, -2);
            if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "space natives: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        } else { std::fprintf(stderr, "space natives: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        lua_pop(L_, 1);
    }
    SetString("imagePath", config_.imagePath);
    SetString("fontPath", config_.fontPath);
    SetString("audioPath", config_.audioPath);
    SetString("localizationPath", config_.localizationPath);
    SetString("levelPath", config_.levelPath);
    SetString("deviceModel", config_.deviceModel);
    // Seasons 4.x (settingsWrapper.lua builds "installationId_" .. uniqueInstallationId) and the later scripts read these globals from the exe:
    // the id of this install, and the `native` table of the platform services (apprater, ...). A service the PC exe does not have is simply a missing field.
    SetString("uniqueInstallationId", "angrybirdsengine");
    // The `engine` table every script can use (see docs/SCRIPTING.md, "Changing the engine from Lua"): the pristine natives, a list of their names and
    // events the engine raises (update, levelLoaded, objectCreated, objectRemoved). It is made before any game script runs.
    RunString(
        "local G = _G\n"
        "local env = G.getfenv(1)\n"
        "local original, resOriginal, handlers = {}, {}, {}\n"
        "for k, v in G.pairs(env) do if G.type(v) == 'function' then original[k] = v end end\n"
        "if G.type(env.res) == 'table' then for k, v in G.pairs(env.res) do if G.type(v) == 'function' then resOriginal[k] = v end end end\n"
        "local engine = {version = '2.9', original = original, resOriginal = resOriginal, hookCount = 0}\n"
        "engine.tuningDefaults = {['sw.forceDivisor']=10,['sw.homingDamage']=0.2,['sw.wobbleFactor']=0.02,['sw.wobbleMax']=0.1,['sw.removeLimit']=0.88,['rio.wobbleFactor']=0.02,['rio.wobbleMax']=0.1,['zoom.stepIn']=0.2,['zoom.stepOut']=0.1,['zoom.duration']=0.5,['solver.velocityThreshold']=1,['solver.baumgarte']=0.2,['solver.maxRotation']=1.5707964,['solver.angularSleepTolerance']=0.034906585,['solver.maxLinearCorrection']=0.2,['solver.linearSlop']=0.05,['solver.timeToSleep']=0.25,['solver.sleepTolerance']=0.1,['water.buoyancy']=0.1,['water.band']=1.5,['water.keepOrientationOffset']=0.25,['water.boxPushFactor']=0.25,['water.pushSpeedLimit']=15,['gravity.revertFactor']=-0.1,['particle.amountScale']=1,['particle.speedScale']=1,['particle.lifeScale']=1,['particle.sizeScale']=1,['particle.gravityScale']=1,['classic.blockForce']=0.1,['classic.blockScore']=10,['classic.birdForceDivisor']=10,['classic.legacyVelocityFactor']=1.75,['gravity.birdDecay']=1.3,['wobble.duration']=1,['wobble.maxScale']=4,['wobble.cutoff']=0.01,['wobble.frequency']=100,['wobble.baseFrequency']=5,['flip.speed']=3,['moving.tolerance']=0.5,['moving.zeroTolerance']=0.001,['background.skip']=0,['foreground.skip']=0}\n"
        "function engine.natives(includeRes)\n"
        "    local names = {}\n"
        "    for k in G.pairs(original) do names[#names + 1] = k end\n"
        "    if includeRes then for k in G.pairs(resOriginal) do names[#names + 1] = 'res.' .. k end end\n"
        "    G.table.sort(names)\n"
        "    return names\n"
        "end\n"
        "function engine.on(event, fn)\n"
        "    G.assert(G.type(event) == 'string' and G.type(fn) == 'function', 'engine.on(event, function)')\n"
        "    local list = handlers[event]\n"
        "    if not list then list = {}; handlers[event] = list end\n"
        "    list[#list + 1] = fn\n"
        "    engine.hookCount = engine.hookCount + 1\n"
        "    return fn\n"
        "end\n"
        "function engine.off(event, fn)\n"
        "    local list = handlers[event]\n"
        "    if not list then return false end\n"
        "    for i = #list, 1, -1 do if list[i] == fn then G.table.remove(list, i); engine.hookCount = engine.hookCount - 1; return true end end\n"
        "    return false\n"
        "end\n"
        "function engine.emit(event, ...)\n"
        "    local list = handlers[event]\n"
        "    if not list then return end\n"
        "    for i = 1, #list do\n"
        "        local ok, err = G.pcall(list[i], ...)\n"
        "        if not ok then G.print('engine.on(' .. event .. '): ' .. G.tostring(err)) end\n"
        "    end\n"
        "end\n"
        "env.engine = engine\n");
    GetEnvField("native");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); NewGlobalTable("native"); } else lua_pop(L_, 1);
    GetEnvField("tuning");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); NewGlobalTable("tuning"); } else lua_pop(L_, 1);
    lua_pushboolean(L_, config_.isLiteVersion);
    SetEnvField("isLiteVersion");
    lua_pushboolean(L_, config_.showCursor);
    SetEnvField("showCursor");
    lua_pushboolean(L_, config_.isBetaVersion);
    SetEnvField("isBetaVersion");
    lua_pushboolean(L_, 0);
    SetEnvField("roku");
    lua_pushboolean(L_, 0);
    SetEnvField("rokuOnWindows");
    lua_pushnumber(L_, config_.screenWidth);
    SetEnvField("screenWidth");
    lua_pushnumber(L_, config_.screenHeight);
    SetEnvField("screenHeight");
    lua_pushnumber(L_, 1);
    SetEnvField("zoomLevel");
    lua_pushnumber(L_, 0);
    SetEnvField("touchcount");

    const char* tables[] = {"keyPressed", "keyReleased", "keyHold", "touches", "multitouchSweep", "multitouchZoom",
                            "clippedText", "objects"};
    for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) NewGlobalTable(tables[i]);

    // The touch gesture tables: no zoom is cooling down, no sweep is going on (the scripts compare these directly).
    GetEnvField("multitouchZoom");
    lua_pushnumber(L_, -1); lua_setfield(L_, -2, "zoomCoolingTime");
    lua_pop(L_, 1);
    GetEnvField("multitouchSweep");
    lua_pushboolean(L_, 0); lua_setfield(L_, -2, "isSweepping");
    lua_pushnumber(L_, 0); lua_setfield(L_, -2, "sweepSpeed");
    lua_pop(L_, 1);

    // cursor { x, y, wheel, wheelTriggered }
    lua_newtable(L_);
    lua_pushnumber(L_, 0); lua_setfield(L_, -2, "x");
    lua_pushnumber(L_, 0); lua_setfield(L_, -2, "y");
    lua_pushnumber(L_, 0); lua_setfield(L_, -2, "wheel");
    lua_pushboolean(L_, 0); lua_setfield(L_, -2, "wheelTriggered");
    SetEnvField("cursor");

    // objects.world / loadedObjects / joints / counts are filled by the level loader and the scripts
    GetEnvField("objects");
    const char* sub[] = {"world", "loadedObjects", "joints", "counts", "deadBlocks"};
    for (size_t i = 0; i < sizeof(sub) / sizeof(sub[0]); i++) {
        lua_newtable(L_);
        lua_setfield(L_, -2, sub[i]);
    }
    lua_pop(L_, 1);
    lua_newtable(L_);
    SetEnvField("deadBlocks");

    LoadPersisted("settings", "settings.lua");
    LoadPersisted("highscores", "highscores.lua");
    // Angry Birds Space keeps more tables in files of their own (the exe loads them next to settings and highscores; the scripts read bi_data.root.x and so on)
    if (IsSpaceDataFolder()) {
        LoadPersisted("bi_data", "bi_data.lua", true);
        LoadPersisted("g_eaglePurchasesTable", "eaglepurchases.lua", true);
        LoadPersisted("g_episodePurchasesTable", "episodepurchases.lua", true);
        LoadPersisted("g_gamePurchasesTable", "gamepurchases.lua", true);
    }
}

ScriptHost::~ScriptHost() { lua_close(L_); }

// Pops the top value into the script environment under `name`.
void ScriptHost::SetEnvField(const char* name) {
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    lua_insert(L_, -2);
    lua_setfield(L_, -2, name);
    lua_pop(L_, 1);
}

// Pushes the value of `name` in the script environment.
void ScriptHost::GetEnvField(const char* name) {
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L_, -1, name);
    lua_remove(L_, -2);
}

// Loads a Lua file so that it runs in the script environment. Leaves the function on the stack.
int ScriptHost::LoadInEnv(const std::string& path) {
    std::vector<uint8_t> bytes;
    int rc = 1;
    if (ab::ReadLuaFile(path, bytes)) rc = luaL_loadbuffer(L_, (const char*)bytes.data(), bytes.size(), ("@" + path).c_str());
    else lua_pushstring(L_, ("cannot open " + path).c_str());
    if (rc == 0) {
        lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
        lua_setfenv(L_, -2);
    }
    return rc;
}

void ScriptHost::SetString(const char* name, const std::string& v) {
    lua_pushlstring(L_, v.data(), v.size());
    SetEnvField(name);
}

void ScriptHost::NewGlobalTable(const char* name) {
    lua_newtable(L_);
    SetEnvField(name);
}

// settings.lua / highscores.lua: either `tableName = {...}` (written by this engine) or, in the original
// (encrypted) files, one assignment per entry (`Level27 = {...}`). The file runs in a table of its own and
// that table, or the table inside it, becomes the global. A missing file means an empty table.
bool ScriptHost::IsSpaceDataFolder() const {
    if (!config_.commonScriptPath.empty()) return true;
    std::vector<uint8_t> bytes;            // a Space build that keeps everything in one scripts folder still has blocks_planets.lua
    return ab::ReadLuaFile(config_.scriptPath + "/blocks_planets.lua", bytes) && !bytes.empty();
}

void ScriptHost::LoadPersisted(const char* tableName, const char* fileName, bool onlyIfExists) {
    ab::SetSaveFamilySpace(IsSpaceDataFolder());
    std::string path = platform_.ResolveUserFile(fileName, true), text;
    bool encrypted = false;
    if (onlyIfExists && !ab::ReadSaveFile(path, text, encrypted)) return;
    // The file, or the copy the last save kept as <file>.bak when the file is missing, empty or cannot be read (a game closed in the middle of a save): never start with an empty save while a good one exists
    lua_newtable(L_);                                           // the file's own environment
    const std::string candidates[2] = {path, path + ".bak"};
    bool loaded = false;
    for (int c = 0; c < 2 && !loaded; c++) {
        text.clear(); encrypted = false;
        if (!ab::ReadSaveFile(candidates[c], text, encrypted)) {
            if (c == 0) platform_.Print("no saved file " + path);
            continue;
        }
        if (c == 0 && text.find_first_not_of(" \t\r\n") == std::string::npos) {                 // an empty file: the save was cut off, or this is a save of another game that was set aside
            std::ifstream bak((path + ".bak").c_str(), std::ios::binary);
            if (bak.good()) { platform_.Print("saved file " + path + " is empty, trying " + path + ".bak"); continue; }
        }
        platform_.Print("saved file " + candidates[c] + (encrypted ? " (encrypted original)" : " (plain)"));
        if (luaL_loadbuffer(L_, text.data(), text.size(), ("@" + candidates[c]).c_str()) == 0) {
            lua_pushvalue(L_, -2);
            lua_setfenv(L_, -2);
            if (lua_pcall(L_, 0, 0, 0) == 0) { loaded = true; break; }
        }
        platform_.Print(std::string("cannot read ") + candidates[c] + ": " + lua_tostring(L_, -1));
        lua_pop(L_, 1);
        if (c == 0) { std::ofstream keep((path + ".corrupt").c_str(), std::ios::binary); keep.write(text.data(), (std::streamsize)text.size()); }   // what could not be read is kept, not overwritten
        lua_pop(L_, 1); lua_newtable(L_);                       // a fresh environment for the next candidate
    }
    lua_getfield(L_, -1, tableName);
    if (lua_istable(L_, -1)) lua_remove(L_, -2);
    else lua_pop(L_, 1);                                        // Space: the file's own table, whose `root` field the scripts read (settings.root.x)
    SetEnvField(tableName);
}

bool ScriptHost::RunFile(const std::string& path) {
    if (LoadInEnv(path) != 0 || lua_pcall(L_, 0, 0, 0) != 0) {
        error_ = path + ": " + lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool ScriptHost::LoadScripts() {
    auto installSpaceEditor = [&]() {   // v2.5: Angry Birds Space has no editor scripts, the engine brings one (F2 in a level, space_editor.lua)
        lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
        if (luaL_loadbuffer(L_, (const char*)kSpaceEditorLua, kSpaceEditorLuaSize, "space_editor.lua") == 0) {
            lua_pushvalue(L_, -2);
            if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "space editor: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        } else { std::fprintf(stderr, "space editor: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        lua_pop(L_, 1);
    };
    if (!config_.commonScriptPath.empty()) {
        space_ = true;
        starWars_ = true;
        if (!LoadTableFile(L_, "starTable", config_.scriptPath + "/starLimits.lua", &error_)) return false;   // the star limits per level
        bool ok = RunFile(config_.commonScriptPath + "/gamelogic.lua");
        if (ok) {   // Star Wars: the exe runs options.lua (releaseBuild, customerString, gameVersionNumber ...) itself, before the first callback
            GetEnvField("releaseBuild");
            bool haveOptions = !lua_isnil(L_, -1);
            lua_pop(L_, 1);
            std::vector<uint8_t> probe;
            if (!haveOptions && ab::ReadLuaFile(config_.scriptPath + "/options.lua", probe) && !probe.empty())
                ok = RunFile(config_.scriptPath + "/options.lua");
        }
        if (ok) installSpaceEditor();
        return ok;
    }   // Space: its gamelogic.lua loads the rest
    const char* files[] = {"/loadlist.lua", "/gamelogic.lua"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (!RunFile(config_.scriptPath + files[i])) return false;
    }
    // starLimits.lua, blocks.lua and particles.lua are data files: each fills its own table
    if (!LoadTableFile(L_, "starTable", config_.scriptPath + "/starLimits.lua", &error_)) return false;
    std::vector<uint8_t> blocksBytes;
    if (ReadLuaFile(config_.scriptPath + "/blocks.lua", blocksBytes) && !blocksBytes.empty()) {
        if (!LoadTableFile(L_, "blockTable", config_.scriptPath + "/blocks.lua", &error_)) return false;
        if (!LoadTableFile(L_, "particleTable", config_.scriptPath + "/particles.lua", &error_)) return false;
    } else if (ReadLuaFile(config_.scriptPath + "/blocks_planets.lua", blocksBytes) && !blocksBytes.empty()) {
        // Angry Birds Space: loadBlockData in the scripts loads the block files
        space_ = true;
        // Builds whose scripts never load the block files (the exe does it itself there: loadBlockData is not called) get the same
        // load here, in the order of the retail loadBlockData, so blockTable, the themes, materials and particles exist before the first scene.
        GetEnvField("loadBlockData");
        bool hasLoader = lua_isfunction(L_, -1);
        lua_pop(L_, 1);
        GetEnvField("blockTable");
        bool hasTable = lua_istable(L_, -1);
        lua_pop(L_, 1);
        if (!hasLoader && !hasTable) {
            struct F { const char* file; const char* name; };
            static const F list[] = {
                {"materials", "materials"}, {"damagefactors", "damageFactors"}, {"blocks_levelgoals", "blockTable"}, {"birds", "blockTable"},
                {"blocks_hazard", "blockTable"}, {"blocks_wood", "blockTable"}, {"blocks_menu", "blockTable"}, {"blocks_comic", "blockTable"},
                {"blocks_bonusLevels", "blockTable"}, {"blocks_bosses", "blockTable"}, {"blocks_stone", "blockTable"}, {"blocks_glass", "blockTable"},
                {"blocks_gameElements", "blockTable"}, {"blocks_static", "blockTable"}, {"blocks_planets", "blockTable"}, {"blocks_asteroids", "blockTable"},
                {"blocks_decorations", "blockTable"}, {"blocks_sensors", "blockTable"}, {"blocks_items", "blockTable"}, {"blocks_scoreobjects", "blockTable"},
                {"blocks_themeObjects", "blockTable"}, {"blocks_compound", "blockTable"}, {"blocks_metal", "blockTable"},
                {"groups", "groups"}, {"themes", "themes"}, {"textures", "textures"}, {"worldAttributes", "worldAttributes"}, {"particles", "particleTable"}};
            std::string code;
            for (const F& f : list) {
                std::vector<uint8_t> probe;
                if (ReadLuaFile(config_.scriptPath + "/" + f.file + ".lua", probe) && !probe.empty())
                    code += std::string("loadLuaFile(scriptPath .. \"/") + f.file + ".lua\", \"" + f.name + "\", true, true)\n";
            }
            RunString(code.c_str());
        }
    } else { rio_ = true; if (!LoadRioTables(L_)) { error_ = "Rio data files could not be loaded"; return false; }
        // the game scene draws with objects.theme before a level has told it which theme it uses
        lua_pushboolean(L_, 1);
        SetEnvField("rioMode");
        GetEnvField("objects");
        lua_pushstring(L_, "theme2");
        lua_setfield(L_, -2, "theme");
        lua_pop(L_, 1);
        // The level selection opens only the levels up to settings.lastOpenLevels[pack], which the scripts raise when a level is
        // completed. A level that has a high score but is beyond that number (a save from elsewhere, a level played from the
        // extra-play menu) stayed locked until the one before it was completed again, in the exe as well: open them.
        RunString(
            "local original = getLastOpenLevelNumberInLevelPack\n"
            "function getLastOpenLevelNumberInLevelPack(pack)\n"
            "    local open = original(pack)\n"
            "    local order, scores = levelOrder[pack], highscores\n"
            "    if order and scores then\n"
            "        for i = #order, open + 1, -1 do\n"
            "            local s = scores[order[i]]\n"
            "            if s and s.score and s.score > 0 then\n"
            "                open = s.completed and i + 1 or i\n"
            "                settings.lastOpenLevels[pack] = open\n"
            "                break\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "    return open\n"
            "end\n");
        // The reward popup (a golden fruit flying to the middle of the screen) had no counter, so the player could not see how many
        // fruits of the episode were found. The popup page is made after the scripts load, so it is changed on its first use.
        RunString(
            "local original = physicsToScreenTransform\n"
            "local episodeOf = {PINEAPPLE = 1, BANANA = 2, MELON = 3, PAPAYA = 4, APPLE = 5, MANGO = 6}\n"
            "function physicsToScreenTransform(x, y)\n"
            "    local popup = rewardPopup\n"
            "    if popup and not popup.tagPatched then\n"
            "        popup.tagPatched = true\n"
            "        local show, update = popup.show, popup.update\n"
            "        function popup.show(self, sprite, sx, sy)\n"
            "            show(self, sprite, sx, sy)\n"
            "            local episode = episodeOf[_G.string.sub(sprite, 11)]\n"
            "            if not episode then return end\n"
            "            if not self.items.tag then\n"
            "                self:insertItem('tag', gamelua.SpriteItem:new({sprite = 'STAND_NOTE', visible = false, selectable = false, useScreenCoordinates = true}))\n"
            "                self:insertItem('tagText', gamelua.TextItem:new({text = '', font = 'FONT_INGAME_SMALL', visible = false, selectable = false, useScreenCoordinates = true}))\n"
            "            end\n"
            "            local count, total = getEpisodeCollectableCount(episode)\n"
            "            self.items.tagText.text = count .. '/' .. total\n"
            "        end\n"
            "        function popup.update(self, dt, a)\n"
            "            update(self, dt, a)\n"
            "            local reward, tag, text = self.items.reward, self.items.tag, self.items.tagText\n"
            "            if tag and reward then\n"
            "                local shown = reward.visible and self.visible\n"
            "                local scale = reward.xs or 1\n"
            "                tag.visible, text.visible = shown, shown\n"
            "                tag.localX, tag.x = reward.localX, reward.localX\n"
            "                tag.localY, tag.y = reward.localY + 95 * scale, reward.localY + 95 * scale\n"
            "                tag.xs, tag.ys = scale, scale\n"
            "                text.localX, text.x = tag.localX, tag.localX\n"
            "                text.localY, text.y = tag.localY, tag.localY\n"
            "                text.xs, text.ys = scale, scale\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "    return original(x, y)\n"
            "end\n");
    }
    EnsureClassicEditor(L_);
    if (IsSpaceDataFolder()) installSpaceEditor();
    // A level played from the editor (Shift+P) leaves through returnToEditor when the pause key is pressed, but the pause button of the
    // game opened the pause menu, whose exit button then went to a menu page that does not exist while editing (updateMenuPage: field 'page' is nil).
    // The pause button now does what the pause key does.
    RunString(
        "local original = goToMenu\n"
        "if _G.type(original) == \"function\" and _G.type(returnToEditor) == \"function\" then\n"
        "    function goToMenu()\n"
        "        if startedFromEditor then returnToEditor() else original() end\n"
        "    end\n"
        "end\n");
    // The scripts' debug tools (fps 0/F, background 9, hit boxes / sleeping objects 8, level clear C, shift+arrows to change level, ...)
    // are all behind `not releaseBuild` or cheatsEnabled. releaseBuild itself must stay true: false unlocks every level and episode
    // (resetSettings, the level select). So the cheats are switched on, and the two per-frame functions that read the debug keys run
    // in an environment where releaseBuild reads false and everything else reads and writes the real globals.
    if (!std::getenv("AB_NODEBUGKEYS")) RunString(
        "local env = _G.getfenv(1)\n"
        "env.cheatsEnabled = true\n"
        "env.showEditor = true\n"
        "local view = _G.setmetatable({releaseBuild = false}, {__index = env, __newindex = env})\n"
        "for _, name in _G.ipairs({\"update\", \"updateGame\"}) do\n"
        "    if _G.type(env[name]) == \"function\" then _G.setfenv(env[name], view) end\n"
        "end\n"
        "env.__abview = view\n");
    // the view is kept for loadLuaFile: Angry Birds 2.0.0 builds the main menu's EDITOR button (MainMenu.init) and places it
    // (MainMenu.layout) only when releaseBuild is false, and main_menu.lua is loaded after this point
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L_, -1, "__abview");
    lua_setfield(L_, LUA_REGISTRYINDEX, "ab.view");
    lua_pushnil(L_);
    lua_setfield(L_, -2, "__abview");
    lua_pop(L_, 1);
    return true;
}

void ScriptHost::TraceMissingGlobals(bool on) {
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    if (on) {
        lua_newtable(L_);
        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, MissingGlobalHook, 1);
        lua_setfield(L_, -2, "__index");
        lua_setmetatable(L_, -2);
    } else {
        lua_pushnil(L_);
        lua_setmetatable(L_, -2);
    }
    lua_pop(L_, 1);
}

bool ScriptHost::CallWithArgs(const char* name, int nargs) {
    // the function and its arguments are pushed by the caller; debug.traceback is the error handler
    int base = lua_gettop(L_) - nargs - 1;
    lua_getglobal(L_, "debug");
    lua_getfield(L_, -1, "traceback");
    lua_remove(L_, -2);
    lua_insert(L_, base + 1);
    if (lua_pcall(L_, nargs, 0, base + 1) != 0) {
        std::string msg = std::string(name) + ": " + lua_tostring(L_, -1);
        lua_pop(L_, 2);
        // Space builds can be incomplete (scene scripts or block files missing) or draw a scene before its tables exist:
        // an error in a frame's update or draw is printed once and the frame is skipped instead of ending the game.
        // AB_STRICT=1 restores the old behaviour (the first script error ends the run).
        static const bool strict = std::getenv("AB_STRICT") != nullptr;
        if (!strict && IsSpaceDataFolder() && (std::strcmp(name, "update") == 0 || std::strcmp(name, "draw") == 0)) {
            if (reportedErrors_.insert(msg).second) std::fprintf(stderr, "script error (skipped, the game goes on): %s\n", msg.c_str());
            return true;
        }
        error_ = msg;
        return false;
    }
    lua_pop(L_, 1);
    return true;
}

void ScriptHost::RunString(const char* code) {
    if (luaL_loadstring(L_, code) != 0) { std::fprintf(stderr, "exec: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); return; }
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    lua_setfenv(L_, -2);
    if (lua_pcall(L_, 0, 0, 0) != 0) { std::fprintf(stderr, "exec: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
}

bool ScriptHost::Call(const char* name) {
    GetEnvField(name);
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    return CallWithArgs(name, 0);
}

bool ScriptHost::Update(float dt, float realDt) {
    GetEnvField("update");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushnumber(L_, dt);
    lua_pushnumber(L_, realDt);
    const bool ok = CallWithArgs("update", 2) && DrawEditorOverlay();
    EmitHook(L_, "update", nullptr, nullptr, 2, dt, realDt);   // engine.on("update", function(dt, realDt) ... end)
    return ok;
}

// Seasons and the later games keep the editor's on-screen extras (level name, status line, group palette) in a global drawEditor() that the exe
// calls after the frame is drawn; their updateEditor and drawGame do not call it themselves.
bool ScriptHost::DrawEditorOverlay() {
    GetEnvField("currentGameMode");
    GetEnvField("updateEditor");
    bool inEditor = lua_isfunction(L_, -1) && lua_rawequal(L_, -1, -2);
    lua_pop(L_, 2);
    if (!inEditor) return true;
    return Call("drawEditor");
}

bool ScriptHost::BirdCollision(const std::string& a, const std::string& b, float force, float damage) {
    GetEnvField("birdCollision");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushlstring(L_, a.data(), a.size());
    lua_pushlstring(L_, b.data(), b.size());
    lua_pushnumber(L_, force);
    lua_pushnumber(L_, damage);
    return CallWithArgs("birdCollision", 4);
}

bool ScriptHost::BlockCollision(const std::string& a, const std::string& b, float force, bool wasDamageDone) {
    GetEnvField("blockCollision");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushlstring(L_, a.data(), a.size());
    lua_pushlstring(L_, b.data(), b.size());
    lua_pushnumber(L_, force);
    lua_pushboolean(L_, wasDamageDone);
    if (space_) {      // Space: blockCollision(name, other, force, wasDamageDone, dot, resultDamage)
        lua_pushboolean(L_, wasDamageDone);
        lua_pushnumber(L_, 0);
        return CallWithArgs("blockCollision", 6);
    }
    return CallWithArgs("blockCollision", 4);
}

bool ScriptHost::BirdCollisionSW(const std::string& a, const std::string& b, float force, float damage, const float pn[4]) {
    GetEnvField("birdCollision");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushlstring(L_, a.data(), a.size());
    lua_pushlstring(L_, b.data(), b.size());
    lua_pushnumber(L_, force);
    lua_pushnumber(L_, damage);
    for (int i = 0; i < 4; i++) lua_pushnumber(L_, pn[i]);
    return CallWithArgs("birdCollision", 8);
}

bool ScriptHost::BlockCollisionSW(const std::string& a, const std::string& b, float force, bool wasDamageDone, float damage, const float pn[4]) {
    GetEnvField("blockCollision");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushlstring(L_, a.data(), a.size());
    lua_pushlstring(L_, b.data(), b.size());
    lua_pushnumber(L_, force);
    lua_pushboolean(L_, wasDamageDone);
    lua_pushboolean(L_, 0);
    lua_pushnumber(L_, damage);
    for (int i = 0; i < 4; i++) lua_pushnumber(L_, pn[i]);
    return CallWithArgs("blockCollision", 10);
}

bool ScriptHost::ResolutionChanged(float w, float h) {
    GetEnvField("resolutionChanged");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushnumber(L_, w);
    lua_pushnumber(L_, h);
    return CallWithArgs("resolutionChanged", 2);
}

}  // namespace ab
