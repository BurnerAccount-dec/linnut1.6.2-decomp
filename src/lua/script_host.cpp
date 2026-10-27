#include <cstdlib>
#include "lua/script_host.h"
#include "lua/aes.h"

#include <cstdio>

extern "C" {
#include "lauxlib.h"
#include "lualib.h"
}

#include "lua/lua_api.h"

namespace ab {

extern const unsigned char kEditorClassicLua[];
extern const unsigned int kEditorClassicLuaSize;

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

    SetString("scriptPath", config_.scriptPath);
    SetString("imagePath", config_.imagePath);
    SetString("fontPath", config_.fontPath);
    SetString("audioPath", config_.audioPath);
    SetString("localizationPath", config_.localizationPath);
    SetString("levelPath", config_.levelPath);
    SetString("deviceModel", config_.deviceModel);
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
void ScriptHost::LoadPersisted(const char* tableName, const char* fileName) {
    std::string path = platform_.ResolveUserFile(fileName, true), text;
    bool encrypted = false;
    lua_newtable(L_);                                           // the file's own environment
    if (!ab::ReadSaveFile(path, text, encrypted)) {
        platform_.Print("no saved file " + path);
    } else {
        platform_.Print("saved file " + path + (encrypted ? " (encrypted original)" : " (plain)"));
        if (luaL_loadbuffer(L_, text.data(), text.size(), ("@" + path).c_str()) == 0) {
            lua_pushvalue(L_, -2);
            lua_setfenv(L_, -2);
            if (lua_pcall(L_, 0, 0, 0) != 0) {
                platform_.Print(std::string("cannot run ") + path + ": " + lua_tostring(L_, -1));
                lua_pop(L_, 1);
            }
        } else {
            platform_.Print(std::string("cannot read ") + path + ": " + lua_tostring(L_, -1));
            lua_pop(L_, 1);
        }
    }
    lua_getfield(L_, -1, tableName);
    if (lua_istable(L_, -1)) lua_remove(L_, -2);
    else lua_pop(L_, 1);
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
        error_ = std::string(name) + ": " + lua_tostring(L_, -1);
        lua_pop(L_, 2);
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
    return CallWithArgs("update", 2) && DrawEditorOverlay();
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
    return CallWithArgs("blockCollision", 4);
}

bool ScriptHost::ResolutionChanged(float w, float h) {
    GetEnvField("resolutionChanged");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushnumber(L_, w);
    lua_pushnumber(L_, h);
    return CallWithArgs("resolutionChanged", 2);
}

}  // namespace ab
