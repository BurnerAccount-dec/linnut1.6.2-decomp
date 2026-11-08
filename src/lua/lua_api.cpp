#ifdef _WIN32
#define NOMINMAX
#include <cstdlib>
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iterator>
#include <vector>
#include "lua/aes.h"
#include <cstring>
#include "lua/lua_api.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lualib.h"

}

namespace ab {
extern const unsigned char kCompoImageLua[];
extern const unsigned int kCompoImageLuaSize;
extern const unsigned char kInvisibleButtonLua[];
extern const unsigned int kInvisibleButtonLuaSize;
bool LoadTableFile(lua_State* L, const char* name, const std::string& path, std::string* error, bool append);
void SyncWorldZ(lua_State* L, const std::string& name, float z);     // v2.2.3, defined at the end
namespace {

Platform* P(lua_State* L) { return static_cast<Platform*>(lua_touserdata(L, lua_upvalueindex(1))); }
float N(lua_State* L, int i) { return (float)luaL_checknumber(L, i); }
float NOpt(lua_State* L, int i, float def) { return (float)luaL_optnumber(L, i, def); }
std::string S(lua_State* L, int i) { return lua_isnoneornil(L, i) ? std::string() : std::string(luaL_checkstring(L, i)); }   // a nil name is an empty one
bool B(lua_State* L, int i) { return lua_toboolean(L, i) != 0; }
// The script environment table (registry["ab.env"]); engine functions and values live there.
void PushEnv(lua_State* L) { lua_getfield(L, LUA_REGISTRYINDEX, "ab.env"); }

// ---- table field readers (for createJoint / setJointParameters)
bool Has(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    bool has = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return has;
}
float FieldN(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    return v;
}
bool FieldB(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    bool v = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return v;
}
std::string FieldS(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return v;
}

// ---- window / system
int l_requestExit(lua_State* L) { P(L)->RequestExit(); return 0; }
int l_print(lua_State* L) {
    std::string out;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) out += "\t";
        if (lua_isstring(L, i)) out += lua_tostring(L, i);
        else if (lua_isboolean(L, i)) out += lua_toboolean(L, i) ? "true" : "false";
        else out += luaL_typename(L, i);
    }
    P(L)->Print(out);
    return 0;
}
int l_printGlobals(lua_State* L) {
    PushEnv(L);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) P(L)->Print(std::string(lua_tostring(L, -2)) + " (" + luaL_typename(L, -1) + ")");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 0;
}
int l_setBGColor(lua_State* L) { P(L)->SetBGColor(N(L, 1), N(L, 2), N(L, 3)); return 0; }
int l_captureMouse(lua_State* L) { P(L)->CaptureMouse(B(L, 1)); return 0; }
int l_isMouseCaptured(lua_State* L) { lua_pushboolean(L, P(L)->IsMouseCaptured()); return 1; }
int l_setFullScreenMode(lua_State* L) { P(L)->SetFullScreen(B(L, 1)); return 0; }
int l_isInFullScreenMode(lua_State* L) { lua_pushboolean(L, P(L)->IsFullScreen()); return 1; }
int l_setResolution(lua_State* L) { P(L)->SetResolution(N(L, 1), N(L, 2)); return 0; }
int l_setIsMultitouchMouseWheelSimulationEnabled(lua_State* L) { P(L)->SetMouseWheelSimulation(B(L, 1)); return 0; }
int l_doesMouseClickSetsTouchCount(lua_State* L) { lua_pushboolean(L, P(L)->MouseClickSetsTouchCount()); return 1; }
int l_setEditing(lua_State* L) { P(L)->SetEditing(B(L, 1)); return 0; }
int l_setGameOn(lua_State* L) { P(L)->SetGameOn(B(L, 1)); return 0; }

// createThemeSprite(name, sprite, x, y, scaleX, scaleY, angle, speedX, layer) and friends
bool RioMode(lua_State* L);
bool IsSpaceData(lua_State* L);
bool HasNum(lua_State* L, int t, const char* k);
bool BoolOr(lua_State* L, int t, const char* k, bool def);
float NumOr(lua_State* L, int t, const char* k, float def);
int l_createThemeSprite(lua_State* L) {
    if (!RioMode(L) && IsSpaceData(L)) {
        P(L)->CreateThemeSpriteSpace(S(L, 1), S(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), (int)N(L, 8), N(L, 9), B(L, 10), lua_isnumber(L, 11) ? (float)lua_tonumber(L, 11) : 0.0f, lua_isnumber(L, 12) ? (float)lua_tonumber(L, 12) : 0.0f);
        return 0;
    }
    if (RioMode(L) || IsSpaceData(L)) {      // Rio and Space: ..., angle, layer, angleVel (Space calls it scaleSpeed), flipped [, velX, velY in Space]
        P(L)->CreateThemeSpriteRio(S(L, 1), S(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), (int)N(L, 8), N(L, 9), B(L, 10));
        return 0;
    }
    P(L)->CreateThemeSprite(S(L, 1), S(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), N(L, 8), (int)N(L, 9));
    return 0;
}
// createThemeAnimation(table) (0x484cc0): layer, x, y, scaleX, scaleY, angle, name, spriteName, velX, velY, scaleSpeed, startAnimTimer, animDelay,
// startingDelay, isAnimation, bLoop and animation (a list of the frames' sprite names)
int l_createThemeAnimation(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    Platform::ThemeAnimation a;
    a.layer = (int)NumOr(L, 1, "layer", 0); a.x = NumOr(L, 1, "x", 0); a.y = NumOr(L, 1, "y", 0);
    a.scaleX = NumOr(L, 1, "scaleX", 1); a.scaleY = NumOr(L, 1, "scaleY", 1); a.angle = NumOr(L, 1, "angle", 0);
    if (Has(L, 1, "name")) a.name = FieldS(L, 1, "name");
    if (Has(L, 1, "spriteName")) a.sprite = FieldS(L, 1, "spriteName");
    a.velX = NumOr(L, 1, "velX", 0); a.velY = NumOr(L, 1, "velY", 0); a.scaleSpeed = NumOr(L, 1, "scaleSpeed", 0);
    a.animTimer = NumOr(L, 1, "startAnimTimer", 0);
    a.animDelay = NumOr(L, 1, "animDelay", 0);
    a.startingDelay = HasNum(L, 1, "startingDelay") ? FieldN(L, 1, "startingDelay") : a.animDelay;   // 0x484fe5: the delay starts as animDelay unless given
    a.loop = BoolOr(L, 1, "bLoop", false);
    lua_getfield(L, 1, "animation");
    if (lua_istable(L, -1))
        for (int i = 1;; i++) {
            lua_rawgeti(L, -1, i);
            if (!lua_isstring(L, -1)) { lua_pop(L, 1); break; }
            a.frames.push_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    lua_pop(L, 1);
    P(L)->CreateThemeAnimation(a);
    return 0;
}
int l_modifyThemeSprite(lua_State* L) {
    P(L)->ModifyThemeSprite(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), (int)N(L, 7));
    return 0;
}
int l_removeThemeSprite(lua_State* L) { P(L)->RemoveThemeSprite(S(L, 1), (int)N(L, 2)); return 0; }
int l_updateThemeSprite(lua_State* L) { P(L)->UpdateThemeSprites(N(L, 1)); return 0; }
int l_setThemeSprite(lua_State* L) { P(L)->SetThemeSprite(S(L, 1), S(L, 2), (int)N(L, 3)); return 0; }
int l_rotateThemeSprites(lua_State* L) { P(L)->RotateThemeSprites(N(L, 1)); return 0; }
int l_updateBackgroundAndForegroundScrollingNative(lua_State* L) { P(L)->ScrollThemeLayers(N(L, 1), B(L, 2)); return 0; }

// Seasons: loadLevelFile(path, date) asked a server for a level of that date and getLoadStatus() reported
// {status = ...} until the answer came. Levels are local here, so the answer is ready at once (status 1).
int l_getLoadStatus(lua_State* L) {
    lua_newtable(L);
    lua_pushnumber(L, 1);
    lua_setfield(L, -2, "status");
    return 1;
}

int l_throwException(lua_State* L) { P(L)->Print("script exception: " + S(L, 1)); return 0; }

int l_loadLuaFileToObject(lua_State* L) {
    // loadLuaFileToObject(path, object, name): runs the file in a table of its own that falls back to
    // `object` for names it does not define, then stores that table as object[name]
    std::string path = S(L, 1);
    bool toObject = lua_istable(L, 2) && lua_isstring(L, 3);
    bool spaceBlocks = false;
    std::vector<uint8_t> bytes;
    int rc = 1;
    // Angry Birds Island HD (Hatchery) names its files from the data folder ("hatchery/scripts/..."); try that when the path does not exist as written
    bool found = ab::ReadLuaFile(path, bytes);
    if (!found) { std::string alt = P(L)->ResolveDataFile(path); if (alt != path) found = ab::ReadLuaFile(alt, bytes); }
    // the two ui components the Hatchery needs that 2.0.0's PC scripts do not have (CompoImage, InvisibleButton) are built in
    if (!found) {
        const struct { const char* file; const unsigned char* data; unsigned size; } builtIn[] = {
            {"ui_components/CompoImage.lua", kCompoImageLua, kCompoImageLuaSize},
            {"ui_components/InvisibleButton.lua", kInvisibleButtonLua, kInvisibleButtonLuaSize}};
        for (const auto& b : builtIn) {
            size_t n = std::strlen(b.file);
            if (path.size() >= n && path.compare(path.size() - n, n, b.file) == 0) { bytes.assign(b.data, b.data + b.size); found = true; }
        }
    }
    if (found) rc = luaL_loadbuffer(L, (const char*)bytes.data(), bytes.size(), ("@" + path).c_str());
    else lua_pushstring(L, ("cannot open " + path).c_str());
    if (rc == 0) {
        if (toObject) {
            if (*lua_tostring(L, 3) == 0) {
                lua_pushvalue(L, 2);                    // 2.0.0, name "": the file runs in the object itself
            } else {
                lua_newtable(L);                        // the file's table
                lua_newtable(L);                        // its metatable { __index = object }
                lua_pushvalue(L, 2);
                lua_setfield(L, -2, "__index");
                if (std::strcmp(lua_tostring(L, 3), "Hatchery") == 0) {
                    // hatchery.lua loads the rest of the Hatchery (its UI files, ui components, events) "into this": for this file `this` is the
                    // file's own table, so everything it defines (hatchery, hatcheryEventManager, ui ...) is shared by those files and stays
                    // out of the game's globals
                    lua_pushvalue(L, -2);
                    lua_setfield(L, -3, "this");
                    // and the Hatchery is written for the iPad (its sheets, context menu and panels pick their layout from gamelua.deviceModel):
                    // its `gamelua` reads everything from the game's, except that the device is an iPad
                    luaL_loadstring(L, "local env = ...; return setmetatable({deviceModel = \"ipad\"}, {__index = env, __newindex = env})");
                    lua_pushvalue(L, 2);
                    lua_call(L, 1, 1);
                    lua_setfield(L, -3, "gamelua");
                }
                lua_setmetatable(L, -2);
                // an object without a metatable of its own falls back to the globals, so the menu files
                // (Angry Birds Rio) also find `gamelua`, `res` and the engine functions by bare name
                if (!lua_getmetatable(L, 2)) {
                    PushEnv(L);
                    if (!lua_rawequal(L, -1, 2)) {
                        lua_newtable(L);
                        lua_pushvalue(L, -2);
                        lua_setfield(L, -2, "__index");
                        lua_setmetatable(L, 2);
                    }
                    lua_pop(L, 1);
                } else lua_pop(L, 1);
                lua_pushvalue(L, -1);
                lua_setfield(L, 2, lua_tostring(L, 3)); // object[name] = the file's table
            }
            lua_setfenv(L, -2);
        } else if (lua_isstring(L, 2) && *lua_tostring(L, 2)) {
            // loadLuaFile(path, name): the file runs in a table of its own (names it does not define come from
            // the globals) and that table becomes the global `name`, e.g. cutscenes
            // Angry Birds Space loads each block file with loadLuaFile(path, "blockTable"): the definitions it lists are filed
            // under blockTable.blocks[definition] (as the Rio loader does); materials, damage factors, themes and groups also
            // become fields of blockTable
            const bool isBlocks = std::strcmp(lua_tostring(L, 2), "blockTable") == 0;
            PushEnv(L);                                 // the globals
            lua_newtable(L);                            // the file's table
            lua_newtable(L);
            lua_pushvalue(L, -3);
            lua_setfield(L, -2, "__index");
            lua_setmetatable(L, -2);
            if (!isBlocks) {
                lua_pushvalue(L, -1);
                lua_setfield(L, -3, lua_tostring(L, 2));    // globals[name] = the file's table
                static const char* kNested[] = {"materials", "damageFactors", "themes", "groups"};
                for (const char* nn : kNested) if (std::strcmp(lua_tostring(L, 2), nn) == 0) {
                    lua_getfield(L, -2, "blockTable");
                    if (!lua_istable(L, -1)) {
                        lua_pop(L, 1);
                        lua_newtable(L);
                        lua_pushvalue(L, -1);
                        lua_setfield(L, -4, "blockTable");
                    }
                    lua_pushvalue(L, -2);
                    lua_setfield(L, -2, nn);
                    lua_pop(L, 1);
                }
            } else {
                lua_getfield(L, -2, "blockTable");
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1);
                    lua_newtable(L);
                    lua_pushvalue(L, -1);
                    lua_setfield(L, -4, "blockTable");
                }
                lua_getfield(L, -1, "blocks");
                if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, -1); lua_setfield(L, -3, "blocks"); }
                lua_pop(L, 2);
                // remember the file's table to file it after the run
                lua_pushvalue(L, -1);
                lua_setfield(L, LUA_REGISTRYINDEX, "ab.blockfile");
            }
            lua_remove(L, -2);
            lua_setfenv(L, -2);
            spaceBlocks = isBlocks;
        } else {
            PushEnv(L);
            lua_setfenv(L, -2);
        }
        rc = lua_pcall(L, 0, 0, 0);
        if (rc == 0 && spaceBlocks) {
            const char* filer =
                "local T, bt, env = ...\nlocal pairs, ipairs, type = _G.pairs, _G.ipairs, _G.type\n"
                "for k, v in pairs(T) do\n"
                "  if type(v) == 'number' then env[k] = v\n"
                "  elseif type(v) == 'table' then\n"
                "    for _, e in ipairs(v) do\n"
                "      if type(e) == 'table' and type(e.definition) == 'string' then bt.blocks[e.definition] = e end\n"
                "    end\n"
                "  end\n"
                "end\n"
                "if not bt.blocks.Ground then bt.blocks.Ground = {definition = 'Ground', type = 'box', sprite = '', texture = '', material = 'immovable'} end\n";
            if (luaL_loadstring(L, filer) == 0) {
                lua_getfield(L, LUA_REGISTRYINDEX, "ab.blockfile");
                PushEnv(L);
                lua_getfield(L, -1, "blockTable");
                lua_insert(L, -2);                      // T, blockTable, env
                if (lua_pcall(L, 3, 0, 0) != 0) { P(L)->Print(std::string("blockTable: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
            } else lua_pop(L, 1);
        }
        if (rc == 0) {
            // debug build: MainMenu.init / MainMenu.layout run with releaseBuild false (shows the 2.0.0 EDITOR button)
            lua_getfield(L, LUA_REGISTRYINDEX, "ab.view");
            PushEnv(L);
            lua_getfield(L, -1, "MainMenu");
            if (lua_istable(L, -1) && lua_istable(L, -3)) {
                for (const char* name : {"init", "layout"}) {
                    lua_pushstring(L, name);
                    lua_rawget(L, -2);
                    if (lua_isfunction(L, -1)) { lua_pushvalue(L, -4); lua_setfenv(L, -2); }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 3);
            // debug build: the iPad scripts keep the star coins and stars, and the Hatchery state, in settingsWrapper; 2.0.0's PC copy lacks them
            const std::string sw = "settingsWrapper.lua";
            if (!std::getenv("AB_NODEBUGKEYS") && path.size() >= sw.size() && path.compare(path.size() - sw.size(), sw.size(), sw) == 0) {
                const char* wrapper =
                    "local sw = settingsWrapper or SettingsWrapper\n"
                    "if sw == nil then return end\n"
                    "if sw.getAvailableStarCoins == nil then\n"
                    "    function sw:getAvailableStarCoins() return settings.starCoins or 1000 end\n"
                    "    function sw:setAvailableStarCoins(val) settings.starCoins = val end\n"
                    "    function sw:getAvailableStars() return settings.stars or 1000 end\n"
                    "    function sw:setAvailableStars(val) settings.stars = val end\n"
                    "end\n"
                    "for _, name in _G.ipairs({\"setHatcheryLocalTime\", \"setHatcheryState\", \"setHatcheryLastSavedServerTime\"}) do\n"
                    "    if sw[name] == nil then\n"
                    "        local key = \"hatchery\" .. _G.string.sub(name, 12)\n"
                    "        sw[name] = function(self, value) settings[key] = value end\n"
                    "    end\n"
                    "end\n";
                if (luaL_loadstring(L, wrapper) == 0) {
                    PushEnv(L);
                    lua_setfenv(L, -2);
                    if (lua_pcall(L, 0, 0, 0) != 0) { P(L)->Print(std::string("settingsWrapper additions: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
                } else { P(L)->Print(std::string("settingsWrapper additions: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
            }
            // debug build: this copy of hatchery.lua has the WORLD_VIEW link commented out in init, but enter() still goes there
            const std::string hl = "hatchery/hatchery.lua";
            if (!std::getenv("AB_NODEBUGKEYS") && path.size() >= hl.size() && path.compare(path.size() - hl.size(), hl.size(), hl) == 0) {
                const char* link =
                    "local H = Hatchery\n"
                    "local init = H.init\n"
                    "function H.init(...)\n"
                    "    init(...)\n"
                    "    if H.hatchery:getLink(\"WORLD_VIEW\") == nil then\n"
                    "        H.hatchery:addLink(\"WORLD_VIEW\", H.WorldView:new())\n"
                    "        H.hatchery:getLink(\"WORLD_VIEW\"):setHatchery(H.hatchery)\n"
                    "    end\n"
                    "end\n";
                if (luaL_loadstring(L, link) == 0) {
                    PushEnv(L);
                    lua_setfenv(L, -2);
                    if (lua_pcall(L, 0, 0, 0) != 0) { P(L)->Print(std::string("hatchery link: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
                } else { P(L)->Print(std::string("hatchery link: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
            }
            // debug build: the Hatchery's shop log is drawn for the iPad's 1024 pixel wide screen; on a wider window its buttons are centred on the log
            const std::string wsp = "hatchery/scripts/UI/WorldSelectionPanel.lua";
            if (!std::getenv("AB_NODEBUGKEYS") && path.size() >= wsp.size() && path.compare(path.size() - wsp.size(), wsp.size(), wsp) == 0) {
                const char* shop =
                    "local relocate, layout = WorldSelectionPanel.relocateItemButtons, WorldSelectionPanel.layout\n"
                    "function WorldSelectionPanel:relocateItemButtons()\n"
                    "    relocate(self)\n"
                    "    local shift = (gamelua.screenWidth - 1024) * 0.5\n"
                    "    for _, button in _G.ipairs(self.objectButtons) do button.x = button.x - shift end\n"
                    "end\n"
                    "function WorldSelectionPanel:layout()\n"
                    "    layout(self)\n"
                    "    self:getChild(\"purchaseButton\").x = gamelua.screenWidth * 0.5 - 512 + 0.08 * 1024\n"
                    "end\n";
                if (luaL_loadstring(L, shop) == 0) {
                    lua_pushvalue(L, 2);
                    lua_setfenv(L, -2);
                    if (lua_pcall(L, 0, 0, 0) != 0) { P(L)->Print(std::string("shop layout: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
                } else { P(L)->Print(std::string("shop layout: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
            }
            // debug build: the iPad's in-game bird selector hangs its button at the screen's corner with an iPad-only sprite; here it sits after the
            // pause button (and the restart button next to it) and is only there while the pause button is
            const std::string bs = "hatchery/scripts/UI/BirdSelector.lua";
            if (!std::getenv("AB_NODEBUGKEYS") && path.size() >= bs.size() && path.compare(path.size() - bs.size(), bs.size(), bs) == 0) {
                const char* selector =
                    "local init, layout, update, draw = BirdSelector.init, BirdSelector.layout, BirdSelector.update, BirdSelector.draw\n"
                    "function BirdSelector:init()\n"
                    "    init(self)\n"
                    "    local button = self:getChild(\"button\")\n"
                    "    self.hiddenYPos = -250\n"
                    "    if self.state == \"HIDDEN\" then self.y = self.hiddenYPos end\n"
                    "    if _G.res.getSpriteBounds(\"\", button.image) == 0 then button:setImage(\"H_GAME_OBJECT_EGG_1\") end\n"
                    "    button.scaleX = 0.75\n"
                    "    button.scaleY = 0.75\n"
                    "end\n"
                    "function BirdSelector:layout()\n"
                    "    layout(self)\n"
                    "    local button = self:getChild(\"button\")\n"
                    "    local hud = gamelua.menuManager.currentRoot\n"
                    "    local pause = hud and hud.name == \"gameHud\" and hud:getChild(\"pauseButton\")\n"
                    "    local restart = pause and hud:getChild(\"restart_button\")\n"
                    "    local x, y = 113, -53\n"
                    "    if pause then\n"
                    "        y = pause.y\n"
                    "        x = pause.x + 2 * ((restart and restart.x or pause.x + 104) - pause.x)\n"
                    "    end\n"
                    "    button.x = x - self.x\n"
                    "    button.y = y - self.y + 50\n"
                    "end\n"
                    "function BirdSelector:draw(x, y)\n"
                    "    draw(self, x, y)\n"
                    "end\n"
                    "local addDot = gamelua.addToTrajectory\n"
                    "function gamelua.addToTrajectory(i, x, y)   -- the first dot of a Hatchery bird's shot sets that bird's trail, as the iPad's launch code does\n"
                    "    local bird = i == 1 and gamelua.flyingBird or (gamelua.otherBirds and gamelua.otherBirds[i - 1] and gamelua.objects.world[gamelua.otherBirds[i - 1]])\n"
                    "    if bird and bird.hatcheryBird and bird.hatcheryBird.getTrajectorySprites then\n"
                    "        setCustomTrajectorySprites(bird.hatcheryBird:getTrajectorySprites())\n"
                    "    end\n"
                    "    addDot(i, x, y)\n"
                    "end\n"
                    "local eagle = gamelua.startMightyEagleFromHatchery\n"
                    "function gamelua.startMightyEagleFromHatchery()\n"
                    "    if gamelua.eagleBaitLaunched == true or gamelua.eagleUsedInCurrentLevel == true then return end   -- the eagle comes once per level, like retail\n"
                    "    -- the bench's eagle button works on the PC build (isEagleEnabled is false there) and without the eagle having been bought\n"
                    "    for name, fill in _G.pairs({\n"
                    "        MightyEagleBird = {sprite = \"BIRD_MIGHTY_EAGLE_MOTION\", spriteFlying = \"BIRD_MIGHTY_EAGLE_RADIAL\", spriteScore = \"BIRD_MIGHTY_EAGLE_RADIAL\",\n"
                    "                           type = \"circle\", friction = 0.1, allowRoll = true, density = 100, strength = 60000},\n"
                    "        BaitSardine = {sprite = \"BAIT_SARDINE\", spriteScore = \"BAIT_SARDINE\", allowRoll = true, density = 1, strength = 1}}) do\n"
                    "        local block = gamelua.blockTable.blocks[name]   -- the PC data's blocks lack what the iPad's blocks.lua gives the eagle and the sardine\n"
                    "        for field, value in _G.pairs(fill) do if block[field] == nil then block[field] = value end end\n"
                    "    end\n"
                    "    local enabled = gamelua.isEagleEnabled\n"
                    "    gamelua.isEagleEnabled = function() return true end\n"
                    "    eagle()\n"
                    "    gamelua.isEagleEnabled = enabled\n"
                    "end\n"
                    "function BirdSelector:update(dt, time)\n"
                    "    update(self, dt, time)\n"
                    "    if self.state == \"HIDDEN\" then\n"
                    "        local hud = gamelua.menuManager.currentRoot\n"
                    "        local pause = hud and hud.name == \"gameHud\" and hud:getChild(\"pauseButton\")\n"
                    "        self:getChild(\"button\").visible = pause ~= nil and pause ~= false and pause.visible == true and gamelua.eagleBaitLaunched ~= true\n"
                    "    end\n"
                    "    local hud = gamelua.menuManager.currentRoot\n"
                    "    local restart = hud and hud.name == \"gameHud\" and hud:getChild(\"restart_button\")\n"
                    "    if restart then   -- the restart button sits between the pause button and this one\n"
                    "        local pause = hud:getChild(\"pauseButton\")\n"
                    "        restart.x, restart.y = pause.x + 164.5, pause.y + 62\n"
                    "    end\n"
                    "end\n";
                if (luaL_loadstring(L, selector) == 0) {
                    lua_pushvalue(L, 2);
                    lua_setfenv(L, -2);
                    if (lua_pcall(L, 0, 0, 0) != 0) { P(L)->Print(std::string("bird selector: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
                } else { P(L)->Print(std::string("bird selector: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
            }
            // debug build: the iPad's ChallengePage.lua calls ChallengeFrame:storeViewedChallenges() on entry, but the function is inside a comment there
            const std::string cp = "challenges/ChallengePage.lua";
            if (!std::getenv("AB_NODEBUGKEYS") && path.size() >= cp.size() && path.compare(path.size() - cp.size(), cp.size(), cp) == 0) {
                const char* store = "if ChallengeFrame and ChallengeFrame.storeViewedChallenges == nil then function ChallengeFrame:storeViewedChallenges() end end\n";
                if (luaL_loadstring(L, store) == 0) {
                    PushEnv(L);
                    lua_setfenv(L, -2);
                    if (lua_pcall(L, 0, 0, 0) != 0) { P(L)->Print(std::string("challenge page: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
                } else { P(L)->Print(std::string("challenge page: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
            }
            // debug build: the Challenges badge and the Hatchery button of the episode selection are drawn with the iPad's (larger) sprites; they are
            // made smaller and moved apart so they do not overlap each other or the first episode
            const std::string es = "menus/episode_selection.lua";
            if (!std::getenv("AB_NODEBUGKEYS") && path.size() >= es.size() && path.compare(path.size() - es.size(), es.size(), es) == 0) {
                const char* episodes =
                    "local layout = EpisodeScrollFrame.layout\n"
                    "function EpisodeScrollFrame:layout()\n"
                    "    layout(self)\n"
                    "    local challenge, hatchery = self:getChild(\"challengeButton\"), self:getChild(\"hatcheryButton\")\n"
                    "    if challenge then challenge.scaleX = 0.75; challenge.scaleY = 0.75; challenge.x = challenge.x + 20 end\n"
                    "    if hatchery then hatchery.scaleX = 0.65; hatchery.scaleY = 0.65; hatchery.x = hatchery.x + 35 end\n"
                    "end\n";
                if (luaL_loadstring(L, episodes) == 0) {
                    PushEnv(L);
                    lua_setfenv(L, -2);
                    if (lua_pcall(L, 0, 0, 0) != 0) { P(L)->Print(std::string("episode selection: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
                } else { P(L)->Print(std::string("episode selection: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
            }
            // debug build: the buttons of the iPad's BuyStarsDialog are empty planks (the amounts were drawn by the App Store layer); the amounts are written on them
            const std::string bsd = "UI/BuyStarsDialog.lua";
            if (!std::getenv("AB_NODEBUGKEYS") && path.size() >= bsd.size() && path.compare(path.size() - bsd.size(), bsd.size(), bsd) == 0) {
                const char* amounts =
                    "local init, layout = BuyStarsDialog.init, BuyStarsDialog.layout\n"
                    "function BuyStarsDialog:init()\n"
                    "    init(self)\n"
                    "    for _, v in _G.pairs(self.packages) do\n"
                    "        local amount = ui.Text:new()\n"
                    "        amount.name = \"amount\" .. v\n"
                    "        amount.hanchor = \"HCENTER\"\n"
                    "        amount.vanchor = \"VCENTER\"\n"
                    "        amount.text = \"\" .. v\n"
                    "        self:addChild(amount)\n"
                    "    end\n"
                    "end\n"
                    "function BuyStarsDialog:layout()\n"
                    "    layout(self)\n"
                    "    for _, v in _G.pairs(self.packages) do\n"
                    "        local button, amount = self:getChild(\"packageButton\" .. v), self:getChild(\"amount\" .. v)\n"
                    "        amount.x, amount.y = button.x, button.y\n"
                    "    end\n"
                    "end\n";
                if (luaL_loadstring(L, amounts) == 0) {
                    lua_pushvalue(L, 2);
                    lua_setfenv(L, -2);
                    if (lua_pcall(L, 0, 0, 0) != 0) { P(L)->Print(std::string("buy stars: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
                } else { P(L)->Print(std::string("buy stars: ") + lua_tostring(L, -1)); lua_pop(L, 1); }
            }
            // debug build: options.lua has just switched Hatchery and Challenges off; they come back on when their scripts are in the data
            // folder (hatchery/hatchery.lua and scripts/challenges/challenges.lua, taken from the iPad version)
            const std::string opt = "options.lua";
            if (path.size() >= opt.size() && path.compare(path.size() - opt.size(), opt.size(), opt) == 0) {
                bool debug = !std::getenv("AB_NODEBUGKEYS");   // the same switch as the debug keys (script_host.cpp)
                std::vector<uint8_t> probe;
                std::string dir = path.substr(0, path.size() - opt.size());
                const char* flags[] = {"g_hatcheryEnabled", "g_challengesEnabled"};
                std::string files[] = {"hatchery/hatchery.lua", dir + "challenges/challenges.lua"};
                for (int i = 0; i < 2 && debug; i++) {
                    bool have = ab::ReadLuaFile(files[i], probe);
                    if (!have) { std::string alt = P(L)->ResolveDataFile(files[i]); have = alt != files[i] && ab::ReadLuaFile(alt, probe); }
                    if (!have) continue;
                    PushEnv(L);
                    lua_pushboolean(L, 1);
                    lua_setfield(L, -2, flags[i]);
                    lua_pop(L, 1);
                    if (i == 0) {   // the iPad gamelogic switches the in-game bird selector on next to g_hatcheryEnabled
                        PushEnv(L);
                        lua_pushboolean(L, 1);
                        lua_setfield(L, -2, "g_hatcheryEnableBirdSelector");
                        lua_pop(L, 1);
                    }
                    P(L)->Print(std::string("debug build: ") + flags[i] + " on");
                }
            }
        }
    }
    if (rc != 0) {
        P(L)->Print("loadLuaFileToObject failed: " + path + ": " + std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
    }
    return 0;
}

// ---- files
// blocks.lua / particles.lua are re-read on every loadLevel so the tables start fresh
void ReloadTable(lua_State* L, const char* name, const char* file) {
    PushEnv(L);
    lua_getfield(L, -1, "scriptPath");
    std::string path = std::string(lua_isstring(L, -1) ? lua_tostring(L, -1) : "scripts") + file;
    lua_pop(L, 2);
    std::string err;
    if (!ab::LoadTableFile(L, name, path, &err, false)) P(L)->Print("loadLevel: " + err);
}

// Angry Birds Rio has no blocks.lua: the exe's loadLevel reads materials, damage factors, pigs, level goals, score
// objects, birds and the blocks_*.lua files, the last ones all into blockTable, then themes and groups.
bool HasSingleBlocksFile(lua_State* L) {
    PushEnv(L);
    lua_getfield(L, -1, "scriptPath");
    std::string path = std::string(lua_isstring(L, -1) ? lua_tostring(L, -1) : "scripts") + "/blocks.lua";
    lua_pop(L, 2);
    std::vector<uint8_t> bytes;
    return ab::ReadLuaFile(path, bytes) && !bytes.empty();
}

// Angry Birds Space (PC): its scripts load the block files themselves (loadBlockData -> loadLuaFile), so the engine keeps its hands off
bool IsSpaceData(lua_State* L) {
    PushEnv(L);
    lua_getfield(L, -1, "scriptPath");
    std::string path = std::string(lua_isstring(L, -1) ? lua_tostring(L, -1) : "scripts") + "/blocks_planets.lua";
    lua_pop(L, 2);
    std::vector<uint8_t> bytes;
    return ab::ReadLuaFile(path, bytes) && !bytes.empty();
}

void ReloadAllTables(lua_State* L) {
    if (IsSpaceData(L)) return;
    if (HasSingleBlocksFile(L)) {
        ReloadTable(L, "blockTable", "/blocks.lua");
        ReloadTable(L, "particleTable", "/particles.lua");
        return;
    }
    ab::LoadRioTables(L);
}


// loadLevel(file) (0x40d2c0): the level file runs in a fresh, empty environment table; that table
// becomes the global `loadedObjects`. A level that cannot be read leaves loadedObjects nil, which
// the scripts handle ("level was not found").
int l_loadLevel(lua_State* L) {
    std::string file = S(L, 1);
    P(L)->BeginLevelLoad();
    ReloadAllTables(L);

    bool ok = false;
    lua_newtable(L);                                    // the level's environment
    if (file.size() < 4 || file.compare(file.size() - 4, 4, ".lua") != 0) file += ".lua";   // the scripts leave it off
    std::string path = P(L)->ResolveDataFile(file);
    // The exe decrypts the file (AES-256-CBC) before running it; a plain-text level is used as it is.
    std::vector<uint8_t> bytes;
    int loaded = 1;
    std::string loadError = "No such file or directory";
    if (file.find(".temp") == std::string::npos || !P(L)->FindSavedLevel(file, bytes)) ab::ReadLuaFile(path, bytes);   // the editor's TAB copy comes from memory
    if (const char* dump = std::getenv("AB_DUMPLEVEL")) { if (FILE* f = std::fopen((std::string(dump) + "." + file.substr(file.find_last_of("/\\") + 1)).c_str(), "wb")) { std::fwrite(bytes.data(), 1, bytes.size(), f); std::fclose(f); } }   // debug: the decrypted level text
    if (!bytes.empty()) {
        loaded = luaL_loadbuffer(L, (const char*)bytes.data(), bytes.size(), path.c_str());
        if (loaded != 0) { loadError = lua_tostring(L, -1); lua_pop(L, 1); }
    }
    if (loaded == 0) {
        lua_pushvalue(L, -2);
        lua_setfenv(L, -2);
        if (lua_pcall(L, 0, 0, 0) == 0) ok = true;
        else { P(L)->Print("loadLevel: " + std::string(lua_tostring(L, -1))); lua_pop(L, 1); }
    } else {
        P(L)->Print("loadLevel: cannot read " + path + ": " + loadError);
    }
    if (!ok) { lua_pop(L, 1); lua_pushnil(L); }
    else {
        // Space (0x486b00 / 0x486c50): the gravity force multiplier of the world is the level's own `gravityForceMultiplier` when the file has one
        // (1 in some levels), otherwise the global `defaultGravityForceMultiplier` of worldAttributes.lua (4). Without this every level pulled with 4.
        float gm = 0; bool haveGm = false;
        lua_getfield(L, -1, "gravityForceMultiplier");
        if (lua_isnumber(L, -1)) { gm = (float)lua_tonumber(L, -1); haveGm = true; }
        lua_pop(L, 1);
        if (!haveGm) {
            PushEnv(L);
            lua_getfield(L, -1, "defaultGravityForceMultiplier");
            if (lua_isnumber(L, -1)) { gm = (float)lua_tonumber(L, -1); haveGm = true; }
            lua_pop(L, 2);
        }
        if (haveGm) { float r = 0; P(L)->BodyCommand("", 8, gm, r); }
        // The scripts empty loadedObjects once they have taken what they use. The editor's saveLevel writes back what the file
        // held that no script keeps (trainCarts, weather...), so the top-level entries stay in the registry.
        lua_newtable(L);
        lua_pushnil(L);
        while (lua_next(L, -3)) {
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_settable(L, -5);
            lua_pop(L, 1);
        }
        lua_setfield(L, LUA_REGISTRYINDEX, "ab.lastLevel");
    }
    PushEnv(L);
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, "loadedObjects");
    lua_pop(L, 2);
    P(L)->EndLevelLoad();
    lua_pushboolean(L, ok);
    EmitHook(L, "levelLoaded", file.c_str());
    return 1;
}
// loadLevelEditor(file) (Seasons, 0x40db30): the editor asks for the plain-text copy of a level before it runs loadLevelInternal.
// ReadLuaFile already takes plain and encrypted levels alike, so all that is left is to report a level that is missing.
int l_loadLevelEditor(lua_State* L) {
    std::string file = S(L, 1);
    if (file.size() < 4 || file.compare(file.size() - 4, 4, ".lua") != 0) file += ".lua";
    std::vector<uint8_t> bytes;
    ab::ReadLuaFile(P(L)->ResolveDataFile(file), bytes);
    if (bytes.empty()) P(L)->Print("loadLevelEditor: cannot read " + P(L)->ResolveDataFile(file));
    lua_pushboolean(L, !bytes.empty());
    return 1;
}
int l_saveLevel(lua_State* L) { P(L)->SaveLevel(S(L, 1)); return 0; }
// saveLevelForReload(name) (Space, 0x46b850): the level as it is now (live positions and angles, what the player has destroyed) is saved under `name` and kept as
// objects.previousLevel, the table loadLevelInternal takes as the level when the player comes back from an eggsteroid or a bonus level (it is `loadedObjects` then).
int l_saveLevelForReload(lua_State* L) {
    std::string name = S(L, 1);
    P(L)->SaveLevel(name);
    std::string file = name;
    if (file.size() < 4 || file.compare(file.size() - 4, 4, ".lua") != 0) file += ".lua";
    std::vector<uint8_t> bytes;
    PushEnv(L);
    lua_getfield(L, -1, "objects");
    int objects = lua_gettop(L);
    if (lua_istable(L, objects) && P(L)->FindSavedLevel(file, bytes) && !bytes.empty()) {
        lua_newtable(L);                                    // the level's environment, as loadLevel makes it
        if (luaL_loadbuffer(L, (const char*)bytes.data(), bytes.size(), file.c_str()) == 0) {
            lua_pushvalue(L, -2);
            lua_setfenv(L, -2);
            if (lua_pcall(L, 0, 0, 0) != 0) { P(L)->Print("saveLevelForReload: " + std::string(lua_tostring(L, -1))); lua_pop(L, 1); lua_newtable(L); lua_replace(L, -2); }
        } else { P(L)->Print("saveLevelForReload: " + std::string(lua_tostring(L, -1))); lua_pop(L, 1); }
        // The saved text holds what the physics knows (name, definition, position, angle). What the script keeps about an object (the radius, size and
        // vertices it was created with, a planet's forces and filters) is put back from the live table, so the level loads again as it was made.
        {
            int prev = lua_gettop(L);
            lua_getfield(L, prev, "world");
            lua_getfield(L, objects, "world");
            int live = lua_gettop(L), saved = live - 1;
            static const char* keep[] = {"radius", "width", "height", "scaleX", "scaleY", "scale", "vertices", "gravitationMinForce", "gravitationMaxForce",
                                         "forceAngle", "gravityCollisionMask", "gravityFilterCategory"};
            if (lua_istable(L, saved) && lua_istable(L, live)) {
                lua_pushnil(L);
                while (lua_next(L, saved)) {
                    if (lua_istable(L, -1) && lua_type(L, -2) == LUA_TSTRING) {
                        int entry = lua_gettop(L);
                        lua_pushvalue(L, entry - 1);
                        lua_gettable(L, live);
                        if (lua_istable(L, -1)) {
                            int src = lua_gettop(L);
                            for (const char* k : keep) {
                                lua_getfield(L, entry, k);
                                bool missing = lua_isnil(L, -1);
                                lua_pop(L, 1);
                                if (!missing) continue;
                                lua_getfield(L, src, k);
                                if (lua_isnumber(L, -1) || lua_istable(L, -1) || lua_isstring(L, -1)) lua_setfield(L, entry, k); else lua_pop(L, 1);
                            }
                        }
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);
                }
            }
            lua_settop(L, prev);
        }
        lua_setfield(L, objects, "previousLevel");
    } else if (lua_istable(L, objects)) {
        lua_newtable(L);
        lua_setfield(L, objects, "previousLevel");
    }
    lua_settop(L, 0);
    return 0;
}
int l_createDirectory(lua_State* L) { P(L)->CreateDirectory(S(L, 1)); return 0; }
int l_checkDirectory(lua_State* L) { lua_pushboolean(L, P(L)->CheckDirectory(S(L, 1))); return 1; }
int l_checkForLuaFile(lua_State* L) { lua_pushboolean(L, P(L)->CheckForLuaFile(S(L, 1))); return 1; }

// Writes `value` (on top of the stack) as Lua source.
void Serialize(lua_State* L, std::string& out, int depth) {
    switch (lua_type(L, -1)) {
        case LUA_TNUMBER: { char b[64]; std::snprintf(b, sizeof b, "%.9g", (double)lua_tonumber(L, -1)); out += b; break; }
        case LUA_TBOOLEAN: out += lua_toboolean(L, -1) ? "true" : "false"; break;
        case LUA_TSTRING: {
            size_t len; const char* s = lua_tolstring(L, -1, &len);
            out += '"';
            for (size_t i = 0; i < len; i++) {
                unsigned char c = (unsigned char)s[i];
                if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c < 32) { char b[8]; std::snprintf(b, sizeof b, "\\%03d", c); out += b; }
                else out += (char)c;
            }
            out += '"';
            break;
        }
        case LUA_TTABLE: {
            if (depth > 32) { out += "nil"; break; }
            out += "{";
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                int kt = lua_type(L, -2), vt = lua_type(L, -1);
                if ((kt == LUA_TSTRING || kt == LUA_TNUMBER) &&
                    (vt == LUA_TNUMBER || vt == LUA_TBOOLEAN || vt == LUA_TSTRING || vt == LUA_TTABLE)) {
                    out += "[";
                    lua_pushvalue(L, -2);          // copy the key so lua_next keeps working
                    Serialize(L, out, depth + 1);
                    lua_pop(L, 1);
                    out += "]=";
                    Serialize(L, out, depth + 1);
                    out += ",";
                }
                lua_pop(L, 1);
            }
            out += "}";
            break;
        }
        default: out += "nil"; break;
    }
}

bool IsSpaceData(lua_State* L);
// saveLuaFile(fileName, tableName, appData): writes `tableName = <global tableName>` to fileName
int SaveLuaFileImpl(lua_State* L, const std::string& file, const std::string& name, bool appData) {
    PushEnv(L);
    lua_getfield(L, -1, name.c_str());
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
    std::string path = P(L)->ResolveUserFile(file, appData);
    // an existing original file stays in its format: one `key = value` line per entry, encrypted if it was
    // (some originals are not encrypted: their text starts with an entry, not with `tableName = {`)
    std::string old;
    bool encrypted = false;
    const char* saveKey = ab::kSaveKey;
    ab::SetSaveFamilySpace(IsSpaceData(L));
    ab::ReadSaveFile(path, old, encrypted, &saveKey);   // 1.x, Seasons and Space saves have their own keys
    static int isSpace = -1, isStarWars = -1;
    if (isSpace < 0) {
        isSpace = IsSpaceData(L) ? 1 : 0;
        std::vector<uint8_t> probe;      // the Star Wars scripts have blocks_tatooine.lua; its save files have a key of their own
        PushEnv(L); lua_getfield(L, -1, "scriptPath");
        std::string sp = std::string(lua_isstring(L, -1) ? lua_tostring(L, -1) : "scripts") + "/blocks_tatooine.lua";
        lua_pop(L, 2);
        isStarWars = (ab::ReadLuaFile(sp, probe) && !probe.empty()) ? 1 : 0;
    }
    // Angry Birds Space and Star Wars: every save is encrypted with the game's own key (0x496180 in Space); the table is written one entry per line, which for settings is `root = {...}`
    if (isSpace) { encrypted = true; saveKey = isStarWars ? ab::kStarWarsSaveKey : ab::kSpaceSaveKey; }
    size_t start = old.find_first_not_of(" \t\r\n");
    bool original = encrypted || (start != std::string::npos && old.compare(start, name.size() + 3, name + " = ") != 0);
    std::string text;
    lua_getfield(L, -1, "root");
    bool hasRoot = isSpace && lua_istable(L, -1);
    if (!hasRoot) lua_pop(L, 1);
    if (hasRoot) {                                      // Space: settings.root and friends are the whole file, written as `root = {...}`
        text = "root = ";
        Serialize(L, text, 0);
        text += "\n";
        lua_pop(L, 1);
    } else if (original) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                text += lua_tostring(L, -2);
                text += " = ";
                Serialize(L, text, 0);
                text += "\n";
            }
            lua_pop(L, 1);
        }
    } else {
        text = name + " = ";
        Serialize(L, text, 0);
        text += "\n";
    }
    lua_pop(L, 1);
    std::vector<uint8_t> bytes(text.begin(), text.end());
    if (original) {
        std::string backup = path + ".original-backup";        // the first overwrite keeps the file the game wrote
        if (FILE* b = std::fopen(backup.c_str(), "rb")) std::fclose(b);
        else if (FILE* in = std::fopen(path.c_str(), "rb")) {
            if (FILE* b = std::fopen(backup.c_str(), "wb")) {
                int c;
                while ((c = std::fgetc(in)) != EOF) std::fputc(c, b);
                std::fclose(b);
            }
            std::fclose(in);
        }
        if (encrypted) {
            std::vector<uint8_t> enc;
            ab::AesEncryptCbc((const uint8_t*)saveKey, bytes, enc);
            bytes = enc;
        }
    }
    if (!ab::WriteFileSafely(path, bytes)) P(L)->Print("saveLuaFile: cannot write " + path);
    return 0;
}
int l_saveLuaFile(lua_State* L) { return SaveLuaFileImpl(L, S(L, 1), S(L, 2), B(L, 3)); }
// savePersistentLuaFile(fileName, tableName): the same file in the user data (highscores.lua, bi_data.lua)
int l_savePersistentLuaFile(lua_State* L) { return SaveLuaFileImpl(L, S(L, 1), S(L, 2), true); }

// Hatchery: the saved world (tileMapSave.lua) and egg sprite list (savedEggSprites.lua) are files in the user data written by saveLuaFile
int l_checkForPersistentFile(lua_State* L) {
    std::string path = P(L)->ResolveUserFile(S(L, 1), true);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f) std::fclose(f);
    lua_pushboolean(L, f != nullptr);
    return 1;
}
// returns the table the file defines (`name = { ... }`)
int l_loadPersistentFileWrapper(lua_State* L) {
    std::string path = P(L)->ResolveUserFile(S(L, 1), true), text;
    bool encrypted = false;
    const char* key = ab::kSaveKey;
    if (!ab::ReadSaveFile(path, text, encrypted, &key) || luaL_loadbuffer(L, text.data(), text.size(), ("@" + path).c_str()) != 0) {
        if (lua_gettop(L) > 1 && lua_isstring(L, -1)) P(L)->Print("loadPersistentFileWrapper: " + std::string(lua_tostring(L, -1)));
        lua_settop(L, 0);
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);                                   // the file's own globals
    lua_pushvalue(L, -1);
    lua_setfenv(L, -3);
    int fenv = lua_gettop(L);
    lua_pushvalue(L, 2);
    if (lua_pcall(L, 0, 0, 0) != 0) {
        lua_settop(L, 0);
        lua_pushnil(L);
        return 1;
    }
    lua_pushnil(L);
    while (lua_next(L, fenv)) {
        if (lua_istable(L, -1)) { lua_remove(L, -2); lua_remove(L, fenv); return 1; }
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    return 1;
}

// Hatchery egg painter canvas
int l_initializePainter(lua_State* L) { P(L)->CanvasInit((int)N(L, 2), (int)N(L, 3)); return 0; }
int l_clearCanvas(lua_State* L) { P(L)->CanvasClear(N(L, 1), N(L, 2), N(L, 3), N(L, 4)); return 0; }
int l_paintToCanvas(lua_State* L) { P(L)->CanvasPaint(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5)); return 0; }
int l_drawCanvasWithMask(lua_State* L) { P(L)->CanvasDraw(S(L, 3)); return 0; }
int l_getDominantCanvasColors(lua_State* L) {
    std::string first, second;
    P(L)->CanvasDominant(first, second);
    lua_pushstring(L, first.c_str());
    lua_pushstring(L, second.c_str());
    return 2;
}
int l_createSpriteFromCanvas(lua_State* L) { lua_pushboolean(L, P(L)->CanvasMakeSprite(S(L, 1), (int)N(L, 2), (int)N(L, 3), (int)N(L, 4), (int)N(L, 5), B(L, 6), S(L, 7))); return 1; }
// setupCompoObject(name, items, scale): items = { {sprite=, x=, y=, scale=, angle=}, ... }
int l_setupCompoObject(lua_State* L) {
    std::vector<Platform::CompoItem> items;
    if (lua_istable(L, 2)) {
        int n = (int)lua_objlen(L, 2);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, 2, i);
            if (lua_istable(L, -1)) {
                Platform::CompoItem item;
                lua_getfield(L, -1, "sprite"); if (lua_isstring(L, -1)) item.sprite = lua_tostring(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "x"); if (lua_isnumber(L, -1)) item.x = (float)lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "y"); if (lua_isnumber(L, -1)) item.y = (float)lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "scale"); if (lua_isnumber(L, -1)) item.scale = (float)lua_tonumber(L, -1); lua_pop(L, 1);
                lua_getfield(L, -1, "angle"); if (lua_isnumber(L, -1)) item.angle = (float)lua_tonumber(L, -1); lua_pop(L, 1);
                items.push_back(item);
            }
            lua_pop(L, 1);
        }
    }
    P(L)->SetupCompoObject(S(L, 1), items, lua_isnumber(L, 3) ? (float)lua_tonumber(L, 3) : 1.0f);
    return 0;
}
// createSilhouette(newName, sourceSprite, r, g, b, a [, outlineWidth]) -> number of pixels in the shape, or nil
//   Makes a new sprite called newName from the shape of an existing sprite: every pixel that sourceSprite draws (its alpha is not 0) is painted in one
//   colour, and everything it leaves clear stays clear, so a pig becomes a flat pig-shaped patch, a bird a bird-shaped shadow. The source can be any
//   sprite a sheet knows, with its pivot, and it keeps its size; use the new name anywhere a sprite name goes (setSprite, drawSprite, an object's
//   sprite, a compo part). Calling it again with the same newName replaces the earlier sprite.
//   r, g, b, a is the colour. Give each as 0 to 1 (1, 1, 1 white; 0, 0, 0 black), or as 0 to 255 when any value is above 1. The pixel's own alpha is
//   multiplied by a, so soft edges of the source stay soft and a = 0.5 makes a half-see-through shadow. A colour left out is 1.
//   outlineWidth (optional, default 0): 0 fills the whole shape. A number of pixels above 0 keeps only the border of the shape, that many pixels wide,
//   measured inwards from every edge (including the edges of holes), which gives an outline sprite to draw behind or over the original for a glow or a
//   selection frame.
//   Returns how many pixels the shape has (it does not change with the outline width), or nil when the source sprite cannot be found. The sprite is
//   made when the call runs: a sheet that is released later still leaves the silhouette in place.
int l_createSilhouette(lua_State* L) {
    int n = P(L)->CreateSilhouette(S(L, 1), S(L, 2), lua_isnoneornil(L, 3) ? 1.0f : N(L, 3), lua_isnoneornil(L, 4) ? 1.0f : N(L, 4),
                                   lua_isnoneornil(L, 5) ? 1.0f : N(L, 5), lua_isnoneornil(L, 6) ? 1.0f : N(L, 6), lua_isnoneornil(L, 7) ? 0 : (int)N(L, 7));
    if (n < 0) lua_pushnil(L); else lua_pushinteger(L, n);
    return 1;
}
int l_loadCanvasSprite(lua_State* L) { lua_pushboolean(L, P(L)->CanvasLoadSprite(S(L, 1), S(L, 2), (int)N(L, 3), (int)N(L, 4))); return 1; }

// ---- physics
// objects.world / objects.joints (the exe keeps these tables as `Game+0x1a4`'s fields)
bool PushObjectsTable(lua_State* L, const char* which) {
    PushEnv(L);
    lua_getfield(L, -1, "objects");
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
    lua_getfield(L, -1, which);
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
    return true;
}
void SetNum(lua_State* L, const char* k, float v) { lua_pushnumber(L, v); lua_setfield(L, -2, k); }
void SetBool(lua_State* L, const char* k, bool v) { lua_pushboolean(L, v); lua_setfield(L, -2, k); }
void SetStr(lua_State* L, const char* k, const std::string& v) { lua_pushlstring(L, v.data(), v.size()); lua_setfield(L, -2, k); }

// objects.world[name] = { name, sprite, type, x, y, width, height | radius, angle, density, friction,
//                         restitution, mass, xVel, yVel, z_order }   (key order of the exe's helper)
void WriteWorldEntry(lua_State* L, const BodyDef& d, const char* type, float mass) {
    if (!PushObjectsTable(L, "world")) return;
    lua_newtable(L);
    SetStr(L, "name", d.name);
    SetStr(L, "sprite", d.sprite);
    SetStr(L, "type", type);
    SetNum(L, "x", d.x);
    SetNum(L, "y", d.y);
    if (std::string(type) == "circle") {
        SetNum(L, "radius", d.w);
    } else {
        SetNum(L, "width", d.w);
        SetNum(L, "height", d.h);
    }
    SetNum(L, "angle", 0);
    SetNum(L, "density", d.density);
    SetNum(L, "friction", d.friction);
    SetNum(L, "restitution", d.restitution);
    SetNum(L, "mass", mass);
    SetNum(L, "xVel", 0);
    SetNum(L, "yVel", 0);
    SetNum(L, "z_order", d.zOrder);
    // Angry Birds Rio's entries also carry the animation counters (the scripts count them down)
    SetNum(L, "animTimer", 0);
    SetNum(L, "animFrame", 0);
    SetNum(L, "animThresholdTimer", 0);
    lua_setfield(L, -2, d.name.c_str());
    lua_pop(L, 1);
}

BodyDef ReadBody(lua_State* L, bool circle) {
    BodyDef d;
    d.name = S(L, 1);
    d.sprite = S(L, 2);
    d.x = N(L, 3);
    d.y = N(L, 4);
    int i = 5;
    d.w = N(L, i++);
    if (!circle) d.h = N(L, i++);
    d.density = N(L, i++);
    d.friction = N(L, i++);
    d.restitution = N(L, i++);
    // Angry Birds Rio's circles take the collision flag as well (a boolean followed by the controllable boolean)
    if (!circle || lua_gettop(L) >= i + 2 || (lua_type(L, i) == LUA_TBOOLEAN && lua_type(L, i + 1) == LUA_TBOOLEAN)) d.collision = B(L, i++);   // Space's createCircle always has 11 arguments (the collision flag may be a sprite name: any non-nil value is true)
    d.controllable = B(L, i++);
    d.zOrder = N(L, i++);
    return d;
}
int l_createBox(lua_State* L) {
    BodyDef d = ReadBody(L, false);
    WriteWorldEntry(L, d, "box", P(L)->CreateBox(d));
    EmitHook(L, "objectCreated", d.name.c_str(), "box");
    return 0;
}
int l_createCircle(lua_State* L) {
    BodyDef d = ReadBody(L, true);
    WriteWorldEntry(L, d, "circle", P(L)->CreateCircle(d));
    EmitHook(L, "objectCreated", d.name.c_str(), "circle");
    return 0;
}
int l_createPolygon(lua_State* L) {
    BodyDef d = ReadBody(L, false);
    WriteWorldEntry(L, d, "polygon", P(L)->CreatePolygon(d, P(L)->Vertices()));
    EmitHook(L, "objectCreated", d.name.c_str(), "polygon");
    return 0;
}
// createLineShape (Star Wars): the same arguments as createPolygon; the vertices from addVertex become a chain of edges (0x439640)
int l_createLineShape(lua_State* L) {
    BodyDef d = ReadBody(L, false);
    WriteWorldEntry(L, d, "line", P(L)->CreateLine(d, P(L)->Vertices()));
    return 0;
}
// ---- Angry Birds Space compound bodies: the scripts clear the list, add one entry per part, then createCompoundObject makes the body
std::vector<CompoundShape> g_compoundShapes;
float g_compoundDamping = 0;
std::vector<std::pair<int, float>> g_compoundParams;   // setCompoundShapeParameter(shapes, id, value): applied to the object when createCompoundObject makes it
int CompoundAdd(lua_State* L, int kind, Platform* p) {
    CompoundShape cs;
    cs.kind = kind;
    cs.sprite = lua_isstring(L, 1) ? lua_tostring(L, 1) : "";
    cs.x = N(L, 2); cs.y = N(L, 3); cs.angle = N(L, 4);
    if (kind == 3) cs.w = N(L, 5);
    else { cs.w = N(L, 5); cs.h = N(L, 6); }
    if (kind == 1) cs.verts = p->Vertices();
    g_compoundShapes.push_back(cs);
    return 0;
}
int l_clearShapesArray(lua_State*) { g_compoundShapes.clear(); g_compoundParams.clear(); g_compoundDamping = 0; return 0; }
int l_addBoxToCompoundList(lua_State* L) { return CompoundAdd(L, 0, P(L)); }
int l_addPolygonToCompoundList(lua_State* L) { return CompoundAdd(L, 1, P(L)); }
int l_addLineToCompoundList(lua_State* L) { return CompoundAdd(L, 2, P(L)); }
int l_addCircleToCompoundList(lua_State* L) { return CompoundAdd(L, 3, P(L)); }
int l_setCompoundShapeLinearDamping(lua_State* L) { g_compoundDamping = N(L, 2); return 0; }
// setCompoundShapeParameter(shapes, id, value): the same ids as setObjectParameter (the scripts pass 5, the scale, and 15, disableImmovable). The parameter
// belongs to the compound that is being built, so it is kept until createCompoundObject has made the body and is then set on it, as the exe does.
int l_setCompoundShapeParameter(lua_State* L) { g_compoundParams.push_back({(int)N(L, 2), N(L, 3)}); return 0; }
// createCompoundObject(name, x, y, _, density, friction, restitution, collision, controllable, z_order)
int l_createCompoundObject(lua_State* L) {
    BodyDef d;
    d.name = S(L, 1); d.x = N(L, 2); d.y = N(L, 3);
    d.density = N(L, 5); d.friction = N(L, 6); d.restitution = N(L, 7);
    d.collision = lua_isnoneornil(L, 8) ? true : B(L, 8);
    d.controllable = B(L, 9); d.zOrder = N(L, 10);
    d.sprite = g_compoundShapes.empty() ? "" : g_compoundShapes[0].sprite;
    float mass = P(L)->CreateCompound(d, g_compoundShapes, g_compoundDamping);
    for (const auto& pr : g_compoundParams) P(L)->SetObjectParameter(d.name, pr.first, pr.second);
    WriteWorldEntry(L, d, "compound", mass);
    return 0;
}
int l_finalizeCompoundObject(lua_State*) { return 0; }
int l_clearVertices(lua_State* L) { P(L)->ClearVertices(); return 0; }
int l_addVertex(lua_State* L) { P(L)->AddVertex(N(L, 1), N(L, 2)); return 0; }

// The exe reads optional numbers only if the field is a number or a string, and optional flags only
// if the field is a real boolean (so `collideConnected = 0` counts as absent).
bool HasNum(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    bool has = lua_type(L, -1) == LUA_TNUMBER || lua_type(L, -1) == LUA_TSTRING;
    lua_pop(L, 1);
    return has;
}
bool BoolOr(lua_State* L, int t, const char* k, bool def) {
    lua_getfield(L, t, k);
    bool v = lua_type(L, -1) == LUA_TBOOLEAN ? lua_toboolean(L, -1) != 0 : def;
    lua_pop(L, 1);
    return v;
}
float NumOr(lua_State* L, int t, const char* k, float def) { return HasNum(L, t, k) ? FieldN(L, t, k) : def; }

// Angry Birds Space: createCompoundBodyFromTable(t) (0x48a690), the form gamelogic.lua uses for a compound it has assembled: name, x, y, angle, z_order,
// material (a table with density, friction and restitution) and children, one table per part with x, y, angle, blockData (type box / circle / polygon,
// width, height, radius, sprite) and, for a polygon, worldVertices. It builds its own part list, so the list of the add...ToCompoundList natives is not touched.
int l_createCompoundBodyFromTable(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    BodyDef d;
    d.name = FieldS(L, 1, "name");
    d.x = NumOr(L, 1, "x", 0); d.y = NumOr(L, 1, "y", 0);
    d.zOrder = NumOr(L, 1, "z_order", 0);
    d.density = 1; d.friction = 0.5f; d.restitution = 0.2f; d.collision = true; d.controllable = false;
    lua_getfield(L, 1, "material");
    if (lua_istable(L, -1)) {
        int m = lua_gettop(L);
        d.density = NumOr(L, m, "density", d.density); d.friction = NumOr(L, m, "friction", d.friction); d.restitution = NumOr(L, m, "restitution", d.restitution);
    }
    lua_pop(L, 1);
    std::vector<CompoundShape> shapes;
    lua_getfield(L, 1, "children");
    if (lua_istable(L, -1)) {
        for (int i = 1;; i++) {
            lua_rawgeti(L, -1, i);
            if (!lua_istable(L, -1)) { lua_pop(L, 1); break; }
            int c = lua_gettop(L);
            CompoundShape cs;
            cs.x = NumOr(L, c, "x", 0); cs.y = NumOr(L, c, "y", 0); cs.angle = NumOr(L, c, "angle", 0);
            lua_getfield(L, c, "blockData");
            if (lua_istable(L, -1)) {
                int b = lua_gettop(L);
                std::string type = Has(L, b, "type") ? FieldS(L, b, "type") : "";
                if (Has(L, b, "sprite")) cs.sprite = FieldS(L, b, "sprite");
                bool ok = true;
                if (type == "box") { cs.kind = 0; cs.w = NumOr(L, b, "width", 0); cs.h = NumOr(L, b, "height", 0); }
                else if (type == "circle") { cs.kind = 3; cs.w = NumOr(L, b, "radius", 0); }
                else if (type == "polygon") {
                    cs.kind = 1;
                    P(L)->ClearVertices();
                    lua_getfield(L, c, "worldVertices");
                    if (lua_istable(L, -1))
                        for (int k = 1;; k++) {
                            lua_rawgeti(L, -1, k);
                            if (!lua_istable(L, -1)) { lua_pop(L, 1); break; }
                            P(L)->AddVertex(NumOr(L, lua_gettop(L), "x", 0), NumOr(L, lua_gettop(L), "y", 0));
                            lua_pop(L, 1);
                        }
                    lua_pop(L, 1);
                    cs.verts = P(L)->Vertices();
                } else ok = false;
                if (ok) shapes.push_back(cs);
            }
            lua_pop(L, 2);      // blockData, child
        }
    }
    lua_pop(L, 1);
    if (shapes.empty()) return 0;
    d.sprite = shapes[0].sprite;
    float mass = P(L)->CreateCompound(d, shapes, 0);
    WriteWorldEntry(L, d, "compound", mass);
    return 0;
}


// Angry Birds Space (0x47d8b8, the tail of its createJoint): the loader links the two world entries of a joint. A sensor and the body it is tied to link
// each other (linkedObject); an object with a levelGoal field joined to a bubble gets linkedBubble, and the bubble lists it in linkedPigs; any other partner is linkedObject
void SpaceLinkJoint(lua_State* L, const std::string& e1, const std::string& e2) {
    static int isSpace = -1;
    if (isSpace < 0) isSpace = IsSpaceData(L) ? 1 : 0;
    if (!isSpace || !PushObjectsTable(L, "world")) return;
    int w = lua_gettop(L);
    lua_getfield(L, w, e1.c_str()); int a = lua_gettop(L);
    lua_getfield(L, w, e2.c_str()); int b = lua_gettop(L);
    if (lua_istable(L, a) && lua_istable(L, b)) {
        auto setName = [&](int t, const char* k, const std::string& v) { lua_pushlstring(L, v.data(), v.size()); lua_setfield(L, t, k); };
        if (Has(L, a, "sensorType") || Has(L, b, "sensorType")) { setName(a, "linkedObject", e2); setName(b, "linkedObject", e1); }
        for (int pass = 0; pass < 2; pass++) {
            int x = pass ? b : a, y = pass ? a : b;
            const std::string& xn = pass ? e2 : e1; const std::string& yn = pass ? e1 : e2;
            if (!Has(L, x, "levelGoal")) continue;
            bool bubble = false;
            lua_getfield(L, y, "definition");
            if (lua_isstring(L, -1)) {
                PushEnv(L);
                lua_getfield(L, -1, "blockTable");
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "blocks");
                    if (lua_istable(L, -1)) {
                        lua_pushvalue(L, -4);
                        lua_gettable(L, -2);
                        if (lua_istable(L, -1)) {
                            lua_getfield(L, -1, "material");
                            if (lua_isstring(L, -1)) bubble = strncmp(lua_tostring(L, -1), "bubble", 6) == 0;
                            lua_pop(L, 1);
                        }
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 2);
            }
            lua_pop(L, 1);
            if (bubble) {
                setName(x, "linkedBubble", yn);
                lua_getfield(L, y, "linkedPigs");
                if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, -1); lua_setfield(L, y, "linkedPigs"); }
                lua_pushlstring(L, xn.data(), xn.size());
                lua_rawseti(L, -2, (int)lua_objlen(L, -2) + 1);
                lua_pop(L, 1);
            } else setName(x, "linkedObject", yn);
        }
    }
    lua_settop(L, w - 1);
}

// createJoint{ name, end1, end2, type, coordType, x1, y1, x2, y2, ... } (0x40ecd0)
int l_createJoint(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    JointDef d;
    d.name = FieldS(L, 1, "name");
    d.end1 = FieldS(L, 1, "end1");
    d.end2 = FieldS(L, 1, "end2");
    d.type = (int)FieldN(L, 1, "type");
    d.coordType = HasNum(L, 1, "coordType") ? (int)std::floor(FieldN(L, 1, "coordType") + 0.5f) : 0;
    d.x1 = FieldN(L, 1, "x1"); d.y1 = FieldN(L, 1, "y1");
    d.x2 = FieldN(L, 1, "x2"); d.y2 = FieldN(L, 1, "y2");
    d.collideConnected = BoolOr(L, 1, "collideConnected", false);
    switch (d.type) {
    case 1:
        d.frequency = NumOr(L, 1, "frequency", 4.0f);
        d.dampingRatio = NumOr(L, 1, "dampingRatio", 0.5f);
        break;
    case 3:
        d.motor = BoolOr(L, 1, "motor", false);
        d.motorSpeed = NumOr(L, 1, "motorSpeed", 0.0f);
        d.maxTorque = NumOr(L, 1, "maxTorque", 10000.0f);
        d.limit = BoolOr(L, 1, "limit", false);
        d.lowerLimit = NumOr(L, 1, "lowerLimit", 0.0f);
        d.upperLimit = NumOr(L, 1, "upperLimit", 3.1415927f);
        d.backAndForth = BoolOr(L, 1, "backAndForth", false);
        break;
    case 4:
        d.worldAxisX = NumOr(L, 1, "worldAxisX", 0.0f);
        d.worldAxisY = NumOr(L, 1, "worldAxisY", 0.0f);
        d.limit = BoolOr(L, 1, "limit", true);
        d.lowerLimit = NumOr(L, 1, "lowerLimit", 0.0f);
        d.upperLimit = NumOr(L, 1, "upperLimit", 5.0f);
        d.motor = BoolOr(L, 1, "motor", true);
        d.motorSpeed = NumOr(L, 1, "motorSpeed", 0.0f);
        d.maxTorque = NumOr(L, 1, "maxTorque", 10000.0f);
        d.backAndForth = BoolOr(L, 1, "backAndForth", true);
        break;
    case 5:
        d.destroyTimer = NumOr(L, 1, "destroyTimer", 1.0f);
        break;
    default:
        break;
    }
    if (!P(L)->CreateJoint(d)) return 0;

    // objects.joints[name] = a new table with the effective values
    if (!PushObjectsTable(L, "joints")) return 0;
    lua_newtable(L);
    SetStr(L, "name", d.name);
    SetStr(L, "end1", d.end1);
    SetStr(L, "end2", d.end2);
    SetNum(L, "type", (float)d.type);
    SetNum(L, "coordType", (float)d.coordType);
    SetNum(L, "x1", d.x1); SetNum(L, "y1", d.y1);
    SetNum(L, "x2", d.x2); SetNum(L, "y2", d.y2);
    switch (d.type) {
    case 1:
        SetNum(L, "frequency", d.frequency);
        SetNum(L, "dampingRatio", d.dampingRatio);
        SetBool(L, "collideConnected", d.collideConnected);
        break;
    case 2:
        SetBool(L, "collideConnected", d.collideConnected);
        break;
    case 3:
        SetBool(L, "motor", d.motor);
        SetNum(L, "motorSpeed", d.motorSpeed);
        SetNum(L, "maxTorque", d.maxTorque);
        SetBool(L, "limit", d.limit);
        SetNum(L, "lowerLimit", d.lowerLimit);
        SetNum(L, "upperLimit", d.upperLimit);
        SetBool(L, "backAndForth", d.backAndForth);
        SetBool(L, "collideConnected", d.collideConnected);
        break;
    case 4:
        SetBool(L, "limit", d.limit);
        SetNum(L, "lowerLimit", d.lowerLimit);
        SetNum(L, "upperLimit", d.upperLimit);
        SetBool(L, "motor", d.motor);
        SetNum(L, "motorSpeed", d.motorSpeed);
        SetNum(L, "maxTorque", d.maxTorque);
        SetBool(L, "collideConnected", d.collideConnected);
        SetNum(L, "worldAxisX", d.worldAxisX);
        SetNum(L, "worldAxisY", d.worldAxisY);
        SetBool(L, "backAndForth", d.backAndForth);
        break;
    case 5:
        SetNum(L, "destroyTimer", d.destroyTimer);
        break;
    default:
        break;
    }
    // Rio: a joint the level marks `recreate` (rebuilt after it breaks) keeps the flag, so the editor writes it back
    lua_getfield(L, 1, "recreate");
    if (lua_isboolean(L, -1)) { lua_pushvalue(L, -1); lua_setfield(L, -3, "recreate"); }
    lua_pop(L, 1);
    // Star Wars: the level's own joint fields that the scripts read later stay on the table (gamescene.lua draws a joint whose
    // `isDrawn` is set (vines, ropes), levelGoalsLogic.lua tests `breakable`, the breaking force and the draw position ride along)
    for (const char* extra : {"breakable", "breakForce", "isDrawn", "jointPosX", "jointPosY"}) {
        lua_getfield(L, 1, extra);
        if (!lua_isnil(L, -1)) lua_setfield(L, -2, extra); else lua_pop(L, 1);
    }
    lua_setfield(L, -2, d.name.c_str());
    lua_pop(L, 1);
    SpaceLinkJoint(L, d.end1, d.end2);
    return 0;
}
// createJoints(list) (Angry Birds Rio): creates every joint of a level's joint table; a joint without a name takes its key
int l_createJoints(lua_State* L) {
    if (std::getenv("AB_JOINTDBG")) {
        int n = 0;
        if (lua_istable(L, 1)) { lua_pushnil(L); while (lua_next(L, 1)) { ++n; lua_pop(L, 1); } }
        std::fprintf(stderr, "createJoints: %s, %d entries\n", lua_istable(L, 1) ? "table" : "not a table", n);
    }
    if (!lua_istable(L, 1)) return 0;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "name");
            bool named = lua_isstring(L, -1);
            lua_pop(L, 1);
            if (!named && lua_type(L, -2) == LUA_TSTRING) {
                lua_pushvalue(L, -2);
                lua_setfield(L, -2, "name");
            }
            lua_pushvalue(L, lua_upvalueindex(1));        // the platform
            lua_pushcclosure(L, l_createJoint, 1);
            lua_pushvalue(L, -2);
            lua_call(L, 1, 0);
        }
        lua_pop(L, 1);
    }
    return 0;
}
int l_setJointParameters(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    JointParams p;
    p.name = FieldS(L, 1, "name");
    if ((p.hasMotor = Has(L, 1, "motor"))) p.motor = FieldB(L, 1, "motor");
    if ((p.hasMotorSpeed = Has(L, 1, "motorSpeed"))) p.motorSpeed = FieldN(L, 1, "motorSpeed");
    if ((p.hasMaxTorque = Has(L, 1, "maxTorque"))) p.maxTorque = FieldN(L, 1, "maxTorque");
    if ((p.hasLimit = Has(L, 1, "limit"))) p.limit = FieldB(L, 1, "limit");
    if ((p.hasLowerLimit = Has(L, 1, "lowerLimit"))) p.lowerLimit = FieldN(L, 1, "lowerLimit");
    if ((p.hasUpperLimit = Has(L, 1, "upperLimit"))) p.upperLimit = FieldN(L, 1, "upperLimit");
    if ((p.hasFrequency = Has(L, 1, "frequency"))) p.frequency = FieldN(L, 1, "frequency");
    if ((p.hasDampingRatio = Has(L, 1, "dampingRatio"))) p.dampingRatio = FieldN(L, 1, "dampingRatio");
    P(L)->SetJointParameters(p);
    if (PushObjectsTable(L, "joints")) {            // the joint's table follows
        lua_getfield(L, -1, p.name.c_str());
        if (lua_istable(L, -1)) {
            if (p.hasMotor) SetBool(L, "motor", p.motor);
            if (p.hasMotorSpeed) SetNum(L, "motorSpeed", p.motorSpeed);
            if (p.hasMaxTorque) SetNum(L, "maxTorque", p.maxTorque);
            if (p.hasLimit) SetBool(L, "limit", p.limit);
            if (p.hasLowerLimit) SetNum(L, "lowerLimit", p.lowerLimit);
            if (p.hasUpperLimit) SetNum(L, "upperLimit", p.upperLimit);
            if (p.hasFrequency) SetNum(L, "frequency", p.frequency);
            if (p.hasDampingRatio) SetNum(L, "dampingRatio", p.dampingRatio);
        }
        lua_pop(L, 2);
    }
    return 0;
}
int l_checkJointLimits(lua_State* L) { P(L)->CheckJointLimits(S(L, 1)); return 0; }
int l_handleJointLimits(lua_State* L) { P(L)->CheckJointLimits(S(L, 1), B(L, 2)); return 0; }
int l_destroyJoint(lua_State* L) {
    std::string n = S(L, 1);
    P(L)->DestroyJoint(n);
    if (PushObjectsTable(L, "joints")) {              // joints[name] = nil (0x410e60)
        lua_pushnil(L);
        lua_setfield(L, -2, n.c_str());
        lua_pop(L, 1);
    }
    return 0;
}
bool RioMode(lua_State* L);
int l_removeObject(lua_State* L) {
    EmitHook(L, "objectRemoved", S(L, 1).c_str());
    std::vector<std::string> fired = P(L)->RemoveObject(S(L, 1));
    if (!fired.empty() && PushObjectsTable(L, "joints")) {
        for (size_t i = 0; i < fired.size(); i++) {
            lua_pushnil(L);
            lua_setfield(L, -2, fired[i].c_str());
        }
        lua_pop(L, 1);
    }
    if ((RioMode(L) || IsSpaceData(L)) && PushObjectsTable(L, "joints")) {      // Rio and Space/Star Wars: a joint whose object is gone does not stay in objects.joints (removeBird would follow it to a missing object)
        std::string gone = S(L, 1);
        std::vector<std::string> stale;
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
                bool match = false;
                for (const char* end : {"end1", "end2"}) {
                    lua_getfield(L, -1, end);
                    if (lua_isstring(L, -1) && gone == lua_tostring(L, -1)) match = true;
                    lua_pop(L, 1);
                }
                if (match) stale.push_back(lua_tostring(L, -2));
            }
            lua_pop(L, 1);
        }
        for (const std::string& k : stale) { lua_pushnil(L); lua_setfield(L, -2, k.c_str()); }
        lua_pop(L, 1);
    }
    return 0;
}
int l_applyImpulse(lua_State* L) { P(L)->ApplyImpulse(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5)); return 0; }
int l_applyForce(lua_State* L) { P(L)->ApplyForce(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5)); return 0; }
// the exe also writes x / y (angle for setRotation) into objects.world[name]
void SetWorldField(lua_State* L, const std::string& name, const char* k1, float v1, const char* k2 = nullptr, float v2 = 0) {
    if (!PushObjectsTable(L, "world")) return;
    lua_getfield(L, -1, name.c_str());
    if (lua_istable(L, -1)) {
        SetNum(L, k1, v1);
        if (k2) SetNum(L, k2, v2);
    }
    lua_pop(L, 2);
}
int l_setPosition(lua_State* L) {
    std::string n = S(L, 1);
    float x = N(L, 2), y = N(L, 3);
    if (P(L)->SetPosition(n, x, y)) SetWorldField(L, n, "x", x, "y", y);
    return 0;
}
int l_setRotation(lua_State* L) {
    std::string n = S(L, 1);
    float a = N(L, 2);
    if (P(L)->SetRotation(n, a, a)) SetWorldField(L, n, "angle", a);
    return 0;
}
// setRecordVelocity(name, on): Space (0x47a5f0). Flags the object so the frame update keeps objects.world[name].xVel / yVel / angle current for it
int l_setRecordVelocity(lua_State* L) { P(L)->SetRecordVelocity(S(L, 1), lua_toboolean(L, 2) != 0); return 0; }
int l_setVelocity(lua_State* L) { P(L)->SetVelocity(S(L, 1), N(L, 2), N(L, 3)); return 0; }
// Angry Birds Space: body properties (0x4... setLinearDamping, setAngularDamping, setFixedRotation, setActive, multiplyVelocity, getAngularVelocity, setAsSensor)
int BodyCmd(lua_State* L, int cmd, bool boolArg = false) {
    float r = 0, a = boolArg ? (lua_toboolean(L, 2) ? 1.0f : 0.0f) : N(L, 2);
    P(L)->BodyCommand(S(L, 1), cmd, a, r);
    return 0;
}
int l_enableAimingAid(lua_State* L) { float r = 0; P(L)->BodyCommand("", 20, lua_toboolean(L, 1) ? 1.0f : 0.0f, r); return 0; }
int l_setAimingAidSprite(lua_State* L) { float r = 0; P(L)->BodyCommand(lua_isstring(L, 1) ? lua_tostring(L, 1) : "", 21, lua_isnumber(L, 2) ? (float)lua_tonumber(L, 2) : 1.0f, r); return 0; }
int l_populateAimingAid(lua_State* L) { float r = 0; P(L)->BodyCommand("", 22, 0, r); return 0; }
int l_clearAimingAid(lua_State* L) { float r = 0; P(L)->BodyCommand("", 23, 0, r); return 0; }
int l_updateBirdTrajectoryTable(lua_State* L) { float r = 0; P(L)->BodyCommand(lua_isstring(L, 1) ? lua_tostring(L, 1) : "BirdSimulation", 24, 0, r); return 0; }
int l_spriteExists(lua_State* L) { lua_pushboolean(L, P(L)->SpriteExists(S(L, 1))); return 1; }
int l_simulateAimingAid(lua_State* L) { float st[2] = {N(L, 5), N(L, 6)}; lua_pushboolean(L, P(L)->AimSimulate(lua_isstring(L, 1) ? lua_tostring(L, 1) : "", N(L, 2), N(L, 3), lua_isnumber(L, 4) ? (int)lua_tonumber(L, 4) : 120, lua_isnumber(L, 5) && lua_isnumber(L, 6) ? st : nullptr)); return 1; }
int l_setSensorGravityMask(lua_State* L) { return BodyCmd(L, 10); }
int l_setRevertGravity(lua_State* L) { float r = 0; P(L)->BodyCommand(S(L, 1), 14, B(L, 2) ? 1.0f : 0.0f, r); return 0; }
int l_setRevertGravityWithMultiplier(lua_State* L) { float r = 0; P(L)->BodyCommand(S(L, 1), 15, B(L, 2) ? 1.0f : 0.0f, r); P(L)->BodyCommand(S(L, 1), 16, NOpt(L, 3, 0), r); P(L)->BodyCommand(S(L, 1), 17, NOpt(L, 4, 0), r); return 0; }
int l_setObjectGravityCategory(lua_State* L) { return BodyCmd(L, 11); }
int l_setSensorMinimumAndMaximumForces(lua_State* L) { float r = 0; P(L)->BodyCommand(S(L, 1), 12, N(L, 2), r); P(L)->BodyCommand(S(L, 1), 13, N(L, 3), r); return 0; }
int l_setGravityForceMultiplier(lua_State* L) { float r = 0; P(L)->BodyCommand("", 8, N(L, 1), r); return 0; }
int l_getGravityForceMultiplier(lua_State* L) { float r = 1; P(L)->BodyCommand("", 9, 0, r); lua_pushnumber(L, r); return 1; }
int l_setLinearDamping(lua_State* L) { return BodyCmd(L, 1); }
int l_setAngularDamping(lua_State* L) { return BodyCmd(L, 2); }
int l_setFixedRotation(lua_State* L) { return BodyCmd(L, 3, true); }
int l_setActive(lua_State* L) { return BodyCmd(L, 4, true); }
int l_multiplyVelocity(lua_State* L) { return BodyCmd(L, 5); }
int l_setAsSensor(lua_State* L) { return BodyCmd(L, 7, true); }
int l_getAngularVelocity(lua_State* L) { float r = 0; P(L)->BodyCommand(S(L, 1), 6, 0, r); lua_pushnumber(L, r); return 1; }
int l_setAngularVelocity(lua_State* L) { P(L)->SetAngularVelocity(S(L, 1), N(L, 2)); return 0; }
int l_setSleeping(lua_State* L) { P(L)->SetSleeping(S(L, 1), B(L, 2)); return 0; }
int l_getAngle(lua_State* L) { lua_pushnumber(L, P(L)->GetAngle(S(L, 1))); return 1; }
int l_getWorldPoint(lua_State* L) {
    float wx, wy;
    P(L)->GetWorldPoint(S(L, 1), N(L, 2), N(L, 3), wx, wy);
    lua_pushnumber(L, wx);
    lua_pushnumber(L, wy);
    return 2;
}
// getVelocity(name) -> speed, vx, vy   (every game; Rio's own native returns the speed only, and a script that takes one value still gets exactly that)
//   The speed of the object's body in world units per second (0 for an unknown object), then the two components of its velocity, x to the right and y
//   downwards, as the body has them right now, whatever kind of object it is. A body that sleeps has no velocity (0, 0, 0).
int l_getVelocity(lua_State* L) {
    float vx = 0, vy = 0;
    P(L)->GetVelocity(S(L, 1), vx, vy);
    lua_pushnumber(L, P(L)->GetSpeed(S(L, 1)));
    lua_pushnumber(L, vx);
    lua_pushnumber(L, vy);
    return 3;
}
// Angry Birds Space: getLinearVelocity(name) -> vx, vy (0x47e870) and getBGColor() -> r, g, b in 0..255 (0x4626a0)
int l_getLinearVelocity(lua_State* L) {
    float vx = 0, vy = 0;
    P(L)->GetVelocity(S(L, 1), vx, vy);
    lua_pushnumber(L, vx);
    lua_pushnumber(L, vy);
    return 2;
}
// getRayCastedObjects({x1 =, y1 =, x2 =, y2 =}) -> a flat list with six entries per object the segment crosses: name, hit x, hit y, normal x, normal y, fraction (0x46f3f0)
int l_getRayCastedObjects(lua_State* L) {
    std::vector<Platform::RayCastHit> hits;
    if (lua_istable(L, 1)) P(L)->RayCastObjects(FieldN(L, 1, "x1"), FieldN(L, 1, "y1"), FieldN(L, 1, "x2"), FieldN(L, 1, "y2"), hits);
    lua_newtable(L);
    int i = 1;
    for (const auto& h : hits) {
        lua_pushlstring(L, h.name.data(), h.name.size()); lua_rawseti(L, -2, i++);
        lua_pushnumber(L, h.px); lua_rawseti(L, -2, i++);
        lua_pushnumber(L, h.py); lua_rawseti(L, -2, i++);
        lua_pushnumber(L, h.nx); lua_rawseti(L, -2, i++);
        lua_pushnumber(L, h.ny); lua_rawseti(L, -2, i++);
        lua_pushnumber(L, h.fraction); lua_rawseti(L, -2, i++);
    }
    return 1;
}
int l_getBGColor(lua_State* L) {
    float r, g, b;
    P(L)->GetBGColor(r, g, b);
    lua_pushnumber(L, r); lua_pushnumber(L, g); lua_pushnumber(L, b);
    return 3;
}
int l_hasMovingObjects(lua_State* L) { lua_pushboolean(L, P(L)->HasMovingObjects(0.5f)); return 1; }
int l_hasMovingObjectsZeroTolerance(lua_State* L) { lua_pushboolean(L, P(L)->HasMovingObjects(0.001f)); return 1; }
int l_getLocalPoint(lua_State* L) {
    float lx, ly;
    P(L)->GetLocalPoint(S(L, 1), N(L, 2), N(L, 3), lx, ly);
    lua_pushnumber(L, lx);
    lua_pushnumber(L, ly);
    return 2;
}
int l_setCollisionEnabled(lua_State* L) { P(L)->SetCollisionEnabled(S(L, 1), B(L, 2)); return 0; }
// setColliderType(name, type) / getColliderType(name) -> type (-1: no such object); setColliderRule(typeA, typeB, collides) / getColliderRule(typeA, typeB)
int l_setColliderType(lua_State* L) { P(L)->SetColliderType(S(L, 1), (int)N(L, 2)); return 0; }
int l_getColliderType(lua_State* L) { lua_pushnumber(L, P(L)->ColliderType(S(L, 1))); return 1; }
// setObjectAlpha(name, alpha) / getObjectAlpha(name): transparency of one object when drawGameNative draws it (0 invisible .. 1 solid; -1: no such object)
// Space (0x47a1e0, 0x47a2e0, 0x47b8f0): per-object sprite settings; the exe also writes them into objects.world[name]
int l_setSpriteRotation(lua_State* L) {
    P(L)->SetSpriteLook(S(L, 1), 0, N(L, 2), 0);
    SetWorldField(L, S(L, 1), "spriteAngle", P(L)->GetSpriteAngle(S(L, 1)));
    return 0;
}
int l_setPivotOffset(lua_State* L) {
    P(L)->SetSpriteLook(S(L, 1), 1, N(L, 2), N(L, 3));
    SetWorldField(L, S(L, 1), "pivotOffsetX", N(L, 2), "pivotOffsetY", N(L, 3));
    return 0;
}
int l_setTextureScale(lua_State* L) { P(L)->SetSpriteLook(S(L, 1), 2, N(L, 2), 0); return 0; }
int l_setNormalTrailSprite(lua_State* L) { P(L)->SetTrailSprite(false, lua_isstring(L, 1) ? lua_tostring(L, 1) : ""); return 0; }
int l_setSpecialTrailSprite(lua_State* L) { P(L)->SetTrailSprite(true, lua_isstring(L, 1) ? lua_tostring(L, 1) : ""); return 0; }
// setObjectDecoration(name, sprite, amount, angleIncrement, scale): the engine half of Space's setDecorationObjects (0x479fb0, see space_natives.lua)
int l_setObjectDecoration(lua_State* L) { P(L)->SetDecoration(S(L, 1), S(L, 2), (int)N(L, 3), N(L, 4), N(L, 5)); return 0; }
int l_setObjectAlpha(lua_State* L) { P(L)->SetObjectAlpha(S(L, 1), N(L, 2)); return 0; }
int l_getObjectAlpha(lua_State* L) { lua_pushnumber(L, P(L)->ObjectAlpha(S(L, 1))); return 1; }
// setObjectZOrder(name, z) / getObjectZOrder(name): the draw order of one object in drawGameNative (the z_order of createBlock; higher = in front)
// v2.2.3: objects.world[name].z_order always shows the current z (a script that reads it sees what setObjectZOrder, bringObjectToFront or the console set)
int l_setObjectZOrder(lua_State* L) {
    P(L)->SetObjectZOrder(S(L, 1), N(L, 2));
    SyncWorldZ(L, S(L, 1), P(L)->ObjectZOrder(S(L, 1)));
    return 0;
}
// nil for an object that does not exist (before: -1e9)
int l_getObjectZOrder(lua_State* L) {
    float z = P(L)->ObjectZOrder(S(L, 1));
    if (z <= -1e8f) lua_pushnil(L); else lua_pushnumber(L, z);
    return 1;
}
// bringObjectToFront(name) / sendObjectToBack(name) -> the new z_order: in front of / behind every other object
int l_moveObjectZ(lua_State* L, bool front) {
    float z = P(L)->MoveObjectZ(S(L, 1), front);
    if (z <= -1e8f) { lua_pushnil(L); return 1; }
    SyncWorldZ(L, S(L, 1), z);
    lua_pushnumber(L, z);
    return 1;
}
int l_bringObjectToFront(lua_State* L) { return l_moveObjectZ(L, true); }
int l_sendObjectToBack(lua_State* L) { return l_moveObjectZ(L, false); }
// getObjectDrawOrder() -> { name, ... } the objects of drawGameNative from the back to the front
int l_getObjectDrawOrder(lua_State* L) {
    std::vector<std::string> names = P(L)->GameDrawOrder();
    lua_createtable(L, (int)names.size(), 0);
    for (size_t i = 0; i < names.size(); i++) { lua_pushlstring(L, names[i].data(), names[i].size()); lua_rawseti(L, -2, (int)i + 1); }
    return 1;
}
// getRenderZ(): the z of the render state (setRenderZ(z) sets it alone)
int l_getRenderZ(lua_State* L) { lua_pushnumber(L, P(L)->RenderZ()); return 1; }
int l_setColliderRule(lua_State* L) { P(L)->SetColliderRule((int)N(L, 1), (int)N(L, 2), B(L, 3)); return 0; }
int l_getColliderRule(lua_State* L) { lua_pushboolean(L, P(L)->ColliderRule((int)N(L, 1), (int)N(L, 2))); return 1; }
// setPolygonRadius(r) / getPolygonRadius(): the skin of the boxes and polygons that are created afterwards (Angry Birds Rio's Box2D 2.2)
int l_setPolygonRadius(lua_State* L) { P(L)->SetPolygonRadius(N(L, 1)); return 0; }
int l_getPolygonRadius(lua_State* L) { lua_pushnumber(L, P(L)->PolygonRadius()); return 1; }
int l_removeJointsFromObject(lua_State* L) { P(L)->RemoveJointsFromObject(S(L, 1)); return 0; }
// getIntersectingObjects{ x, y, left, right, down, up } -> { name, name, ... } (the scripts read the names as the values)
bool RioMode(lua_State* L);
// predictCollision(nameA, nameB [, restitution]) -> table
//   Works out what would happen if two objects met, from where they are and how they move right now. Nothing is simulated and nothing is changed, so a
//   script can ask before a bird is launched or every frame. Both arguments are object names (the names objects.world uses). A name that is not an
//   object is not an error: the table is { valid = false, collides = false, filtered = false } and nothing else.
//   The hit is Box2D's single-contact resolution on the real shapes: the closest points of the bodies' fixtures give the contact point (contactX,
//   contactY) and the normal (nx, ny, from A to B); a normal impulse gives the bounce, a friction impulse along the tangent (ny, -nx), limited to
//   friction * normal impulse, gives the slide, and both act at the contact point so the bodies' inertia and spin (aAngVel / bAngVel) take part.
//   Optional arguments 4 to 7 (aVx, aVy, bVx, bVy) replace the bodies' present velocities for the estimate; nil or left out keeps the present one.
//   restitution is optional: left out, the larger of the fixtures' restitutions is used (as Box2D does), and Box2D's velocity threshold of 1 applies.
//   Fields: valid, filtered, collides, exactShapes, distance, gap, contactX, contactY, nx, ny, relVx, relVy, normalSpeed, tangentSpeed, closingSpeed,
//   timeToContact, massA, massB, restitution, friction, impulse, tangentImpulse, aVx, aVy, bVx, bVy, aAngVel, bAngVel, energyLost (see the README).
//   Box2D 2.1 worlds do not query the shapes: exactShapes is false and the contact is taken on the line between the centres, without spin.
//   Angles are not returned because they follow from the velocities: math.atan2(vy, vx) for a direction, math.atan2(ny, nx) for the contact.
int l_predictCollision(lua_State* L) {
    // optional arguments 4 to 7: the velocities to test with, A's x and y then B's x and y; nil or left out keeps the body's present velocity
    float vel[4];
    for (int i = 0; i < 4; i++) vel[i] = lua_isnoneornil(L, 4 + i) ? std::nanf("") : N(L, 4 + i);
    CollisionPrediction c = P(L)->PredictCollision(S(L, 1), S(L, 2), lua_isnoneornil(L, 3) ? -1.0f : N(L, 3), vel);
    lua_newtable(L);
    auto num = [&](const char* k, float v) { lua_pushnumber(L, v); lua_setfield(L, -2, k); };
    auto flag = [&](const char* k, bool v) { lua_pushboolean(L, v); lua_setfield(L, -2, k); };
    flag("valid", c.valid); flag("collides", c.collides); flag("filtered", c.filtered);
    if (!c.valid) return 1;
    num("distance", c.distance); num("gap", c.gap); num("nx", c.nx); num("ny", c.ny);
    num("relVx", c.relVx); num("relVy", c.relVy); num("closingSpeed", c.closingSpeed); num("timeToContact", c.timeToContact);
    num("impulse", c.impulse); num("restitution", c.restitution); num("massA", c.massA); num("massB", c.massB);
    num("aVx", c.aVx); num("aVy", c.aVy); num("bVx", c.bVx); num("bVy", c.bVy); num("energyLost", c.energyLost);
    flag("exactShapes", c.exactShapes); num("contactX", c.contactX); num("contactY", c.contactY);
    num("normalSpeed", c.normalSpeed); num("tangentSpeed", c.tangentSpeed); num("friction", c.friction); num("tangentImpulse", c.tangentImpulse);
    num("aAngVel", c.aAngVel); num("bAngVel", c.bAngVel);
    return 1;
}
// ---- gravity of a single object, independent of the world's ----------------------------------------------------------------------------------------
// setObjectGravity(name, gx, gy) -> true, or false when there is no such object.
//   Gives the object a gravity of its own: from now on it accelerates by (gx, gy) world units per second squared instead of by the world's gravity.
//   The axes are the world's, the ones getVelocity / setVelocity / setPosition use, so (0, 0) is weightless, the world's own value changes nothing, and
//   the opposite of the world's value turns the world upside down for this one object: the ground becomes the sky and it falls the other way, while
//   everything else still falls the usual way. Any other direction works the same: (gx, 0) is a wall that pulls sideways. Gravitation sensors still
//   act on the object on top of this, as they do on any body. A sleeping object is woken so it starts to move at once. Static bodies are not affected
//   (they do not move), and the call is remembered until resetObjectGravity.
int l_setObjectGravity(lua_State* L) { lua_pushboolean(L, P(L)->SetOwnGravityVector(S(L, 1), N(L, 2), N(L, 3))); return 1; }
// setObjectGravityScale(name, scale) -> true, or false when there is no such object.
//   Gives the object the world's gravity times `scale`, whatever the world's gravity is (it follows the world if that changes, for example in a
//   level that sets its own). 1 is normal, 0 is weightless, 0.2 a floaty moon-like object, 3 a heavy one, and -1 turns it round exactly: the
//   object falls upwards as fast as everything else falls down. The factor multiplies the world's gravity, so in a world whose gravity is 0 (Angry Birds
//   Space) it changes nothing: use setObjectGravity there.
int l_setObjectGravityScale(lua_State* L) { lua_pushboolean(L, P(L)->SetOwnGravityScale(S(L, 1), N(L, 2))); return 1; }
// resetObjectGravity(name) -> true, or false when there is no such object.
//   Takes the object back to the world's gravity (and undoes setObjectGravity and setObjectGravityScale). Its speed stays as it is.
int l_resetObjectGravity(lua_State* L) { lua_pushboolean(L, P(L)->ResetOwnGravity(S(L, 1))); return 1; }
// getObjectGravity(name) -> gx, gy, mode, scale; nil when there is no such object.
//   The acceleration the object has from gravity now (its own, or the world's times its factor, or the world's), and where it comes from: mode is
//   "world", "vector" (setObjectGravity) or "scale" (setObjectGravityScale); scale is the factor of the last one (1 otherwise).
int l_getObjectGravity(lua_State* L) {
    int mode; float gx, gy, scale;
    if (!P(L)->GetOwnGravity(S(L, 1), mode, gx, gy, scale)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, gx); lua_pushnumber(L, gy); lua_pushstring(L, mode == 1 ? "vector" : mode == 2 ? "scale" : "world"); lua_pushnumber(L, scale);
    return 4;
}
int l_getIntersectingObjects(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    // Seasons passes the sizes of the four sides; Rio passes signed offsets (left and down are negative)
    // Space (game.lua makeExplosion) passes left = -R, right = R, down = -R, up = R as well
    static int isSpace = -1;
    if (isSpace < 0) isSpace = IsSpaceData(L) ? 1 : 0;
    float sideSign = (RioMode(L) || isSpace) ? -1.0f : 1.0f;
    std::vector<std::string> names = P(L)->IntersectingObjects(FieldN(L, 1, "x"), FieldN(L, 1, "y"), sideSign * FieldN(L, 1, "left"),
                                                               FieldN(L, 1, "right"), sideSign * FieldN(L, 1, "down"), FieldN(L, 1, "up"));
    lua_newtable(L);
    bool haveWorld = PushObjectsTable(L, "world");     // only objects the scripts still know (they index objects.world[name])
    int n = 0;
    for (const std::string& name : names) {
        if (haveWorld) {
            lua_getfield(L, -1, name.c_str());
            bool known = lua_istable(L, -1);
            lua_pop(L, 1);
            if (!known) continue;
        }
        lua_pushlstring(L, name.data(), name.size());
        lua_rawseti(L, haveWorld ? -3 : -2, ++n);
    }
    if (haveWorld) lua_pop(L, 1);
    return 1;
}
int l_setObjectParameter(lua_State* L) {
    int id = (int)N(L, 2);
    float value = N(L, 3);
    PushEnv(L);
    lua_getfield(L, -1, "rioMode");
    bool rio = lua_toboolean(L, -1) != 0;
    lua_pop(L, 2);
    // Angry Birds Rio: 6 and 7 are bounce multipliers, not Seasons' "use the scale", and a scale of its own applies as is
    if (rio && (id == 6 || id == 7)) { P(L)->SetObjectParameter(S(L, 1), id + 100, value); return 0; }
    P(L)->SetObjectParameter(S(L, 1), id, value);
    if (rio && id == 5) P(L)->SetObjectParameter(S(L, 1), 6, value != 1.0f ? 1.0f : 0.0f);
    return 0;
}
// ---- v2.4 natives, every game
// setScale(name, sx [, sy]) / getScale(name) -> sx, sy: the scale the sprite of an object is drawn with (what setObjectParameter(name, 5, v) sets; a missing sy
// means sy = sx). Turns scaled drawing on in the games that need it. It scales the picture only: the shape stays as it is (resizeCircle changes the shape).
int l_setScale(lua_State* L) {
    float sx = N(L, 2), sy = lua_isnumber(L, 3) ? N(L, 3) : sx;
    P(L)->SetObjectScale(S(L, 1), sx, sy);
    return 0;
}
int l_getScale(lua_State* L) {
    float sx = 1, sy = 1;
    if (!P(L)->GetObjectScale(S(L, 1), sx, sy)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, sx); lua_pushnumber(L, sy);
    return 2;
}
// setSpeedCap(name, maxSpeed [, maxVx, maxVy]) / getSpeedCap(name) -> maxSpeed, maxVx, maxVy / clearSpeedCap(name): the object's velocity is brought back under the
// limits after every physics step (0 or nil: no limit on that one). Only this object; the global setMaxTranslation is not touched.
int l_setSpeedCap(lua_State* L) { P(L)->SetSpeedCap(S(L, 1), NOpt(L, 2, 0), NOpt(L, 3, 0), NOpt(L, 4, 0)); return 0; }
int l_clearSpeedCap(lua_State* L) { P(L)->SetSpeedCap(S(L, 1), 0, 0, 0); return 0; }
int l_getSpeedCap(lua_State* L) {
    float a = 0, b = 0, c = 0;
    if (!P(L)->GetSpeedCap(S(L, 1), a, b, c)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, a); lua_pushnumber(L, b); lua_pushnumber(L, c);
    return 3;
}
// getTouchingObjects(name) -> { names }: every object the body touches right now (its contact list, this very step; sensors and disabled contacts included)
int l_getTouchingObjects(lua_State* L) {
    std::vector<std::string> names = P(L)->TouchingObjects(S(L, 1));
    lua_newtable(L);
    for (size_t i = 0; i < names.size(); i++) { lua_pushlstring(L, names[i].data(), names[i].size()); lua_rawseti(L, -2, (int)i + 1); }
    return 1;
}
// rayCast(x1, y1, x2, y2 [, all]) with Box2D's own ray cast. Without `all`: name, hitX, hitY, normalX, normalY, fraction of the nearest object (nil if none).
// With all = true: a table of { name, x, y, nx, ny, fraction } for every object the segment crosses, nearest first.
int l_rayCast(lua_State* L) {
    std::vector<Platform::RayCastHit> hits;
    P(L)->RayCastObjects(N(L, 1), N(L, 2), N(L, 3), N(L, 4), hits);
    std::stable_sort(hits.begin(), hits.end(), [](const Platform::RayCastHit& a, const Platform::RayCastHit& b) { return a.fraction < b.fraction; });
    if (lua_toboolean(L, 5)) {
        lua_newtable(L);
        int i = 1;
        for (const auto& h : hits) {
            lua_newtable(L);
            lua_pushlstring(L, h.name.data(), h.name.size()); lua_setfield(L, -2, "name");
            lua_pushnumber(L, h.px); lua_setfield(L, -2, "x");
            lua_pushnumber(L, h.py); lua_setfield(L, -2, "y");
            lua_pushnumber(L, h.nx); lua_setfield(L, -2, "nx");
            lua_pushnumber(L, h.ny); lua_setfield(L, -2, "ny");
            lua_pushnumber(L, h.fraction); lua_setfield(L, -2, "fraction");
            lua_rawseti(L, -2, i++);
        }
        return 1;
    }
    if (hits.empty()) { lua_pushnil(L); return 1; }
    const auto& h = hits[0];
    lua_pushlstring(L, h.name.data(), h.name.size());
    lua_pushnumber(L, h.px); lua_pushnumber(L, h.py); lua_pushnumber(L, h.nx); lua_pushnumber(L, h.ny); lua_pushnumber(L, h.fraction);
    return 6;
}
// fileExists(path) -> bool: a file of the game folder (relative to where the game runs) or of the user data (saves), nothing is opened for reading by the game
int l_fileExists(lua_State* L) {
    std::string name = S(L, 1);
    for (int pass = 0; pass < 3; pass++) {
        std::string path = pass == 0 ? name : P(L)->ResolveUserFile(name, pass == 1);
        if (path.empty()) continue;
        if (FILE* f = std::fopen(path.c_str(), "rb")) { std::fclose(f); lua_pushboolean(L, 1); return 1; }
    }
    lua_pushboolean(L, 0);
    return 1;
}
int l_setPhysicsSimulationScale(lua_State* L) { P(L)->SetPhysicsSimulationScale(N(L, 1)); return 0; }
int l_setStartingCameraValue(lua_State* L) { P(L)->SetStartingCameraValue(B(L, 1)); return 0; }
int l_setThemeOffsetY(lua_State* L) { P(L)->SetThemeOffsetY(N(L, 2)); return 0; }
int l_setPhysicsEnabled(lua_State* L) { P(L)->SetPhysicsEnabled(B(L, 1)); return 0; }
int l_isPhysicsEnabled(lua_State* L) { lua_pushboolean(L, P(L)->IsPhysicsEnabled()); return 1; }
int l_setLevelLimits(lua_State* L) { P(L)->SetLevelLimits(N(L, 1), N(L, 2), N(L, 3), N(L, 4)); return 0; }

// ---- camera
int l_setTopLeft(lua_State* L) { P(L)->SetTopLeft(N(L, 1), N(L, 2)); return 0; }
int l_setWorldScale(lua_State* L) { P(L)->SetWorldScale(N(L, 1)); return 0; }
int l_setMaxWorldScale(lua_State* L) { P(L)->SetMaxWorldScale(N(L, 1)); return 0; }
int l_resetMouseWheelScale(lua_State* L) { P(L)->ResetMouseWheelScale(N(L, 1)); return 0; }
int l_setMaxTranslation(lua_State* L) { P(L)->SetMaxTranslation(N(L, 1)); return 0; }

// ---- drawing
int BlendFrom(lua_State* L, int i);
int ShaderFrom(lua_State* L, int i);
int l_setRenderState(lua_State* L) {
    // tx, ty, sx, sy, angle, pivotX, pivotY, alpha, skewX, skewY, bendX, bendY, z, bendFunction (a tween function: f(t, 0, 1, 1) is the bend's shape)
    float a[22] = {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0};
    int argc = lua_gettop(L);
    for (int i = 0; i < argc && i < 13; i++) a[i] = N(L, i + 1);
    int count = argc < 13 ? argc : 13;
    if (argc >= 14 && lua_isfunction(L, 14)) {
        for (int i = 0; i < 9; i++) {
            lua_pushvalue(L, 14);
            lua_pushnumber(L, i / 8.0); lua_pushnumber(L, 0); lua_pushnumber(L, 1); lua_pushnumber(L, 1);
            if (lua_pcall(L, 4, 1, 0) == 0) a[13 + i] = (float)lua_tonumber(L, -1);
            else a[13 + i] = i / 8.0f;
            lua_pop(L, 1);
        }
        count = 22;
    }
    P(L)->SetRenderState(count, a);
    if (argc >= 17) P(L)->SetTint(N(L, 15), N(L, 16), N(L, 17));      // v2.2: setRenderState(..., z, bendFunction, tintR, tintG, tintB)
    if (argc >= 18) P(L)->SetRenderExtras(3, N(L, 18), 0);     // ... tintR, tintG, tintB, blur, pixelate
    if (argc >= 19) P(L)->SetRenderExtras(4, N(L, 19), 0);
    if (argc >= 20 && lua_type(L, 20) != LUA_TNIL) P(L)->SetBlendMode(BlendFrom(L, 20));      // v2.4: ... pixelate, blend mode, shader
    if (argc >= 21 && lua_type(L, 21) != LUA_TNIL) P(L)->SetShaderEffect(ShaderFrom(L, 21));
    return 0;
}
int l_drawRect(lua_State* L) {
    P(L)->DrawRect(N(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), N(L, 8), B(L, 9));
    return 0;
}
int l_drawLine2D(lua_State* L) {
    P(L)->DrawLine2D(N(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), N(L, 8), N(L, 9));
    return 0;
}
// drawRectLines(left, top, right, bottom, width, r, g, b, a): the four edges of a rectangle
int l_drawRectLines(lua_State* L) {
    float x1 = N(L, 1), y1 = N(L, 2), x2 = N(L, 3), y2 = N(L, 4), w = N(L, 5), r = N(L, 6), g = N(L, 7), b = N(L, 8), a = N(L, 9);
    P(L)->DrawLine2D(x1, y1, x2, y1, w, r, g, b, a);
    P(L)->DrawLine2D(x2, y1, x2, y2, w, r, g, b, a);
    P(L)->DrawLine2D(x2, y2, x1, y2, w, r, g, b, a);
    P(L)->DrawLine2D(x1, y2, x1, y1, w, r, g, b, a);
    return 0;
}
// 2.0.0 asks the device about itself and has an ads / notification layer this engine does not have
int l_osName(lua_State* L) { lua_pushstring(L, "Windows"); return 1; }
int l_osVersion(lua_State* L) { lua_pushstring(L, "6.1"); return 1; }
int l_deviceModel(lua_State* L) { lua_pushstring(L, "PC"); return 1; }
int l_deviceManufacturer(lua_State* L) { lua_pushstring(L, "PC"); return 1; }
int l_currentLocale(lua_State* L) { lua_pushstring(L, "en"); return 1; }

// ---- 2.0.0 additions
// drawBoxNative(components, x, y, width, height, hanchor, vanchor): a 3x3 (or 3x1) box of sprites. The
// corners keep their size, the edges and the middle stretch to fill the rest.
void ParseAnchor(const char* word, int& v, int& h);
int l_drawBoxNative(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    auto comp = [&](const char* key) -> std::string {
        lua_getfield(L, 1, key);
        std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        return v;
    };
    float x = N(L, 2), y = N(L, 3), w = N(L, 4), h = N(L, 5);
    int v = 0, hh = 0;
    if (lua_isstring(L, 6)) ParseAnchor(lua_tostring(L, 6), v, hh);
    if (lua_isstring(L, 7)) ParseAnchor(lua_tostring(L, 7), v, hh);
    float left = hh == 1 ? x - w / 2 : hh == 2 ? x - w : x;
    float top = v == 1 ? y - h / 2 : v == 2 ? y - h : y;
    auto bounds = [&](const std::string& n, float& bw, float& bh) {
        bw = bh = 0;
        if (!n.empty()) P(L)->ResGetSpriteBounds("", n, bw, bh);
    };
    auto draw = [&](const std::string& n, float dx, float dy, float dw, float dh) {
        if (n.empty() || dw <= 0 || dh <= 0) return;
        P(L)->ResDrawSprite(n, dx, dy, 0, 0, true, dw, dh);
    };
    std::string tl = comp("topLeft"), tm = comp("topMiddle"), tr = comp("topRight");
    std::string l = comp("left"), c = comp("center"), r = comp("right");
    std::string bl = comp("bottomLeft"), bm = comp("bottomMiddle"), br = comp("bottomRight");
    float lw, lh, rw, rh, th = 0, bh2 = 0, tw, bw2, unused;
    bounds(l, lw, unused); bounds(r, rw, unused);
    if (tl.empty() && tm.empty() && tr.empty() && bl.empty() && bm.empty() && br.empty()) {
        // three pieces in a row
        float cw = w - lw - rw;
        draw(l, left, top, lw, h);
        draw(c, left + lw, top, cw, h);
        draw(r, left + w - rw, top, rw, h);
        return 0;
    }
    float tlw, tlh, trw, trh, blw, blh, brw, brh;
    bounds(tl, tlw, tlh); bounds(tr, trw, trh); bounds(bl, blw, blh); bounds(br, brw, brh);
    bounds(tm, tw, th); bounds(bm, bw2, bh2);
    float topH = std::max(tlh, std::max(trh, th)), botH = std::max(blh, std::max(brh, bh2));
    float leftW = std::max(tlw, std::max(lw, blw)), rightW = std::max(trw, std::max(rw, brw));
    float midW = w - leftW - rightW, midH = h - topH - botH;
    draw(tl, left, top, leftW, topH);
    draw(tm, left + leftW, top, midW, topH);
    draw(tr, left + w - rightW, top, rightW, topH);
    draw(l, left, top + topH, leftW, midH);
    draw(c, left + leftW, top + topH, midW, midH);
    draw(r, left + w - rightW, top + topH, rightW, midH);
    draw(bl, left, top + h - botH, leftW, botH);
    draw(bm, left + leftW, top + h - botH, midW, botH);
    draw(br, left + w - rightW, top + h - botH, rightW, botH);
    return 0;
}
// getCurrentTime() -> {year, month, day, hour, min, sec}; getTimeDifference(a, b) -> {days, hours, minutes, seconds}
// where a is the later time
int l_getCurrentTime(lua_State* L) {
    time_t t = time(nullptr);
    struct tm* m = localtime(&t);
    lua_newtable(L);
    auto set = [&](const char* k, int v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); };
    set("year", m->tm_year + 1900); set("month", m->tm_mon + 1); set("day", m->tm_mday);
    set("hour", m->tm_hour); set("minutes", m->tm_min); set("seconds", m->tm_sec);
    return 1;
}
int l_getTimeDifference(lua_State* L) {
    auto toTime = [&](int idx) -> double {
        struct tm m = {};
        auto get = [&](const char* k, int def) { lua_getfield(L, idx, k); int v = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : def; lua_pop(L, 1); return v; };
        if (!lua_istable(L, idx)) return 0;
        m.tm_year = get("year", 1970) - 1900; m.tm_mon = get("month", 1) - 1; m.tm_mday = get("day", 1);
        m.tm_hour = get("hour", 0); m.tm_min = get("minutes", 0); m.tm_sec = get("seconds", 0); m.tm_isdst = -1;
        return (double)mktime(&m);
    };
    // 0x420c30: the absolute difference split into days, hours, minutes and seconds
    long total = (long)std::fabs(toTime(1) - toTime(2));
    lua_newtable(L);
    auto set = [&](const char* k, long v) { lua_pushnumber(L, (double)v); lua_setfield(L, -2, k); };
    set("days", total / 86400); set("hours", total / 3600 % 24);
    set("minutes", total / 60 % 60); set("seconds", total % 60);
    return 1;
}
// ---- graphics settings. Every one takes a TARGET as its last argument (a string): an object's name, a sprite or composite sprite name, or "background" /
// "foreground" / "trajectory" / "particles". Without a target it changes the render state, exactly as before (a convenience for one value of setRenderState).
std::string TargetArg(lua_State* L, int i) { return lua_type(L, i) == LUA_TSTRING ? std::string(lua_tostring(L, i)) : std::string(); }
int GfxNumbers(lua_State* L) { int n = lua_gettop(L); return (n > 0 && lua_type(L, n) == LUA_TSTRING) ? n - 1 : n; }   // how many arguments are not the target
int l_setAlpha(lua_State* L) {
    std::string t = TargetArg(L, 2);
    if (t.empty()) P(L)->SetAlpha(N(L, 1));
    else if (P(L)->HasObject(t)) P(L)->SetObjectAlpha(t, N(L, 1));       // an object: the same alpha setObjectAlpha sets
    else P(L)->SetRenderOverride(t, 0, N(L, 1), 0, 0);
    return 0;
}
// v2.2 dedicated functions for the single values of setRenderState, which then stay as they are
int l_setTint(lua_State* L) {   // setTint(grey [, target]) or setTint(r, g, b [, target]): 0..1, above 1 brightens
    int n = GfxNumbers(L);
    float r = N(L, 1), g = n >= 2 ? N(L, 2) : r, b = n >= 3 ? N(L, 3) : r;
    std::string t = TargetArg(L, n + 1);
    if (t.empty()) P(L)->SetTint(r, g, b); else P(L)->SetRenderOverride(t, 1, r, g, b);
    return 0;
}
int l_getTint(lua_State* L) {
    float r, g, b;
    std::string t = TargetArg(L, 1);
    if (t.empty() || !P(L)->GetRenderOverride(t, 1, r, g, b)) { if (t.empty()) P(L)->GetTint(r, g, b); else r = g = b = 1; }
    lua_pushnumber(L, r); lua_pushnumber(L, g); lua_pushnumber(L, b);
    return 3;
}
int l_resetTint(lua_State* L) { std::string t = TargetArg(L, 1); if (t.empty()) P(L)->SetTint(1, 1, 1); else P(L)->SetRenderOverride(t, 1, 1, 1, 1); return 0; }
int l_setSkew(lua_State* L) { std::string t = TargetArg(L, 3); if (t.empty()) P(L)->SetRenderExtras(0, N(L, 1), N(L, 2)); else P(L)->SetRenderOverride(t, 2, N(L, 1), N(L, 2), 0); return 0; }
int l_setBend(lua_State* L) { std::string t = TargetArg(L, 3); if (t.empty()) P(L)->SetRenderExtras(1, N(L, 1), N(L, 2)); else P(L)->SetRenderOverride(t, 3, N(L, 1), N(L, 2), 0); return 0; }
int l_setRenderZ(lua_State* L) { std::string t = TargetArg(L, 2); if (t.empty()) P(L)->SetRenderExtras(2, N(L, 1), 0); else P(L)->SetRenderOverride(t, 4, N(L, 1), 0, 0); return 0; }
int l_setBlur(lua_State* L) { std::string t = TargetArg(L, 2); if (t.empty()) P(L)->SetRenderExtras(3, N(L, 1), 0); else P(L)->SetRenderOverride(t, 5, N(L, 1), 0, 0); return 0; }       // setBlur(pixels): sprites are smeared over this many pixels; 0 = sharp
int l_setPixelate(lua_State* L) { std::string t = TargetArg(L, 2); if (t.empty()) P(L)->SetRenderExtras(4, N(L, 1), 0); else P(L)->SetRenderOverride(t, 6, N(L, 1), 0, 0); return 0; }   // setPixelate(size): blocks of size x size sprite pixels; 0 = off
int l_resetBlur(lua_State* L) { std::string t = TargetArg(L, 1); if (t.empty()) P(L)->SetRenderExtras(3, 0, 0); else P(L)->SetRenderOverride(t, 5, 0, 0, 0); return 0; }
int l_resetPixelate(lua_State* L) { std::string t = TargetArg(L, 1); if (t.empty()) P(L)->SetRenderExtras(4, 0, 0); else P(L)->SetRenderOverride(t, 6, 0, 0, 0); return 0; }
// v2.4: setBlendMode(mode [, target]): "normal", "add", "multiply" or "screen" (or 0..3). getBlendMode() is not kept; the mode stays until it is set again.
int BlendFrom(lua_State* L, int i) {
    if (lua_isnumber(L, i)) return (int)lua_tonumber(L, i);
    std::string s = lua_type(L, i) == LUA_TSTRING ? lua_tostring(L, i) : "";
    if (s == "add" || s == "additive") return 1;
    if (s == "multiply") return 2;
    if (s == "screen") return 3;
    return 0;
}
int l_setBlendMode(lua_State* L) {
    int m = BlendFrom(L, 1);
    std::string t = TargetArg(L, 2);
    if (t.empty()) P(L)->SetBlendMode(m); else P(L)->SetRenderOverride(t, 7, (float)m, 0, 0);
    return 0;
}
// v2.4: setShader(name [, target]): "none", "grayscale", "sepia", "invert", "chromatic" (colour fringes) or "glow" (0..5). The renderer is fixed-function OpenGL 1.x,
// so these are built in rather than programs you write.
int ShaderFrom(lua_State* L, int i) {
    if (lua_isnumber(L, i)) return (int)lua_tonumber(L, i);
    std::string s = lua_type(L, i) == LUA_TSTRING ? lua_tostring(L, i) : "";
    static const char* names[] = {"none", "grayscale", "sepia", "invert", "chromatic", "glow"};
    for (int k = 0; k < 6; k++) if (s == names[k]) return k;
    if (s == "greyscale" || s == "gray" || s == "grey") return 1;
    return 0;
}
int l_setShader(lua_State* L) {
    int m = ShaderFrom(L, 1);
    std::string t = TargetArg(L, 2);
    if (t.empty()) P(L)->SetShaderEffect(m); else P(L)->SetRenderOverride(t, 8, (float)m, 0, 0);
    return 0;
}
// v2.4: setTintChannel("r" | "g" | "b" | "a", value [, target]): one channel of the tint (or the alpha) and nothing else. Values above 1 brighten.
int l_setTintChannel(lua_State* L) {
    std::string c = lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : "";
    int ch = (c == "r" || c == "R" || c == "red") ? 0 : (c == "g" || c == "G" || c == "green") ? 1 : (c == "b" || c == "B" || c == "blue") ? 2 : 3;
    float v = N(L, 2);
    std::string t = TargetArg(L, 3);
    if (!t.empty()) { if (ch == 3 && P(L)->HasObject(t)) P(L)->SetObjectAlpha(t, v); else P(L)->SetRenderOverride(t, 10, (float)ch, v, 0); return 0; }
    float r, g, b;
    P(L)->GetTint(r, g, b);
    if (ch == 0) r = v; else if (ch == 1) g = v; else if (ch == 2) b = v;
    if (ch == 3) P(L)->SetAlpha(v); else P(L)->SetTint(r, g, b);
    return 0;
}
// v2.4: setVisualRotation(radians [, target]): turns what is drawn and nothing else. For an object it is setSpriteRotation (the body is not touched); for a
// sprite or group it is added to the angle of what is drawn; without a target it is the render state's angle (setRenderState's fifth value).
int l_setVisualRotation(lua_State* L) {
    std::string t = TargetArg(L, 2);
    if (t.empty()) P(L)->SetRenderExtras(5, N(L, 1), 0);
    else if (P(L)->HasObject(t)) { P(L)->SetSpriteLook(t, 0, N(L, 1), 0); }
    else P(L)->SetRenderOverride(t, 9, N(L, 1), 0, 0);
    return 0;
}
// v2.4: resetRenderOverrides(target): forgets every graphics setting made for that target
int l_resetRenderOverrides(lua_State* L) { P(L)->SetRenderOverride(S(L, 1), 11, 0, 0, 0); return 0; }
// res.getSpritePixel(sprite, x, y) -> r, g, b, a (0..255) of the pixel x pixels right and y pixels down from the sprite's top left corner, or nil
int r_getSpritePixel(lua_State* L) {
    int r, g, b, a;
    if (!P(L)->ResGetSpritePixel(S(L, 1), (int)N(L, 2), (int)N(L, 3), r, g, b, a)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, r); lua_pushnumber(L, g); lua_pushnumber(L, b); lua_pushnumber(L, a);
    return 4;
}
// res.getCompoSprites(name [, which]): "current" (default) -> { { sprite =, x =, y =, scale =, angle = }, ... } the parts the composite sprite (or the object drawn as one)
// is made of now, in drawing order; "all" -> { names } every different sprite it uses. nil if there is no such composite.
int r_getCompoSprites(lua_State* L) {
    std::vector<Platform::CompoPartInfo> parts;
    if (!P(L)->ResGetCompoParts(S(L, 1), parts)) { lua_pushnil(L); return 1; }
    bool all = lua_type(L, 2) == LUA_TSTRING && std::string(lua_tostring(L, 2)) == "all";
    lua_newtable(L);
    int i = 1;
    std::vector<std::string> seen;
    for (const auto& p : parts) {
        if (all) {
            if (std::find(seen.begin(), seen.end(), p.sprite) != seen.end()) continue;
            seen.push_back(p.sprite);
            lua_pushlstring(L, p.sprite.data(), p.sprite.size());
        } else {
            lua_newtable(L);
            lua_pushlstring(L, p.sprite.data(), p.sprite.size()); lua_setfield(L, -2, "sprite");
            lua_pushnumber(L, p.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, p.y); lua_setfield(L, -2, "y");
            lua_pushnumber(L, p.scale); lua_setfield(L, -2, "scale");
            lua_pushnumber(L, p.angle); lua_setfield(L, -2, "angle");
        }
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// drawUITextNative(text, x, y [, scaleX, scaleY [, angle [, scale]]]) is Text.draw (0x41adf0). A clipped text
// calls draw on each of its lines; any other text sets the font and the render state and draws its string.
int l_drawUITextNative(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    auto num = [&](const char* k, float def) { lua_getfield(L, 1, k); float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def; lua_pop(L, 1); return v; };
    auto flag = [&](const char* k) { lua_getfield(L, 1, k); bool v = lua_toboolean(L, -1) != 0; lua_pop(L, 1); return v; };
    auto str = [&](const char* k, const char* def) { lua_getfield(L, 1, k); std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : def; lua_pop(L, 1); return v; };
    int argc = lua_gettop(L);
    float ox = N(L, 2), oy = N(L, 3);
    float sx = argc >= 5 ? N(L, 4) : 1.0f, sy = argc >= 5 ? N(L, 5) : 1.0f;
    float angle = argc >= 6 ? N(L, 6) : 0.0f;
    // Space and Star Wars (0x4167f0): the seventh argument is the text's alpha, not a scale (the retail scripts pass self.alpha); a scale there
    // made every fading text grow and shift its pivot.
    static int spaceData = -1;   // the scripts do not change during a run
    if (spaceData < 0) spaceData = IsSpaceData(L) ? 1 : 0;
    bool alphaArg = spaceData == 1;
    float scale = (argc >= 7 && !alphaArg) ? N(L, 7) : 1.0f;
    float argAlpha = (argc >= 7 && alphaArg) ? N(L, 7) : 1.0f;
    if (!flag("visible")) return 0;
    sx *= num("scaleX", 1) * scale;
    sy *= num("scaleY", 1) * scale;
    float x = num("x", 0) * scale + ox, y = num("y", 0) * scale + oy;
    if (flag("clipped")) {
        lua_getfield(L, 1, "lines");
        int n = (int)lua_objlen(L, -1);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            lua_getfield(L, -1, "draw");
            lua_pushvalue(L, -2);
            lua_pushnumber(L, x); lua_pushnumber(L, y); lua_pushnumber(L, sx); lua_pushnumber(L, sy); lua_pushnumber(L, angle);
            if (lua_pcall(L, 6, 0, 0) != 0) { lua_pop(L, 1); }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return 0;
    }
    P(L)->ResUseFont(str("font", "FONT_BASIC"));
    float pivotX = 0, pivotY = 0;
    lua_getfield(L, 1, "rotationPivotX"); lua_getfield(L, 1, "rotationPivotY");
    if (lua_isnumber(L, -2) && lua_isnumber(L, -1)) { pivotX = (float)lua_tonumber(L, -2); pivotY = (float)lua_tonumber(L, -1); }
    lua_pop(L, 2);
    // Space and Star Wars (0x4167f0) only set the scale, angle and pivot of the render state: the translation the caller set stays,
    // which is how a button or bubble puts its text onto itself
    float keepX = 0, keepY = 0;
    if (alphaArg) P(L)->GetRenderTranslation(keepX, keepY);
    float a[8] = {keepX, keepY, sx, sy, angle, pivotX, pivotY, 1};
    lua_getfield(L, 1, "alpha");
    if (lua_isnumber(L, -1) && lua_tonumber(L, -1) < 1.0) a[7] = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (argAlpha < 1.0f) a[7] = argAlpha;
    P(L)->SetRenderState(8, a);
    x /= sx; y /= sy;
    if (flag("floorCoordinates")) { x = std::floor(x); y = std::floor(y); }
    P(L)->ResDrawString(str("group", ""), str("text", ""), x, y, str("hanchor", "LEFT"), str("vanchor", "TOP"));
    float reset[8] = {keepX, keepY, 1, 1, 0, 0, 0, 1};
    P(L)->SetRenderState(8, reset);
    return 0;
}
int l_emptyString(lua_State* L) { lua_pushstring(L, ""); return 1; }
int l_getTimeDifferenceInSeconds(lua_State* L) {
    lua_getglobal(L, "getTimeDifference");
    lua_pushvalue(L, 1); lua_pushvalue(L, 2);
    lua_call(L, 2, 1);
    double total = 0;
    const char* keys[4] = {"days", "hours", "minutes", "seconds"};
    const double mult[4] = {86400, 3600, 60, 1};
    for (int i = 0; i < 4; i++) { lua_getfield(L, -1, keys[i]); total += lua_tonumber(L, -1) * mult[i]; lua_pop(L, 1); }
    lua_pushnumber(L, total);
    return 1;
}
int l_drawLevelParticlesNative(lua_State* L) { P(L)->DrawLevelParticles((int)N(L, 1)); return 0; }
int l_noop(lua_State*) { return 0; }   // setAlpha, drawLevelParticlesNative (the game view already draws its particles)
int l_setWorldGravity(lua_State* L) { P(L)->SetWorldGravity(N(L, 1), N(L, 2)); return 0; }
int l_getWorldGravity(lua_State* L) { float x = 0, y = 0; P(L)->GetWorldGravity(x, y); lua_pushnumber(L, x); lua_pushnumber(L, y); return 2; }
int l_clearParticlesNative(lua_State* L) { P(L)->ClearParticles(); return 0; }
int l_drawGameNative(lua_State* L) { P(L)->DrawGame(); return 0; }
int l_drawBackgroundNative(lua_State* L) { P(L)->DrawBackground(); return 0; }
int l_drawForegroundNative(lua_State* L) { P(L)->DrawForeground(); return 0; }
int l_drawParticlesNative(lua_State* L) { P(L)->DrawParticles(false); return 0; }
int l_setSprite(lua_State* L) { P(L)->SetSprite(S(L, 1), S(L, 2)); return 0; }
int l_setMaterial(lua_State* L) { P(L)->SetMaterial(S(L, 1), S(L, 2)); return 0; }
int l_setTexture(lua_State* L) { P(L)->SetTexture(S(L, 1), S(L, 2)); return 0; }
int l_setTheme(lua_State* L) { P(L)->SetTheme(S(L, 1)); return 0; }
int l_startNewTrajectory(lua_State* L) { P(L)->StartNewTrajectory(); return 0; }
int l_addToTrajectory(lua_State* L) { P(L)->AddToTrajectory(N(L, 1), N(L, 2), N(L, 3)); return 0; }
int l_setCustomTrajectorySprites(lua_State* L) {
    auto S = [&](int i) { const char* v = lua_tostring(L, i); return std::string(v ? v : ""); };
    P(L)->SetCustomTrajectorySprites(S(1), S(2), S(3), S(4));
    return 0;
}
int l_addPuffToTrajectory(lua_State* L) { P(L)->AddPuffToTrajectory(N(L, 1), N(L, 2), N(L, 3)); return 0; }

int l_clipText(lua_State* L) {
    float widest = 0;
    std::vector<std::string> lines = P(L)->ClipText(S(L, 1), S(L, 2), N(L, 3), widest);
    PushEnv(L);
    lua_getfield(L, -1, "clippedText");
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        PushEnv(L);
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, "clippedText");
        lua_pop(L, 1);
    }
    lua_newtable(L);
    for (size_t i = 0; i < lines.size(); i++) {
        lua_pushlstring(L, lines[i].data(), lines[i].size());
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_setfield(L, -2, "lines");
    lua_pushnumber(L, widest);
    lua_setfield(L, -2, "widestLine");
    lua_pop(L, 1);
    return 0;
}

// ---- platform services that a new engine answers with constants
int l_nothing(lua_State*) { return 0; }
int l_playVideo(lua_State* L) { P(L)->PlayVideo(S(L, 1)); return 0; }
int l_stopVideo(lua_State* L) { P(L)->StopVideo(); return 0; }
int l_isVideoPlaying(lua_State* L) { lua_pushboolean(L, P(L)->IsVideoPlaying()); return 1; }
int l_false(lua_State* L) { lua_pushboolean(L, 0); return 1; }
// The exe's device id (0x499860): the MAC address of each network adapter as AA-BB-CC-DD-EE-FF, joined by ';'.
// The registration the game keeps in its save file is tied to this id, so it has to be the same text.
std::string DeviceId() {
    std::string id;
#ifdef _WIN32
    ULONG size = 0;
    GetAdaptersInfo(nullptr, &size);
    if (size == 0) return "ab-device-0";
    std::vector<unsigned char> buf(size);
    IP_ADAPTER_INFO* a = (IP_ADAPTER_INFO*)buf.data();
    if (GetAdaptersInfo(a, &size) != NO_ERROR) return "ab-device-0";
    for (; a; a = a->Next) {
        static const char* hex = "0123456789ABCDEF";
        if (!id.empty() || a != (IP_ADAPTER_INFO*)buf.data()) id += ';';
        for (UINT i = 0; i < a->AddressLength; i++) {
            id += hex[a->Address[i] >> 4];
            id += hex[a->Address[i] & 15];
            if (i + 1 < a->AddressLength) id += '-';
        }
    }
#else
    id = "ab-device-0";
#endif
    return id;
}
// getHardwareModel(): the model string of the device (a PC build answers with its deviceModel, "windows" unless config.lua says otherwise)
int l_getHardwareModel(lua_State* L) {
    PushEnv(L);
    lua_getfield(L, -1, "deviceModel");
    if (lua_isstring(L, -1)) lua_pushstring(L, lua_tostring(L, -1)); else lua_pushstring(L, "windows");
    lua_remove(L, -2); lua_remove(L, -2);
    return 1;
}
int l_getDeviceID(lua_State* L) { lua_pushstring(L, DeviceId().c_str()); return 1; }
int l_areDeviceIDsEqual(lua_State* L) {
    const char* a = lua_tostring(L, 1);
    const char* b = lua_tostring(L, 2);
    lua_pushboolean(L, a && b && std::string(a) == b);
    return 1;
}

// 2.0.0 (0x499ac0): the id the save file kept matches when any one of its adapters is also one of this machine's
int l_verifyDeviceID(lua_State* L) {
    const char* kept = lua_tostring(L, 1);
    bool match = false;
    if (kept && *kept) {
        auto split = [](const std::string& s) { std::vector<std::string> v; size_t p = 0; while (p <= s.size()) { size_t q = s.find(';', p); if (q == std::string::npos) q = s.size(); if (q > p) v.push_back(s.substr(p, q - p)); p = q + 1; } return v; };
        std::vector<std::string> a = split(kept), b = split(DeviceId());
        for (const std::string& x : a) for (const std::string& y : b) if (x == y) match = true;
    }
    lua_pushboolean(L, match);
    return 1;
}

// ---- res.*
int r_setPath(lua_State* L) { P(L)->ResSetPath(S(L, 1)); return 0; }
int r_createSpriteSheet(lua_State* L) { P(L)->ResCreateSpriteSheet(S(L, 1)); return 0; }
int r_releaseSpriteSheet(lua_State* L) { P(L)->ResReleaseSpriteSheet(S(L, 1)); return 0; }
int r_createCompoSpriteSet(lua_State* L) { P(L)->ResCreateCompoSpriteSet(S(L, 1)); return 0; }
int r_releaseCompoSpriteSet(lua_State* L) { P(L)->ResReleaseCompoSpriteSet(S(L, 1)); return 0; }
int r_createBitmapFont(lua_State* L) { P(L)->ResCreateBitmapFont(S(L, 1)); return 0; }
int r_useFont(lua_State* L) { P(L)->ResUseFont(S(L, 1)); return 0; }
int r_createTextGroupSet(lua_State* L) { P(L)->ResCreateTextGroupSet(S(L, 1)); return 0; }
int r_loadLocale(lua_State* L) { P(L)->ResLoadLocale(S(L, 1), S(L, 2)); return 0; }
int r_useLocale(lua_State* L) { P(L)->ResUseLocale(S(L, 1)); return 0; }
int r_getString(lua_State* L) { lua_pushstring(L, P(L)->ResGetString(S(L, 1), S(L, 2)).c_str()); return 1; }
// An anchor word sets the vertical or the horizontal anchor, whichever it names (game::parseAnchorString, 0x43b4b0).
void ParseAnchor(const char* word, int& v, int& h) {
    static const char* vertical[] = {"TOP", "VCENTER", "BOTTOM", "BASELINE", "VPIVOT"};
    static const char* horizontal[] = {"LEFT", "HCENTER", "RIGHT", "HPIVOT"};
    for (int i = 0; i < 5; i++) if (std::strcmp(word, vertical[i]) == 0) v = i;
    for (int i = 0; i < 4; i++) if (std::strcmp(word, horizontal[i]) == 0) h = i;
}
int r_drawSprite(lua_State* L) {
    // drawSprite(sprite, x, y [, vAnchor, hAnchor [, width, height]]) or, with a sheet name in front,
    // drawSprite(sheet, sprite, x, y, ...): a string as the second argument means the sheet form ("" = any sheet)
    if (lua_isnil(L, 1) && lua_type(L, 2) == LUA_TSTRING) { lua_pushstring(L, ""); lua_replace(L, 1); }   // a nil sheet means any sheet
    int o = lua_type(L, 2) == LUA_TSTRING ? 1 : 0;
    int argc = lua_gettop(L) - o;
    int v = 4, h = 3;                       // default: draw so that the sprite's pivot is at (x, y)
    if (argc > 3) ParseAnchor(luaL_checkstring(L, 4 + o), v, h);
    if (argc > 4) ParseAnchor(luaL_checkstring(L, 5 + o), v, h);
    bool sized = argc >= 7;
    P(L)->ResDrawSprite(S(L, 1 + o), N(L, 2 + o), N(L, 3 + o), v, h, sized, sized ? N(L, 6 + o) : 0.0f, sized ? N(L, 7 + o) : 0.0f);
    return 0;
}
// drawCompoSprite is drawSprite (0x4307f0); the name is looked up among the sprites and the composites
// drawTexturedLine2D(sprite, x1, y1, x2, y2, width [, r, g, b, a])   colour 0 to 255, 255 when left out
int r_drawTexturedLine2D(lua_State* L) {
    P(L)->DrawTexturedLine2D(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6),
        lua_isnoneornil(L, 7) ? 255.0f : N(L, 7), lua_isnoneornil(L, 8) ? 255.0f : N(L, 8), lua_isnoneornil(L, 9) ? 255.0f : N(L, 9), lua_isnoneornil(L, 10) ? 255.0f : N(L, 10));
    return 0;
}
int r_drawCompoSprite(lua_State* L) { return r_drawSprite(L); }
int r_drawString(lua_State* L) {
    P(L)->ResDrawString(S(L, 1), S(L, 2), N(L, 3), N(L, 4), S(L, 5), S(L, 6));
    return 0;
}
int r_setClipRect(lua_State* L) { P(L)->ResSetClipRect(N(L, 1), N(L, 2), N(L, 3), N(L, 4)); return 0; }
// getSpriteBounds(sheet, sprite) or getSpriteBounds(sprite): "" as the sheet means any sheet
int r_getSpriteBounds(lua_State* L) {
    if (lua_isnil(L, 1) && lua_gettop(L) >= 2) { lua_pushstring(L, ""); lua_replace(L, 1); }   // a nil sheet means any sheet
    float w, h;
    if (lua_gettop(L) >= 2) P(L)->ResGetSpriteBounds(S(L, 1), S(L, 2), w, h);
    else P(L)->ResGetSpriteBounds("", S(L, 1), w, h);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}
int r_getSpritePivot(lua_State* L) {
    if (lua_isnil(L, 1) && lua_gettop(L) >= 2) { lua_pushstring(L, ""); lua_replace(L, 1); }   // a nil sheet means any sheet
    float x, y;
    if (lua_gettop(L) >= 2) P(L)->ResGetSpritePivot(S(L, 1), S(L, 2), x, y);
    else P(L)->ResGetSpritePivot("", S(L, 1), x, y);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}
int r_getCompoSpriteBounds(lua_State* L) {
    if (lua_isnil(L, 1) && lua_gettop(L) >= 2) { lua_pushstring(L, ""); lua_replace(L, 1); }   // a nil sheet means any sheet
    float x1, y1, x2, y2;
    if (lua_gettop(L) >= 2) P(L)->ResGetCompoSpriteBounds(S(L, 1), S(L, 2), x1, y1, x2, y2);
    else P(L)->ResGetCompoSpriteBounds("", S(L, 1), x1, y1, x2, y2);
    lua_pushnumber(L, x1);
    lua_pushnumber(L, y1);
    lua_pushnumber(L, x2);
    lua_pushnumber(L, y2);
    return 4;
}
int r_getStringWidth(lua_State* L) { lua_pushnumber(L, P(L)->ResGetStringWidth(S(L, 1))); return 1; }
int r_getFontHeight(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontHeight()); return 1; }
int r_getFontTracking(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontTracking()); return 1; }
int r_getFontLeading(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontLeading()); return 1; }
int r_getFontMaxAscending(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontMaxAscending()); return 1; }
int r_getFontMaxDescending(lua_State* L) { lua_pushnumber(L, P(L)->ResGetFontMaxDescending()); return 1; }
int r_createAudioOutput(lua_State* L) { P(L)->ResCreateAudioOutput(N(L, 1), N(L, 2), N(L, 3)); return 0; }
int r_startAudioOutput(lua_State* L) { P(L)->ResStartAudioOutput(); return 0; }
int r_stopAudioOutput(lua_State* L) { P(L)->ResStopAudioOutput(); return 0; }
int r_createAudio(lua_State* L) { P(L)->ResCreateAudio(S(L, 1), S(L, 2)); return 0; }
int r_playAudio(lua_State* L) { P(L)->ResPlayAudio(S(L, 1), NOpt(L, 2, 1), B(L, 3), (int)NOpt(L, 4, 0)); return 0; }
int r_stopAudio(lua_State* L) { P(L)->ResStopAudio(S(L, 1)); return 0; }
int r_stopAllAudio(lua_State* L) { P(L)->ResStopAllAudio(); return 0; }
int r_isAudioPlaying(lua_State* L) { lua_pushboolean(L, P(L)->ResIsAudioPlaying(S(L, 1))); return 1; }
// note the order: setTrackVolume(volume, track)
int r_setTrackVolume(lua_State* L) { P(L)->ResSetTrackVolume(N(L, 1), (int)N(L, 2)); return 0; }
int r_getTrackVolume(lua_State* L) { lua_pushnumber(L, P(L)->ResGetTrackVolume((int)N(L, 1))); return 1; }
// v2.2: setAudioSpeed(speed [, track]) / getAudioSpeed([track]), pauseAudio([track]) / resumeAudio([track]) / isAudioPaused([track]),
// setMasterVolume(v) / getMasterVolume(), setAudioClipVolume(name, v), setAudioClipSpeed(name, speed). Without a track (or -1) they act on everything.
int r_setAudioSpeed(lua_State* L) { P(L)->ResSetAudioSpeed(N(L, 1), lua_isnumber(L, 2) ? (int)N(L, 2) : -1); return 0; }
int r_getAudioSpeed(lua_State* L) { lua_pushnumber(L, P(L)->ResGetAudioSpeed(lua_isnumber(L, 1) ? (int)N(L, 1) : -1)); return 1; }
int r_pauseAudio(lua_State* L) { P(L)->ResPauseAudio(true, lua_isnumber(L, 1) ? (int)N(L, 1) : -1); return 0; }
int r_resumeAudio(lua_State* L) { P(L)->ResPauseAudio(false, lua_isnumber(L, 1) ? (int)N(L, 1) : -1); return 0; }
int r_isAudioPaused(lua_State* L) { lua_pushboolean(L, P(L)->ResIsAudioPaused(lua_isnumber(L, 1) ? (int)N(L, 1) : -1)); return 1; }
int r_setMasterVolume(lua_State* L) { P(L)->ResSetMasterVolume(N(L, 1)); return 0; }
int r_getMasterVolume(lua_State* L) { lua_pushnumber(L, P(L)->ResGetMasterVolume()); return 1; }
int r_setAudioClipVolume(lua_State* L) { P(L)->ResSetAudioClipVolume(S(L, 1), N(L, 2)); return 0; }
int r_setAudioClipSpeed(lua_State* L) { P(L)->ResSetAudioClipSpeed(S(L, 1), N(L, 2)); return 0; }
// v2.4: setAudioClipPitch(name, p) (speed and pitch together), setAudioClipTempo(name, t) (speed alone), setAudioClipPan(name, -1..1), resetAudioClipEffects(name),
// seekAudioClip(name, seconds) -> true if a voice of it plays, getAudioClipTime(name) -> seconds or -1, getAudioClipLength(name) -> seconds or -1.
// All of them also work before the clip is played: the setting is kept for the name and every later play of it starts with it.
// res.setString(group, key, text): sets one text of a text group (the group is made when it is new), so scripts and the console can inject wholly new text; res.getString reads it back
int r_setString(lua_State* L) { P(L)->ResSetString(S(L, 1), S(L, 2), S(L, 3)); return 0; }
int r_setAudioClipPitch(lua_State* L) { P(L)->ResSetAudioClipPitch(S(L, 1), N(L, 2)); return 0; }
int r_setAudioClipTempo(lua_State* L) { P(L)->ResSetAudioClipTempo(S(L, 1), N(L, 2)); return 0; }
int r_setAudioClipPan(lua_State* L) { P(L)->ResSetAudioClipPan(S(L, 1), N(L, 2)); return 0; }
int r_resetAudioClipEffects(lua_State* L) { P(L)->ResResetAudioClipEffects(S(L, 1)); return 0; }
int r_seekAudioClip(lua_State* L) { lua_pushboolean(L, P(L)->ResSeekAudioClip(S(L, 1), N(L, 2))); return 1; }
int r_getAudioClipTime(lua_State* L) { lua_pushnumber(L, P(L)->ResAudioClipTime(S(L, 1))); return 1; }
int r_getAudioClipLength(lua_State* L) { lua_pushnumber(L, P(L)->ResAudioClipLength(S(L, 1))); return 1; }
int r_openURL(lua_State* L) { P(L)->ResOpenURL(S(L, 1)); return 0; }

// ---- particles.*
bool RioMode(lua_State* L) {
    PushEnv(L);
    lua_getfield(L, -1, "rioMode");
    bool rio = lua_toboolean(L, -1) != 0;
    lua_pop(L, 2);
    return rio;
}
int p_addParticles(lua_State* L) {
    // Angry Birds Rio: the background confetti is the one emitter that ignores the limits, and it is drawn in the level's
    // two layers (drawLevelParticlesNative), so it goes to the level list, which the limits do not count
    if (RioMode(L) && lua_toboolean(L, 8) != 0) {
        P(L)->AddLevelParticles(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), true);
        return 0;
    }
    // Angry Birds Rio: a bird or crate landing on the ground puffs one dust particle to the left and one to the right
    // (the script's default amount of 10 would make a cloud)
    float amount = N(L, 2);
    if (RioMode(L)) {
        std::string type = S(L, 1);
        if (type.compare(0, 10, "groundDust") == 0 || type.compare(0, 15, "groundCrateDust") == 0) amount = 1;
    }
    // Seasons adds two booleans: ignore the particle limits, and put the particles in the second list
    P(L)->AddParticles(S(L, 1), amount, N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), lua_toboolean(L, 8) != 0, lua_toboolean(L, 9) != 0);
    return 0;
}
// 0x42cae0: addLevelParticles(type, amount, x, y, w, h, angle, ignoreLimits) -> first list
int p_addLevelParticles(lua_State* L) {
    P(L)->AddLevelParticles(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), lua_toboolean(L, 8) != 0);
    return 0;
}
// Angry Birds Space: addParticlesWithScale(type, amount, x, y, w, h, angle, scaleBegin, scaleEnd) and addParticlesWithProperties(type, amount, x, y, w, h, angle, minVel, maxVel, minEmitterAngle, maxEmitterAngle)
// (0x494900, 0x493280): the call's values replace those of the particle definition
int p_addParticlesWithScale(lua_State* L) {            // (type, amount, x, y, w, h, angle, scaleBegin, scaleEnd)
    Platform::ParticleOverride ov; ov.kind = 1; ov.a = N(L, 8); ov.b = N(L, 9);
    P(L)->AddParticlesOverride(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), ov);
    return 0;
}
int p_addParticlesWithProperties(lua_State* L) {       // (type, amount, x, y, w, h, angle, minVel, maxVel, minEmitterAngle, maxEmitterAngle in radians)
    Platform::ParticleOverride ov; ov.kind = 2; ov.a = N(L, 8); ov.b = N(L, 9); ov.c = N(L, 10); ov.d = N(L, 11);
    P(L)->AddParticlesOverride(S(L, 1), N(L, 2), N(L, 3), N(L, 4), N(L, 5), N(L, 6), N(L, 7), ov);
    return 0;
}
// Star Wars: particles.native_addParticlesWithMode({definitionName, amount, x, y, w, h, angle, mode})
int p_addParticlesWithMode(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto field = [L](const char* key) { lua_getfield(L, 1, key); float v = (float)lua_tonumber(L, -1); lua_pop(L, 1); return v; };
    lua_getfield(L, 1, "definitionName");
    std::string type = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    if (type.empty()) return 0;
    P(L)->AddParticlesMode(type, field("amount"), field("x"), field("y"), field("w"), field("h"), field("angle"), (int)field("mode"));
    return 0;
}
// readDataFile(path) -> the file as a string (decrypted and unpacked like the scripts), or nil: the Star Wars animation files are encrypted JSON
int l_readDataFile(lua_State* L) {
    std::string path = S(L, 1);
    std::vector<uint8_t> bytes;
    bool found = ab::ReadLuaFile(path, bytes);
    if (!found) { std::string alt = P(L)->ResolveDataFile(path); found = alt != path && ab::ReadLuaFile(alt, bytes); }
    if (!found || bytes.empty()) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char*)bytes.data(), bytes.size());
    return 1;
}
int l_drawParticlesFiltered(lua_State* L) { P(L)->DrawParticlesFiltered((int)N(L, 1)); return 0; }
int l_setRestitution(lua_State* L) { P(L)->SetObjectRestitution(S(L, 1), N(L, 2)); return 0; }
int l_getFullScreenWidth(lua_State* L) { lua_getfield(L, LUA_REGISTRYINDEX, "ab.env"); lua_getfield(L, -1, "screenWidth"); lua_remove(L, -2); if (!lua_isnumber(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 0); } return 1; }
int l_getFullScreenHeight(lua_State* L) { lua_getfield(L, LUA_REGISTRYINDEX, "ab.env"); lua_getfield(L, -1, "screenHeight"); lua_remove(L, -2); if (!lua_isnumber(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 0); } return 1; }
int l_performBitwiseOr(lua_State* L) { lua_pushnumber(L, (double)((int)N(L, 1) | (int)N(L, 2))); return 1; }   // 0x40dcb0
// Retail natives (names from the registration table in AngryBirdsStarWars.exe)
int l_setWaterForceMultiplier(lua_State* L) { float r = 0; P(L)->BodyCommand("", 90, (float)N(L, 1), r); return 0; }   // 0x40d490 (game +0x3cc, 2 until set)
int l_getWaterForceMultiplier(lua_State* L) { float r = 2; P(L)->BodyCommand("", 91, 0, r); lua_pushnumber(L, r); return 1; }   // 0x40d4a0
int l_setBirdWaterDrag(lua_State* L) { float r = 0; P(L)->BodyCommand("", 92, (float)N(L, 1), r); return 0; }       // 0x40d4b0 (+0x3d0, 0.4 until set)
int l_setObjectWaterDrag(lua_State* L) { float r = 0; P(L)->BodyCommand("", 93, (float)N(L, 1), r); return 0; }   // 0x40d4c0 (+0x3d4, 0 until set)
int l_fileExistsInAppData(lua_State* L) {   // 0x421ee0
    std::vector<uint8_t> bytes;
    lua_pushboolean(L, ab::ReadLuaFile(S(L, 1), bytes) && !bytes.empty());
    return 1;
}
int l_loadTextFileToString(lua_State* L) {   // 0x419560
    std::vector<uint8_t> bytes;
    std::string path = S(L, 1);
    bool found = ab::ReadLuaFile(path, bytes);
    if (!found) { std::string alt = P(L)->ResolveDataFile(path); found = alt != path && ab::ReadLuaFile(alt, bytes); }
    if (!found) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char*)bytes.data(), bytes.size());
    return 1;
}
int l_isGameOverlayActive(lua_State* L) { lua_pushboolean(L, 0); return 1; }   // 0x40d450 stub in retail
int l_retailNoop(lua_State*) { return 0; }   // clearScreen, refreshCurrentLocale, recoverRenderObjects, ...
int l_setObjectDensity(lua_State* L) { P(L)->SetObjectDensity(S(L, 1), N(L, 2)); return 0; }
int r_keepLastFrame(lua_State* L) { P(L)->ResKeepLastFrame(lua_toboolean(L, 1) != 0); return 0; }
int r_spawnFGLayerParticles(lua_State* L) { P(L)->SpawnFGLayerParticles((int)lua_tonumber(L, 1)); return 0; }
int r_captureSprite(lua_State* L) { P(L)->ResCaptureSprite(S(L, 1)); return 0; }
int p_setSoftLimit(lua_State* L) { P(L)->SetParticleSoftLimit(N(L, 1), N(L, 2)); return 0; }
int p_setHardLimit(lua_State* L) { P(L)->SetParticleHardLimit(N(L, 1)); return 0; }

struct Entry { const char* name; lua_CFunction fn; };

// table == 0 registers into the script environment, otherwise into the table at that stack index.
void Register(lua_State* L, Platform* p, int table, const Entry* entries, size_t count) {
    bool pushedEnv = (table == 0);
    if (pushedEnv) { PushEnv(L); table = lua_gettop(L); }
    for (size_t i = 0; i < count; i++) {
        lua_pushlightuserdata(L, p);
        lua_pushcclosure(L, entries[i].fn, 1);
        lua_setfield(L, table, entries[i].name);
    }
    if (pushedEnv) lua_pop(L, 1);
}

// The Rio level editor files every block under `group` / `index` (blockTable.groups names the ten groups, with their keys).
// The shipped definition files carry neither for most blocks (the list a block was defined in is all that is left), so the editor's
// palette is built from the list: a block belongs to the group named like its list, at its position in it. Only the editor reads
// the two fields.
void FileInEditorPalette(lua_State* L, int key, int entry, int position) {
    static const char* const groups[] = {"staticBlocks", "staticFragile", "glassBlocks", "woodBlocks", "stoneBlocks",
                                         "miscBlocks", "birds", "levelGoals", "decorationBlocks", "scoreObjects"};
    if (lua_type(L, key) != LUA_TSTRING) return;
    std::string list = lua_tostring(L, key);
    lua_getfield(L, entry, "group");
    std::string own = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    auto known = [&](const std::string& name) { for (const char* g : groups) if (name == g) return true; return false; };
    if (own.empty() || (!known(own) && known(list))) {
        lua_pushstring(L, list.c_str());
        lua_setfield(L, entry, "group");
    }
    lua_pushnumber(L, position);
    lua_setfield(L, entry, "index");
}

}  // namespace

void RegisterApi(lua_State* L, Platform* p) {
    static const Entry globals[] = {
        {"requestExit", l_requestExit}, {"print", l_print}, {"printGlobals", l_printGlobals},
        {"setBGColor", l_setBGColor}, {"captureMouse", l_captureMouse}, {"isMouseCaptured", l_isMouseCaptured},
        {"setFullScreenMode", l_setFullScreenMode}, {"isInFullScreenMode", l_isInFullScreenMode},
        {"setResolution", l_setResolution},
        {"setIsMultitouchMouseWheelSimulationEnabled", l_setIsMultitouchMouseWheelSimulationEnabled},
        {"doesMouseClickSetsTouchCount", l_doesMouseClickSetsTouchCount},
        {"setEditing", l_setEditing}, {"setGameOn", l_setGameOn},
        {"loadLuaFileToObject", l_loadLuaFileToObject}, {"loadLuaFile", l_loadLuaFileToObject},   // 2.0.0: the same without an object
        {"loadLevel", l_loadLevel}, {"loadLevelEditor", l_loadLevelEditor}, {"saveLevel", l_saveLevel}, {"saveLevelForReload", l_saveLevelForReload}, {"saveLuaFile", l_saveLuaFile}, {"checkForPersistentFile", l_checkForPersistentFile}, {"loadPersistentFileWrapper", l_loadPersistentFileWrapper},
        {"initializePainter", l_initializePainter}, {"clearCanvas", l_clearCanvas}, {"paintToCanvas", l_paintToCanvas}, {"drawCanvasWithMask", l_drawCanvasWithMask},
        {"getDominantCanvasColors", l_getDominantCanvasColors}, {"createSpriteFromCanvas", l_createSpriteFromCanvas}, {"loadCanvasSprite", l_loadCanvasSprite}, {"createSilhouette", l_createSilhouette}, {"predictCollision", l_predictCollision}, {"setObjectGravity", l_setObjectGravity}, {"setObjectGravityScale", l_setObjectGravityScale}, {"resetObjectGravity", l_resetObjectGravity}, {"getObjectGravity", l_getObjectGravity}, {"setupCompoObject", l_setupCompoObject},
        {"createDirectory", l_createDirectory}, {"checkDirectory", l_checkDirectory}, {"checkForLuaFile", l_checkForLuaFile},
        {"createBox", l_createBox}, {"createCircle", l_createCircle}, {"createPolygon", l_createPolygon}, {"createLineShape", l_createLineShape},
        {"clearVertices", l_clearVertices}, {"addVertex", l_addVertex},
        {"getVelocity", l_getVelocity}, {"createCompoundBodyFromTable", l_createCompoundBodyFromTable}, {"getLinearVelocity", l_getLinearVelocity}, {"getRayCastedObjects", l_getRayCastedObjects}, {"getBGColor", l_getBGColor}, {"hasMovingObjects", l_hasMovingObjects},
        {"hasMovingObjectsZeroTolerance", l_hasMovingObjectsZeroTolerance}, {"getLocalPoint", l_getLocalPoint},
        {"setCollisionEnabled", l_setCollisionEnabled},
        {"setObjectAlpha", l_setObjectAlpha}, {"getObjectAlpha", l_getObjectAlpha}, {"setObjectZOrder", l_setObjectZOrder}, {"getObjectZOrder", l_getObjectZOrder},
        {"bringObjectToFront", l_bringObjectToFront}, {"sendObjectToBack", l_sendObjectToBack}, {"getObjectDrawOrder", l_getObjectDrawOrder},
        {"getRenderZ", l_getRenderZ}, {"setColliderType", l_setColliderType}, {"getColliderType", l_getColliderType}, {"setColliderRule", l_setColliderRule}, {"getColliderRule", l_getColliderRule},
        {"setPolygonRadius", l_setPolygonRadius}, {"getPolygonRadius", l_getPolygonRadius}, {"removeJointsFromObject", l_removeJointsFromObject},
        {"getIntersectingObjects", l_getIntersectingObjects},
        {"setParticleSystemLimits", l_nothing}, {"clearParticleCache", l_nothing}, {"enableInGameParticlesNative", l_nothing},
        {"updateBackgroundAndForegroundScrollingNative", l_updateBackgroundAndForegroundScrollingNative}, {"rotateThemeSprites", l_rotateThemeSprites},
        {"setThemeSprite", l_setThemeSprite}, {"drawMenuParticlesNative", l_nothing}, {"updateMenuParticlesNative", l_nothing}, {"isHDVersion", l_false},
        {"createJoint", l_createJoint}, {"createJoints", l_createJoints}, {"setJointParameters", l_setJointParameters},
        {"checkJointLimits", l_checkJointLimits}, {"destroyJoint", l_destroyJoint},
        {"removeObject", l_removeObject}, {"savePersistentLuaFile", l_savePersistentLuaFile}, {"applyImpulse", l_applyImpulse}, {"applyForce", l_applyForce}, {"applyForceNative", l_applyForce}, {"setObjectDecoration", l_setObjectDecoration}, {"setNormalTrailSprite", l_setNormalTrailSprite}, {"setSpecialTrailSprite", l_setSpecialTrailSprite}, {"setSpriteRotation", l_setSpriteRotation}, {"setPivotOffset", l_setPivotOffset}, {"setTextureScale", l_setTextureScale}, {"setRecordVelocity", l_setRecordVelocity},
        {"setPosition", l_setPosition}, {"setRotation", l_setRotation}, {"setVelocity", l_setVelocity},
        {"setAngularVelocity", l_setAngularVelocity}, {"setLinearDamping", l_setLinearDamping}, {"clearShapesArray", l_clearShapesArray}, {"addBoxToCompoundList", l_addBoxToCompoundList}, {"addPolygonToCompoundList", l_addPolygonToCompoundList}, {"addLineToCompoundList", l_addLineToCompoundList}, {"addCircleToCompoundList", l_addCircleToCompoundList}, {"setCompoundShapeLinearDamping", l_setCompoundShapeLinearDamping}, {"setCompoundShapeParameter", l_setCompoundShapeParameter}, {"createCompoundObject", l_createCompoundObject}, {"finalizeCompoundObject", l_finalizeCompoundObject}, {"enableAimingAid", l_enableAimingAid}, {"setAimingAidSprite", l_setAimingAidSprite}, {"clearAimingAid", l_clearAimingAid}, {"populateAimingAid", l_populateAimingAid}, {"updateBirdTrajectoryTable", l_updateBirdTrajectoryTable}, {"simulateAimingAid", l_simulateAimingAid}, {"spriteExists", l_spriteExists}, {"setSensorGravityMask", l_setSensorGravityMask}, {"setRevertGravity", l_setRevertGravity}, {"setRevertGravityWithMultiplier", l_setRevertGravityWithMultiplier}, {"setObjectGravityCategory", l_setObjectGravityCategory},
        {"setSensorMinimumAndMaximumForces", l_setSensorMinimumAndMaximumForces}, {"setGravityForceMultiplier", l_setGravityForceMultiplier},
        {"getGravityForceMultiplier", l_getGravityForceMultiplier}, {"setAngularDamping", l_setAngularDamping},
        {"setFixedRotation", l_setFixedRotation}, {"setActive", l_setActive}, {"multiplyVelocity", l_multiplyVelocity}, {"setAsSensor", l_setAsSensor},
        {"getAngularVelocity", l_getAngularVelocity}, {"setSleeping", l_setSleeping},
        {"getAngle", l_getAngle}, {"getWorldPoint", l_getWorldPoint}, {"setObjectParameter", l_setObjectParameter}, {"setScale", l_setScale}, {"getScale", l_getScale}, {"setSpeedCap", l_setSpeedCap}, {"getSpeedCap", l_getSpeedCap}, {"clearSpeedCap", l_clearSpeedCap}, {"getTouchingObjects", l_getTouchingObjects}, {"rayCast", l_rayCast}, {"fileExists", l_fileExists},
        {"setPhysicsSimulationScale", l_setPhysicsSimulationScale}, {"setPhysicsEnabled", l_setPhysicsEnabled}, {"setThemeOffsetY", l_setThemeOffsetY}, {"setStartingCameraValue", l_setStartingCameraValue},
        {"isPhysicsEnabled", l_isPhysicsEnabled}, {"setLevelLimits", l_setLevelLimits},
        {"setTopLeft", l_setTopLeft}, {"setWorldScale", l_setWorldScale}, {"setMaxWorldScale", l_setMaxWorldScale}, {"resetMouseWheelScale", l_resetMouseWheelScale},
        {"setMaxTranslation", l_setMaxTranslation},
        {"setRenderState", l_setRenderState}, {"drawRect", l_drawRect}, {"drawLine2D", l_drawLine2D},
        {"drawTexturedRect", l_nothing}, {"drawSome", l_nothing},
        {"drawGameNative", l_drawGameNative}, {"drawBackgroundNative", l_drawBackgroundNative},
        {"drawForegroundNative", l_drawForegroundNative}, {"drawParticlesFiltered", l_drawParticlesFiltered}, {"readDataFile", l_readDataFile}, {"setObjectDensity", l_setObjectDensity}, {"setRestitution", l_setRestitution}, {"handleJointLimits", l_handleJointLimits}, {"getFullScreenWidth", l_getFullScreenWidth}, {"getFullScreenHeight", l_getFullScreenHeight}, {"performBitwiseOr", l_performBitwiseOr}, {"setWaterForceMultiplier", l_setWaterForceMultiplier}, {"getWaterForceMultiplier", l_getWaterForceMultiplier}, {"setBirdWaterDrag", l_setBirdWaterDrag}, {"setObjectWaterDrag", l_setObjectWaterDrag}, {"fileExistsInAppData", l_fileExistsInAppData}, {"loadTextFileToString", l_loadTextFileToString}, {"isGameOverlayActive", l_isGameOverlayActive}, {"clearScreen", l_retailNoop}, {"refreshCurrentLocale", l_retailNoop}, {"recoverRenderObjects", l_retailNoop}, {"drawParticlesNative", l_drawParticlesNative},
        {"setSprite", l_setSprite}, {"setMaterial", l_setMaterial}, {"setTexture", l_setTexture}, {"setTheme", l_setTheme},
        {"clipText", l_clipText},
        {"startNewTrajectory", l_startNewTrajectory}, {"addToTrajectory", l_addToTrajectory},
        {"addPuffToTrajectory", l_addPuffToTrajectory}, {"setCustomTrajectorySprites", l_setCustomTrajectorySprites},
        // platform services
        {"openRegistrationDialog", l_nothing}, {"registerKey", l_nothing}, {"getDeviceID", l_getDeviceID}, {"getHardwareModel", l_getHardwareModel}, {"verifyDeviceID", l_verifyDeviceID}, {"areDeviceIDsEqual", l_areDeviceIDsEqual},
        {"requestLatestVersionNumber", l_nothing}, {"checkForUpdates", l_nothing}, {"callVersionUpdater", l_nothing},
        {"goToTaskSwitcherLua", l_nothing}, {"activateCrystalUI", l_nothing}, {"activateCrystalUIAtProfile", l_nothing},
        {"deactivateCrystalUI", l_nothing}, {"showCrystalSplash", l_nothing}, {"isCrystalSplashShowing", l_false},
        {"isCrystalUIShowing", l_false}, {"userEnabledCrystal", l_false}, {"avoidCrystalBackgroundActivity", l_nothing},
        {"initGameCenter", l_nothing}, {"getLeaderboardScoresForPlayers", l_nothing},
        {"getLeaderboardScoresForRange", l_nothing}, {"showLeaderboards", l_nothing}, {"showAchievements", l_nothing},
        {"postHighscore", l_nothing}, {"unlockAchievement", l_nothing},
        {"logFlurryEvent", l_nothing}, {"logFlurryEventWithParam", l_nothing}, {"logFlurryEventWithParams", l_nothing},
        {"playVideo", l_playVideo}, {"stopVideo", l_stopVideo}, {"isVideoPlaying", l_isVideoPlaying},
        // Angry Birds Seasons additions (not implemented yet)
        {"clearParticlesNative", l_clearParticlesNative}, {"loadLevelFile", l_nothing}, {"getLoadStatus", l_getLoadStatus},
        {"createThemeSprite", l_createThemeSprite}, {"createThemeAnimation", l_createThemeAnimation},
        {"modifyThemeSprite", l_modifyThemeSprite},
        {"removeThemeSprite", l_removeThemeSprite},
        {"updateThemeSprite", l_updateThemeSprite},
        {"drawLayer", l_nothing},
        {"drawRectLines", l_drawRectLines}, {"drawBoxNative", l_drawBoxNative}, {"getCurrentTime", l_getCurrentTime},
        {"getTimeDifference", l_getTimeDifference}, {"setAlpha", l_setAlpha}, {"setBlendMode", l_setBlendMode}, {"setShader", l_setShader}, {"setTintChannel", l_setTintChannel}, {"setVisualRotation", l_setVisualRotation}, {"resetRenderOverrides", l_resetRenderOverrides}, {"setTint", l_setTint}, {"getTint", l_getTint}, {"resetTint", l_resetTint}, {"setSkew", l_setSkew}, {"setBend", l_setBend}, {"setRenderZ", l_setRenderZ}, {"setBlur", l_setBlur}, {"setPixelate", l_setPixelate}, {"resetBlur", l_resetBlur}, {"resetPixelate", l_resetPixelate}, {"drawUITextNative", l_drawUITextNative},
        {"getTImeDifferenceInSeconds", l_getTimeDifferenceInSeconds}, {"checkRegistrationResult", l_false}, {"getClipboardText", l_emptyString},
        {"canOpenProgram", l_false}, {"openProgram", l_nothing}, {"hasLocationCapability", l_false}, {"drawLevelParticlesNative", l_drawLevelParticlesNative},
        {"setWorldGravity", l_setWorldGravity}, {"getWorldGravity", l_getWorldGravity},
        {"getOSName", l_osName}, {"getOSVersion", l_osVersion}, {"getModel", l_deviceModel},
        {"getManufacturer", l_deviceManufacturer}, {"getCurrentLocale", l_currentLocale},
        {"initLocales", l_nothing}, {"setRetinaResolution", l_nothing},
        // Angry Birds Rio: ads, analytics and the NFC eagle are not there on the PC
        {"flushMedioEvents", l_nothing}, {"logMedioAnonymousIDtoFlurry", l_nothing}, {"logMedioEvent", l_nothing},
        {"hideBannerAd", l_nothing}, {"showBannerAd", l_nothing}, {"requestBannerAd", l_nothing},
        {"requestInterstitialAd", l_nothing}, {"requestAndShowInterstitialAd", l_nothing}, {"showInterstitialAd", l_nothing},
        {"toggleNFC", l_nothing}, {"enableSmoothZooming", l_nothing}, {"setGameParameters", l_nothing},
        {"throwException", l_throwException}, {"setNotificationCallback", l_nothing}, {"requestAd", l_nothing}, {"showAd", l_nothing}, {"hideAd", l_nothing},
        {"requestExpandableAd", l_nothing}, {"showExpandableAd", l_nothing}, {"stopAds", l_nothing},
        {"clearParticles", l_clearParticlesNative},
        {"iapBuyItem", l_nothing},
        {"iapGetItemAt", l_nothing},
        {"iapGetItemCount", l_nothing},
        {"iapInitItemPurchase", l_nothing},
        {"iapInitTimeOut", l_nothing},
        {"requestAd", l_nothing},
        {"showAdvertisement", l_nothing},
        {"hideAdvertisement", l_nothing},
        {"requestInterstitial", l_nothing},
        {"showInterstitial", l_nothing},
        {"requestVideoAd", l_nothing},
        {"requestAndShowVideo", l_nothing},
        {"showVideoAdvertisement", l_nothing},
        {"enableiAds", l_nothing},
        {"disableiAds", l_nothing},
        {"addNotificationAfter", l_nothing},
        {"removeNotification", l_nothing},
        {"isWebViewLoading", l_false},
        {"videoReady", l_false},
        {"iAdsSupported", l_false},
    };
    Register(L, p, 0, globals, sizeof(globals) / sizeof(globals[0]));

    static const Entry res[] = {
        {"setPath", r_setPath}, {"createSpriteSheet", r_createSpriteSheet}, {"releaseSpriteSheet", r_releaseSpriteSheet},
        {"createCompoSpriteSet", r_createCompoSpriteSet}, {"releaseCompoSpriteSet", r_releaseCompoSpriteSet},
        {"createBitmapFont", r_createBitmapFont}, {"useFont", r_useFont},
        {"createTextGroupSet", r_createTextGroupSet}, {"loadLocale", r_loadLocale}, {"useLocale", r_useLocale},
        {"getString", r_getString}, {"drawSprite", r_drawSprite}, {"drawCompoSprite", r_drawCompoSprite}, {"drawTexturedLine2D", r_drawTexturedLine2D},
        {"drawString", r_drawString}, {"setClipRect", r_setClipRect},
        {"getSpriteBounds", r_getSpriteBounds}, {"getSpritePivot", r_getSpritePivot},
        {"getCompoSpriteBounds", r_getCompoSpriteBounds}, {"getSpritePixel", r_getSpritePixel}, {"getCompoSprites", r_getCompoSprites},
        {"getStringWidth", r_getStringWidth}, {"getFontHeight", r_getFontHeight}, {"getFontLeading", r_getFontLeading}, {"getFontTracking", r_getFontTracking},
        {"getFontMaxAscending", r_getFontMaxAscending}, {"getFontMaxDescending", r_getFontMaxDescending},
        {"createAudioOutput", r_createAudioOutput}, {"startAudioOutput", r_startAudioOutput},
        {"stopAudioOutput", r_stopAudioOutput}, {"createAudio", r_createAudio}, {"captureSprite", r_captureSprite}, {"keepLastFrame", r_keepLastFrame}, {"playAudio", r_playAudio},
        {"stopAudio", r_stopAudio}, {"stopAllAudio", r_stopAllAudio}, {"isAudioPlaying", r_isAudioPlaying},
        {"setTrackVolume", r_setTrackVolume}, {"getTrackVolume", r_getTrackVolume}, {"setAudioSpeed", r_setAudioSpeed}, {"getAudioSpeed", r_getAudioSpeed}, {"pauseAudio", r_pauseAudio}, {"resumeAudio", r_resumeAudio}, {"isAudioPaused", r_isAudioPaused}, {"setMasterVolume", r_setMasterVolume}, {"getMasterVolume", r_getMasterVolume}, {"setAudioClipVolume", r_setAudioClipVolume}, {"setAudioClipSpeed", r_setAudioClipSpeed}, {"setString", r_setString}, {"setAudioClipPitch", r_setAudioClipPitch}, {"setAudioClipTempo", r_setAudioClipTempo}, {"setAudioClipPan", r_setAudioClipPan}, {"resetAudioClipEffects", r_resetAudioClipEffects}, {"seekAudioClip", r_seekAudioClip}, {"getAudioClipTime", r_getAudioClipTime}, {"getAudioClipLength", r_getAudioClipLength}, {"openURL", r_openURL},
    };
    lua_newtable(L);
    Register(L, p, lua_gettop(L), res, sizeof(res) / sizeof(res[0]));
    lua_setglobal(L, "res");
    {   // native table of the Star Wars exe
        static const Entry themeSystem[] = {{"spawnFGLayerParticles", r_spawnFGLayerParticles}};
        lua_newtable(L);
        Register(L, p, lua_gettop(L), themeSystem, 1);
        lua_setglobal(L, "themeSystem");
    }

    static const Entry particles[] = {
        {"addParticles", p_addParticles}, {"addLevelParticles", p_addLevelParticles}, {"setSoftLimit", p_setSoftLimit}, {"setHardLimit", p_setHardLimit},
        {"native_addParticlesWithMode", p_addParticlesWithMode}, {"addParticlesWithScale", p_addParticlesWithScale}, {"addParticlesWithProperties", p_addParticlesWithProperties},
    };
    lua_newtable(L);
    Register(L, p, lua_gettop(L), particles, sizeof(particles) / sizeof(particles[0]));
    lua_setglobal(L, "particles");
}

// Runs a data file in a fresh, empty environment table and registers that table as the script
// global `name` (the exe's load_lua_file, 0x40ae20: blocks.lua -> blockTable, particles.lua -> particleTable).
bool LoadTableFile(lua_State* L, const char* name, const std::string& path, std::string* error, bool append) {
    // append: the file runs in the table that already is the global `name` (Rio fills blockTable from many files)
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, name);
    if (append && lua_istable(L, -1)) lua_remove(L, -2);
    else { lua_pop(L, 2); lua_newtable(L); }
    bool ok = false;
    // The exe decrypts the file (AES-256-CBC) before running it; a plain-text level is used as it is.
    std::vector<uint8_t> bytes;
    int loaded = 1;
    std::string loadError = "No such file or directory";
    ab::ReadLuaFile(path, bytes);
    if (!bytes.empty()) {
        loaded = luaL_loadbuffer(L, (const char*)bytes.data(), bytes.size(), path.c_str());
        if (loaded != 0) { loadError = lua_tostring(L, -1); lua_pop(L, 1); }
    }
    if (loaded == 0) {
        lua_pushvalue(L, -2);
        lua_setfenv(L, -2);
        if (lua_pcall(L, 0, 0, 0) == 0) ok = true;
    }
    if (!ok) {
        if (error) *error = path + ": " + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "error");
        lua_pop(L, 2);
        return false;
    }
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, name);
    lua_pop(L, 2);
    return true;
}

// Angry Birds Rio (1.4.x): the data comes in many files instead of blocks.lua (the exe's loadLevel, strings
// "Loading materials..." to "Loading particles...").
bool LoadRioTables(lua_State* L) {
    std::string base;
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "scriptPath");
    base = lua_isstring(L, -1) ? lua_tostring(L, -1) : "scripts";
    lua_pop(L, 2);
    std::string err;
    auto load = [&](const char* name, const char* file, bool append) {
        if (!LoadTableFile(L, name, base + file, &err, append)) {
            lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
            lua_pop(L, 1);
            std::fprintf(stderr, "loadLevel: %s\n", err.c_str());
            return false;
        }
        return true;
    };
    bool ok = true;
    // A new blockTable. Each file builds lists of definitions (pigs, birds, woodBlocks...); the exe files every
    // definition under blockTable.blocks[definition]. Plain numbers a file defines (monkeyIdleTimeIntervalMin...)
    // stay globals.
    PushEnv(L);
    lua_newtable(L);
    lua_newtable(L);
    lua_setfield(L, -2, "blocks");
    lua_setfield(L, -2, "blockTable");
    lua_pop(L, 1);
    const char* blockFiles[] = {"/pigs.lua", "/levelgoals.lua", "/scoreobjects.lua", "/birds.lua", "/blocks_hazard.lua",
                                "/blocks_wood.lua", "/blocks_stone.lua", "/blocks_glass.lua", "/blocks_misc.lua",
                                "/blocks_static.lua", "/blocks_staticfragile.lua", "/blocks_decorations.lua"};
    for (const char* f : blockFiles) {
        std::vector<uint8_t> bytes;
        ab::ReadLuaFile(base + f, bytes);
        if (bytes.empty() || luaL_loadbuffer(L, (const char*)bytes.data(), bytes.size(), f) != 0) {
            std::fprintf(stderr, "loadLevel: cannot load %s%s\n", f, bytes.empty() ? "" : (std::string(": ") + lua_tostring(L, -1)).c_str());
            if (!bytes.empty()) lua_pop(L, 1);
            ok = false;
            continue;
        }
        lua_newtable(L);                                // the file's own globals: [chunk, T]
        lua_newtable(L);
        PushEnv(L);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        lua_setfenv(L, -3);
        lua_pushvalue(L, -2);
        if (lua_pcall(L, 0, 0, 0) != 0) {
            std::fprintf(stderr, "loadLevel: %s: %s\n", f, lua_tostring(L, -1));
            lua_pop(L, 3);
            ok = false;
            continue;
        }
        lua_remove(L, -2);                              // [T]
        PushEnv(L);
        lua_insert(L, -2);                              // [env, T]
        lua_getfield(L, -2, "blockTable");
        lua_getfield(L, -1, "blocks");                  // [env, T, blockTable, blocks]
        lua_pushnil(L);
        while (lua_next(L, -4)) {                       // T's key, value
            if (lua_isnumber(L, -1) && lua_type(L, -2) == LUA_TSTRING) {
                lua_pushvalue(L, -2);
                lua_pushvalue(L, -2);
                lua_settable(L, -8);                    // env[key] = number
            } else if (lua_istable(L, -1)) {
                int n = (int)lua_objlen(L, -1);
                for (int i = 1; i <= n; i++) {
                    lua_rawgeti(L, -1, i);
                    lua_getfield(L, -1, "definition");
                    if (lua_isstring(L, -1)) {
                        FileInEditorPalette(L, lua_gettop(L) - 3, lua_gettop(L) - 1, i);
                        lua_pushvalue(L, -1);
                        lua_pushvalue(L, -3);
                        lua_settable(L, -7);            // blocks[definition] = entry
                    }
                    lua_pop(L, 2);
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 4);
    }
    // the exe adds the ground itself: an immovable box
    PushEnv(L);
    lua_getfield(L, -1, "blockTable");
    lua_getfield(L, -1, "blocks");
    lua_newtable(L);
    lua_pushstring(L, "Ground"); lua_setfield(L, -2, "definition");
    lua_pushstring(L, "box"); lua_setfield(L, -2, "type");
    lua_pushstring(L, ""); lua_setfield(L, -2, "sprite");
    lua_pushstring(L, ""); lua_setfield(L, -2, "texture");
    lua_pushstring(L, "immovable"); lua_setfield(L, -2, "material");
    lua_setfield(L, -2, "Ground");
    lua_pop(L, 3);
    // materials, damage factors, themes and groups each fill a table of their own, which the scripts read
    // as blockTable.materials, blockTable.damageFactors, blockTable.themes and blockTable.groups
    struct Nested { const char* name; const char* file; };
    const Nested nested[] = {{"materials", "/materials.lua"}, {"damageFactors", "/damagefactors.lua"},
                             {"themes", "/themes.lua"}, {"groups", "/groups.lua"}};
    for (const Nested& n : nested) {
        ok = load(n.name, n.file, false) && ok;
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "blockTable");
        lua_getfield(L, -2, n.name);
        if (lua_istable(L, -2)) lua_setfield(L, -2, n.name); else lua_pop(L, 1);
        lua_pop(L, 2);
    }
    ok = load("particleTable", "/particles.lua", false) && ok;
    return ok;
}

// v2.2.3: objects.world[name].z_order = z, when the scripts still know the object
void SyncWorldZ(lua_State* L, const std::string& name, float z) {
    if (!PushObjectsTable(L, "world")) return;
    lua_getfield(L, -1, name.c_str());
    if (lua_istable(L, -1)) { lua_pushnumber(L, z); lua_setfield(L, -2, "z_order"); }
    lua_pop(L, 2);
}


// engine.on(event, fn) handlers (see engine_table in script_host.cpp). `engine.hookCount` is the number of handlers registered, so an unused hook is one lookup.
void EmitHook(lua_State* L, const char* event, const char* s1, const char* s2, int numbers, double a, double b) {
    const int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "engine");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "hookCount");
        const bool any = lua_tonumber(L, -1) > 0;
        lua_pop(L, 1);
        if (any) {
            lua_getfield(L, -1, "emit");
            if (lua_isfunction(L, -1)) {
                int n = 1;
                lua_pushstring(L, event);
                if (s1) { lua_pushstring(L, s1); n++; }
                if (s2) { lua_pushstring(L, s2); n++; }
                if (numbers > 0) { lua_pushnumber(L, a); n++; }
                if (numbers > 1) { lua_pushnumber(L, b); n++; }
                if (lua_pcall(L, n, 0, 0) != 0) { std::fprintf(stderr, "engine hook %s: %s\n", event, lua_tostring(L, -1)); lua_pop(L, 1); }
            }
        }
    }
    lua_settop(L, top);
}

}  // namespace ab
