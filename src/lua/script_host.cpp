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
extern const unsigned char kJointPageLua[];
extern const unsigned int kJointPageLuaSize;
extern const unsigned char kFriendsServerLua[];
extern const unsigned int kFriendsServerLuaSize;
extern const unsigned char kFriends2NativesLua[];
extern const unsigned int kFriends2NativesLuaSize;
extern const unsigned char kStellaNativesLua[];
extern const unsigned char kAchievementsPageLua[];
extern const unsigned int kAchievementsPageLuaSize;
extern const unsigned char kCoinsLanguageLua[];
extern const unsigned int kCoinsLanguageLuaSize;
extern const unsigned int kStellaNativesLuaSize;
extern const unsigned char kMotionLua[];
extern const unsigned int kMotionLuaSize;
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
    // v3.1: a 1.6.x game (1.6.0 joints) without a joint window of its own (1.6.2) gets Seasons' window and the editor that opens it
    bool jointPage = false, forced = false;
    if (luaL_loadstring(L, "local e = ...; local v = _G.tonumber(_G.string.match(_G.tostring(e.gameVersionNumber or ''), '^(%d+%.%d+)')); "
                           "return e.EditorJointPage == nil and e.Page ~= nil and e.TextItem ~= nil and v ~= nil and v >= 1.6") == 0) {
        lua_pushvalue(L, -2);
        if (lua_pcall(L, 1, 1, 0) == 0) jointPage = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    } else lua_pop(L, 1);
    if (jointPage && luaL_loadbuffer(L, (const char*)kJointPageLua, kJointPageLuaSize, "joint_page.lua") == 0) {
        lua_pushvalue(L, -2);
        lua_setfenv(L, -2);
        if (lua_pcall(L, 0, 0, 0) != 0) { std::fprintf(stderr, "editor: joint window: %s\n", lua_tostring(L, -1)); lua_pop(L, 1); jointPage = false; }
        else { lua_getfield(L, -1, "EditorJointPage"); jointPage = !lua_isnil(L, -1); lua_pop(L, 1); }
        if (jointPage) { stub = true; forced = true; std::fprintf(stderr, "editor: this game has no joint window, using the one of Seasons 2.3.0 with the 1.6.2 editor\n"); }
    } else if (jointPage) lua_pop(L, 1);
    if (stub && luaL_loadbuffer(L, (const char*)kEditorClassicLua, kEditorClassicLuaSize, "editor_classic.lua") == 0) {
        lua_pushvalue(L, -2);                // the environment
        lua_setfenv(L, -2);
        if (lua_pcall(L, 0, 0, 0) != 0) { std::fprintf(stderr, "editor: %s\n", lua_tostring(L, -1)); lua_pop(L, 1); }
        else if (!forced) std::fprintf(stderr, "editor: this game's updateEditor is empty, using the Angry Birds 1.6.2 editor\n");
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

void InstallX87Trig(lua_State* L);   // lua_api.cpp

ScriptHost::ScriptHost(Platform& platform, const BootConfig& config)
    : platform_(platform), config_(config), L_(luaL_newstate()) {
    luaL_openlibs(L_);
    InstallX87Trig(L_);   // math.sin / cos / tan as the retail PC exes' x87 instructions give them

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
    if (!config_.commonScriptPath.empty()) SetString("commonScriptPath", config_.commonScriptPath);
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
    mobileData_ = config_.deviceModel == "iphone" || config_.deviceModel == "ipad" || config_.deviceModel == "android";
    // v3.2: the Seasons 4.x PC exe too (lua/Class.lua calls setmetatable by bare name: without it every class failed, initialize() stopped
    // half way and started again each frame, so 4.1.0 loaded its sheets over and over and never left the loading screen)
    if (mobileData_ || config_.seasons4)     // v3.1: on the phone the game's globals are Lua's own: its scripts call assert, pcall, math ... by bare name (Seasons 4.1.0's lua/Math.lua)
        RunString("local env = _G.getfenv(1)\n"
                  "for _, n in _G.ipairs({\"assert\", \"error\", \"pcall\", \"xpcall\", \"type\", \"tostring\", \"tonumber\", \"pairs\", \"ipairs\", \"select\", \"unpack\", \"next\", \"rawget\", \"rawset\", \"rawequal\", \"setmetatable\", \"getmetatable\", \"math\", \"string\", \"table\", \"os\", \"coroutine\", \"loadstring\", \"setfenv\", \"getfenv\", \"collectgarbage\"}) do\n"
                  "    if _G.rawget(env, n) == nil then env[n] = _G[n] end\n"
                  "end\n"
                  "if _G.rawget(_G, \"gamelua\") == nil then _G.gamelua = env end\n");   // and they read _G.gamelua (Seasons 4.1.0's options menu)
    // Seasons 4.x (settingsWrapper.lua builds "installationId_" .. uniqueInstallationId) and the later scripts read these globals from the exe:
    // the id of this install, and the `native` table of the platform services (apprater, ...). A service the PC exe does not have is simply a missing field.
    SetString("uniqueInstallationId", "angrybirdsengine");
    SetString("uniqueDeviceId", "angrybirdsengine");
    lua_pushstring(L_, "angrybirdsengine"); lua_setfield(L_, LUA_GLOBALSINDEX, "uniqueDeviceId");
    lua_pushstring(L_, "angrybirdsengine"); lua_setfield(L_, LUA_GLOBALSINDEX, "uniqueInstallationId");   // also as _G.uniqueInstallationId
    // The `engine` table every script can use (see docs/SCRIPTING.md, "Changing the engine from Lua"): the pristine natives, a list of their names and
    // events the engine raises (update, levelLoaded, objectCreated, objectRemoved). It is made before any game script runs.
    RunString(
        "local G = _G\n"
        "local env = G.getfenv(1)\n"
        "local original, resOriginal, handlers = {}, {}, {}\n"
        "for k, v in G.pairs(env) do if G.type(v) == 'function' then original[k] = v end end\n"
        "if G.type(env.res) == 'table' then for k, v in G.pairs(env.res) do if G.type(v) == 'function' then resOriginal[k] = v end end end\n"
        "local engine = {version = '3.1', original = original, resOriginal = resOriginal, hookCount = 0}\n"
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
    {   // v3.3: the motion library (tweens, teleport, any movement, the rewind history as a movement), src/lua/motion.lua
        lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
        if (luaL_loadbuffer(L_, (const char*)kMotionLua, kMotionLuaSize, "motion.lua") == 0) {
            lua_pushvalue(L_, -2);
            if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "motion: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        } else { std::fprintf(stderr, "motion: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        lua_pop(L_, 1);
    }
    GetEnvField("native");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); NewGlobalTable("native"); GetEnvField("native"); }
    // Seasons 4.x reads it as `_G.native.xxx` (createStartUpAssets), `_G` being the real global table, so the same table is put there too
    lua_setfield(L_, LUA_GLOBALSINDEX, "native");
    GetEnvField("tuning");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); NewGlobalTable("tuning"); } else lua_pop(L_, 1);
    GetEnvField("nuked");        // 2.0.0 (Android): the save files the exe found corrupted and replaced (none here)
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); NewGlobalTable("nuked"); } else lua_pop(L_, 1);
    RunString(     // the analytics of Seasons 2.x (Medio): every call does nothing and answers nil (the Android games without scripts_common need it too)
        "local env = _G.getfenv(1)\n"
        "if env.MedioEvents == nil then env.MedioEvents = _G.setmetatable({}, {__index = function() return function() end end}) end\n");
    GetEnvField("MedioEvents");
    lua_setfield(L_, LUA_GLOBALSINDEX, "MedioEvents");
    if (true) {              // (v3.1: for every game: Rio HD 2.0.0 asks for RovioChannel too) the services of the 3.x games (ads, the Rovio channel): not available here, the scripts ask and get "no"
        RunString(
            "local env = _G.getfenv(1)\n"
            "if env.getFirstOf == nil then env.getFirstOf = function(n, t, def) for i = 1, n do if t[i] ~= nil then return t[i] end end return def end end\n"
            "if env.getGameTimer == nil then env.getGameTimer = function() return _G.os.clock() end end\n"
            "if env.getSpeed == nil then env.getSpeed = function(name) return (env.getVelocity(name)) end end\n"
            "if env.drawLine == nil then env.drawLine = function(r, g, b, a, x1, y1, x2, y2, _, w) env.drawLine2D(x1, y1, x2, y2, w or 1, r, g, b, a) end end\n"
            "if env.getTrajectory == nil then\n"
            "    -- the predicted flight of a bird (the Egg Defender cannons aim with it): points {x, y, t} every 1/30 s for 3 s from where the bird is, with its speed and the world's gravity\n"
            "    env.getTrajectory = function(name)\n"
            "        local o = env.objects and env.objects.world and env.objects.world[name]\n"
            "        local pts = {}\n"
            "        if not o then return pts end\n"
            "        local _, vx, vy = env.getVelocity(name)\n"
            "        local _, gy = env.getWorldGravity()\n"
            "        for i = 0, 90 do local t = i / 30; pts[#pts + 1] = {x = o.x + vx * t, y = o.y + vy * t + 0.5 * (gy or 0) * t * t, t = t} end\n"
            "        return pts\n"
            "    end\n"
            "end\n"
            "if env.drawSlingScopeNative == nil then   -- the Sling Scope power-up (Classic 3.x, Friends, Seasons): drawSlingScope passes the launch velocity and the bird's flyVerticalForce; the path is the aiming aid's simulation of that launch\n"
            "    -- its dots are the red PARTICLE_SLINGDOT the games ship for it (Friends, Seasons), else the aiming aid trail sprites\n"
            "    local scope = {on = false, drawn = false}\n"
            "    env.abScope = scope; _G.abScope = scope\n"
            "    env.drawSlingScopeNative = function(vx, vy, fy, _, _, dot)   -- Friends 2.0.0 adds (2, 0.07, \"PARTICLE_SLINGDOT\", ...): the dot sprite is the sixth\n"
            "        local g = _G.getfenv(2)   -- the game's globals (Friends keeps them in its own table)\n"
            "        local n = g.currentBirdName\n"
            "        local o = n and g.objects and g.objects.world and g.objects.world[n]\n"
            "        local s = _G.type(g.slingshot) == \"table\" and g.slingshot or {}\n"
            "        local p = g.rubberBandPos or g.levelStartPosition or s.rubberbandPhysicalPos or s.levelStartPhysicalPos   -- Friends 2.0.0 keeps them in slingshot\n"
            "        if o == nil or p == nil or g.simulateAimingAid == nil or g.enableAimingAid == nil then return end\n"
            "        local aid = g.aimingAid\n"
            "        scope.drawn = true\n"
            "        if not scope.on then\n"
            "            scope.on = true\n"
            "            if not (_G.type(aid) == \"table\" and aid.showing) and g.setAimingAidSprite then for _, sp in _G.ipairs({_G.type(dot) == \"string\" and dot or \"PARTICLE_SLINGDOT\", \"PARTICLE_SLINGDOT\", \"SLINGSCOPE_DOT\", \"TRAIL_AIM_1\", \"TRAIL_WHITE_1\", \"TRAIL_FLOWER_2\"}) do if not g.spriteExists or g.spriteExists(sp) then g.setAimingAidSprite(sp, sp == \"TRAIL_WHITE_1\" and 1.6 or 1) break end end end\n"
            "            g.enableAimingAid(true)\n"
            "        end\n"
            "        local m = o.mass or 1\n"
            "        local sx, sy = p.x, p.y\n"
            "        -- the bird is pushed back by the slingshot's pole base, so it leaves from where it is, not from where the rubber band was pulled: the retail scope starts from the pulled spot and is off there; tuning[\"scope.accurate\"] = 0 gives that back\n"
            "        if (env.tuning == nil or env.tuning[\"scope.accurate\"] ~= 0) and _G.type(o.x) == \"number\" and _G.type(o.y) == \"number\" then sx, sy = o.x, o.y end\n"
            "        g.simulateAimingAid(n, vx * m, vy * m, 150, sx, sy, 0, fy or 0)\n"
            "    end\n"
            "end\n"
            "for _, n in _G.ipairs({\"setAccelerometerActive\", \"enableMotion\", \"showSystemPopup\", \"setThemeForegroundOffsetY\", \"setThemeRectColour\", \"native_reloadIngameSprites\", \"setUpdating\", \"sendNotification\", \"printError\", \"drawSlingScopeNative\", \"enablePigDaysVignette\", \"disablePigDaysVignette\"}) do if env[n] == nil then env[n] = function() end end end\n"
            // Seasons 4.x: an explosion (or a bubble) that pushes an object breaks the object's breakable joints whose breakForce the push reaches
            "if env.destroyBreakableJoints == nil then\n"
            "    env.destroyBreakableJoints = function(name, force)\n"
            "        local joints = _G.type(env.objects) == \"table\" and env.objects.joints\n"
            "        if _G.type(joints) ~= \"table\" then return end\n"
            "        for jn, j in _G.pairs(joints) do\n"
            "            if _G.type(j) == \"table\" and j.breakable and not j.abBroken and (j.end1 == name or j.end2 == name) and (force or 0) >= (j.breakForce or 0) then\n"
            "                j.abBroken = true\n"
            "                if env.destroyJointDeferred then env.destroyJointDeferred(j.name or jn) else env.destroyJoint(j.name or jn) end\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "end\n"
            "if env.requestCurrentTimeOnServer == nil then   -- Hatchery Island: the server clock is this computer's clock\n"
            "    env.requestCurrentTimeOnServer = function() if env.currentTimeOnServerReceived and env.getCurrentTime then local t = env.getCurrentTime() env.currentTimeOnServerReceived(t, t) end end\n"
            "end\n"
            "if _G.destroyBreakableJoints == nil then _G.destroyBreakableJoints = env.destroyBreakableJoints end   -- the bird and bubble scripts run in their own tables that see the real globals\n"
            "if env.drawRubberband == nil then\n"
            "    -- the touch-device slingshot band: the scripts pass screen pixels, drawLine2D wants world coordinates (the render state is the game's)\n"
            "    env.drawRubberband = function(x1, y1, x2, y2, w, sprite)\n"
            "        local s = env.tempWorldScale or env.worldScale or 1\n"
            "        local sc, cx, cy = env.screen, env.cameraShakeX or 0, env.cameraShakeY or 0\n"
            "        env.drawLine2D(x1 / s + sc.left + cx, y1 / s + sc.top + cy, x2 / s + sc.left + cx, y2 / s + sc.top + cy, w / s, env.g_rubber_r or 48, env.g_rubber_g or 22, env.g_rubber_b or 8, 255)\n"
            "    end\n"
            "end\n"
            "if env.__abForceObjs == nil then\n"
            "    env.__abForceObjs = {}\n"
            "    local rawSet = env.setObjectParameter\n"
            "    env.setObjectParameter = function(name, id, v)\n"
            "        if id == 8 then env.__abForceObjs[name] = (v == 1) or nil return end\n"
            "        return rawSet(name, id, v)\n"
            "    end\n"
            "    env.__abPerStepForces = function()\n"
            "        local dt = 1 / 30\n"
            "        local world = env.objects and env.objects.world\n"
            "        if not world then return end\n"
            "        local gx, gy = env.getWorldGravity()\n"
            "        for name in _G.pairs(env.__abForceObjs) do\n"
            "            local o = world[name]\n"
            "            if not o then env.__abForceObjs[name] = nil\n"
            "            elseif not o.disableForce then\n"
            "                local vx, vy = env.getVelocity(name)\n"
            "                local ang = env.getAngle(name) or 0\n"
            "                local c, s = _G.math.cos(ang), _G.math.sin(ang)\n"
            "                local fx, fy = o.forceX or 0, o.forceY or 0\n"
            "                local fr = o.forceRelative\n"
            "                if fr and fr ~= 0 then\n"
            "                    local tv = o.targetVelocity\n"
            "                    local along = vx * c + vy * s\n"
            "                    if not tv or tv == 0 or (fr > 0 and along < tv) or (fr < 0 and along > -tv) then fx, fy = fx + fr * c, fy + fr * s end\n"
            "                end\n"
            "                if fx ~= 0 or fy ~= 0 then env.applyForceNative(name, fx, fy) end\n"
            "                local k = o.airResistance or 0\n"
            "                local kx, ky = k, k\n"
            "                local fxr, fyr = o.frictionXRelative or 0, o.frictionYRelative or 0\n"
            "                local fl = o.floatability or 0\n"
            "                if k ~= 0 or fxr ~= 0 or fyr ~= 0 or fl ~= 0 then\n"
            "                    local ax = vx * c + vy * s\n"
            "                    local ay = -vx * s + vy * c\n"
            "                    ax = ax * _G.math.max(0, 1 - (k + fxr) * dt)\n"
            "                    ay = ay * _G.math.max(0, 1 - (k + fyr) * dt)\n"
            "                    local nx, ny = ax * c - ay * s, ax * s + ay * c\n"
            "                    nx, ny = nx - gx * fl * dt, ny - gy * fl * dt\n"
            "                    env.setVelocity(name, nx, ny)\n"
            "                end\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "end\n"
            "local none = function() end\n"
            "local no = function() return false end\n"
            "local zero = function() return 0 end\n"
            "local names = {RovioAds = {trackConversion = none, startSession = none, addPlacementNative = none, addPlacement = none, addPlacementWithGeometry = none, show = no, hide = none, click = none, isAvailable = no},\n"
            "               RovioChannel = {onMenuInitialised = none, openChannelView = none, isAvailable = no, numOfNewContent = zero, updateNewContent = none}}\n"
            "for name, t in _G.pairs(names) do   -- any other question (isAdManagerReady, Seasons 4.1.0) answers no, any other call does nothing\n"
            "    _G.setmetatable(t, {__index = function(_, k) if _G.type(k) == \"string\" and (_G.string.find(k, \"^is\") or _G.string.find(k, \"^has\")) then return no end return none end})\n"
            "    if env[name] == nil then env[name] = t end\n"
            "end\n"
            "-- Classic 3.x pause menu: the PauseMenuPromo placement (the space right of the pause bar) shows a Rovio News card with the postman pig\n"
            "local promo = {}\n"
            "if env.RovioAds == names.RovioAds then\n"
            "    local ads = names.RovioAds\n"
            "    ads.addPlacementWithGeometry = function(name, x, y, w, h) promo[name] = {x = x, y = y, w = w, h = h, sw = env.screenWidth, sh = env.screenHeight} end   -- kept as a part of the screen it was given on (a resized window moves it with the page)\n"
            "    ads.show = function(name) local p = promo[name] if not p then return false end if not p.shown then p.since = _G.os.clock() end p.shown = true return true end\n"
            "    ads.hide = function(name) if promo[name] then promo[name].shown = false end end\n"
            "    ads.isAvailable = function(name) return promo[name] ~= nil end\n"
            "end\n"
            "-- 3.6: the physics overlay of Angry Birds 1.6.2 (key 8: sleeping objects in red, each block's collision shape, a block that has taken damage in blue) for the games whose\n"
            "-- scripts have none (1.0.0 and others; the 1.6.2 script draws its own and has drawDummyCollisionBox). Same drawing calls, same colours.\n"
            "env.abWireframe = false\n"
            "env.abDrawWireframe = function()\n"
            "    local math, pairs, table = _G.math, _G.pairs, _G.table\n"
            "    if _G.type(env.drawDummyCollisionBox) == 'function' or _G.os.getenv('AB_NODEBUGKEYS') then return end\n"
            "    if _G.type(env.objects) ~= 'table' or _G.type(env.objects.world) ~= 'table' or _G.type(env.blockTable) ~= 'table' or _G.type(env.physicsToWorldTransform) ~= 'function' then return end\n"
            "    if env.keyPressed and env.keyPressed['8'] then env.abWireframe = not env.abWireframe end\n"
            "    local ws = env.tempWorldScale or env.worldScale\n"
            "    if not env.abWireframe or _G.type(ws) ~= 'number' or _G.type(env.screen) ~= 'table' or (env.currentGameMode ~= env.updateGame and env.currentGameMode ~= env.updateEditor) then return end\n"
            "    local toWorld, set, line, rect = env.physicsToWorldTransform, env.setRenderState, env.drawLine, env.drawRect\n"
            "    local function polygon(points, lineW, r, g, b, a, angle, pivotX, pivotY)\n"
            "        local n = #points\n"
            "        points[n + 1] = points[1]\n"
            "        local minX, maxX, minY, maxY = 99999999, -99999999, 99999999, -99999999\n"
            "        for i = 1, n do\n"
            "            minX, maxX = math.min(minX, points[i][1]), math.max(maxX, points[i][1])\n"
            "            minY, maxY = math.min(minY, points[i][2]), math.max(maxY, points[i][2])\n"
            "        end\n"
            "        local px, py = (pivotX or 0.5) * (maxX - minX), (pivotY or 0.5) * (maxY - minY)\n"
            "        for i = 2, n + 1 do\n"
            "            local x1, y1, x2, y2 = points[i - 1][1], points[i - 1][2], points[i][1], points[i][2]\n"
            "            set(-env.screen.left, -env.screen.top, ws, ws, angle, (minX + px) - x1, (minY + py) - y1)\n"
            "            line(r, g, b, a, x1, y1, x2, y2, true, lineW)\n"
            "        end\n"
            "    end\n"
            "    local function box(x1, y1, x2, y2, angle, pivotX, pivotY)\n"
            "        polygon({{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}}, 3, 0, 255, 255, 190, angle, pivotX, pivotY)\n"
            "    end\n"
            "    for _, v in pairs(env.objects.world) do\n"
            "        local def = v.definition and env.blockTable.blocks and env.blockTable.blocks[v.definition]\n"
            "        local sprite = v.sprite\n"
            "        if _G.type(sprite) == 'string' and sprite ~= '' then\n"
            "            local w, h = env.res.getSpriteBounds('', sprite)\n"
            "            w, h = w * 0.5, h * 0.5\n"
            "            local x, y = toWorld(v.x, v.y)\n"
            "            local x1, y1, x2, y2 = x - w, y - h, x + w, y + h\n"
            "            if v.sleeping == true and (v.density == 0 or v.mass == 0) then\n"
            "                set(-env.screen.left, -env.screen.top, ws, ws, v.angle, v.spritePivotX, v.spritePivotY)\n"
            "                rect(1, 0, 0, 0.5, x1, y1, x2, y2, true)\n"
            "            end\n"
            "            if def then\n"
            "                if def.type == 'circle' then\n"
            "                    local radius = def.radius or math.max(env.worldToPhysicsTransform(w, 0), env.worldToPhysicsTransform(h, 0))\n"
            "                    local points = {}\n"
            "                    for i = 0, math.pi * 2, 0.5 do points[#points + 1] = {x + math.cos(i) * toWorld(radius, 0), y + math.sin(i) * toWorld(radius, 0)} end\n"
            "                    polygon(points, 4, 255, 255, 255, 255, v.angle, 0.5, 0.5)\n"
            "                elseif def.type == 'box' and def.width ~= nil then\n"
            "                    local ww, hh = toWorld(def.width, def.height)\n"
            "                    box(x - ww / 2, y - hh / 2, x + ww / 2, y + hh / 2, v.angle, (v.spritePivotX or w) / (w * 2), (v.spritePivotY or h) / (h * 2))\n"
            "                elseif def.type == 'box' then\n"
            "                    box(x1, y1, x2, y2, v.angle, 0.5, 0.5)\n"
            "                elseif def.type == 'polygon' and def.vertices then\n"
            "                    local points = {}\n"
            "                    for _, p in pairs(def.vertices) do points[#points + 1] = {x + w * 2 * p.x - w, y + h * 2 * p.y - h} end\n"
            "                    polygon(points, 3, 255, 255, 255, 255, v.angle, 0.5, 0.5)\n"
            "                end\n"
            "                if v.strength ~= nil and v.strength ~= def.strength then\n"
            "                    local px, py = v.spritePivotX or w, v.spritePivotY or h\n"
            "                    set(-env.screen.left, -env.screen.top, ws, ws, v.angle, px, py)\n"
            "                    line(0, 0, 255, 155, x1, y1, x2, y1, true, 4)\n"
            "                    line(0, 0, 255, 155, x1, y1, x1, y2, true, 4)\n"
            "                    set(-env.screen.left, -env.screen.top, ws, ws, v.angle, px, -py)\n"
            "                    line(0, 0, 255, 155, x1, y2, x2, y2, true, 4)\n"
            "                    set(-env.screen.left, -env.screen.top, ws, ws, v.angle, -px, py)\n"
            "                    line(0, 0, 255, 155, x2, y1, x2, y2, true, 4)\n"
            "                end\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "    set(0, 0, 1, 1, 0)\n"
            "end\n"
            "env.abDrawAds = function()\n"
            "    if env.abDrawWireframe then env.abDrawWireframe() end\n"
            "    if env.abDrawAchievements then env.abDrawAchievements() end   -- the achievement page (F7, or the game's Game Center button)\n"
            "    if env.abDrawBanner then env.abDrawBanner() end   -- the Game Center banner of the iOS games (achievements)\n"
            "    local p = promo.PauseMenuPromo\n"
            "    if not (p and p.shown) then return end\n"
            "    local res = _G.res\n"
            "    local x1, y1, x2, y2 = res.getCompoSpriteBounds('MAIL_PIG_HAPPY_FRAME_1')\n"
            "    if x1 == x2 and not p.loaded and env.loadAssets then p.loaded = true _G.pcall(env.loadAssets, {'ROVIO_NEWS'}) x1, y1, x2, y2 = res.getCompoSpriteBounds('MAIL_PIG_HAPPY_FRAME_1') end\n"
            "    local t = _G.os.clock() - p.since\n"
            "    local a = _G.math.min(t * 4, 1)\n"
            "    local kx, ky = (env.screenWidth or p.sw) / (p.sw or 1), (env.screenHeight or p.sh) / (p.sh or 1)\n"
            "    local pw, ph = p.w * kx, p.h * ky\n"
            "    local w = _G.math.min(pw * 0.7, ph * 0.75 * 1.4)\n"   // the card keeps its shape (7:5) and fills the placement
            "    local h = w / 1.4\n"
            "    local cx, cy = (p.x + p.w / 2) * kx, (p.y + p.h / 2) * ky\n"
            "    local fs = h / 400\n"                            // the text is the size it was at 400 pixels high
            "    local left, top = cx - w / 2, cy - h / 2\n"
            "    env.drawRect(1, 1, 1, 0.9 * a, left - 4, top - 4, left + w + 4, top + h + 4, false)\n"
            "    env.drawRect(0.09, 0.35, 0.62, a, left, top, left + w, top + h, false)\n"
            "    env.drawRect(0.05, 0.22, 0.45, a, left, top, left + w, top + h * 0.2, false)\n"
            "    local menuFont = env.getFontMenu and env.getFontMenu() or 'FONT_MENU'\n"
            "    local basicFont = env.getFontBasic and env.getFontBasic() or 'FONT_BASIC'\n"
            "    env.setRenderState(0, 0, 1, 1, 0, 0, 0, a)\n"
            "    res.useFont(menuFont)\n"
            "    env.setRenderState(0, 0, fs, fs, 0, 0, 0, a)\n"
            "    res.drawString('', 'ROVIO NEWS', cx / fs, (top + h * 0.1) / fs, 'VCENTER', 'HCENTER')\n"
            "    if x2 > x1 then\n"
            "        local s = h * 0.5 / (y2 - y1)\n"
            "        local frame = (_G.math.floor(t * 1.5) % 4 == 3) and 'MAIL_PIG_HAPPY_FRAME_2' or 'MAIL_PIG_HAPPY_FRAME_1'\n"
            "        env.setRenderState(cx / s - (x1 + x2) / 2, (top + h * 0.56) / s - (y1 + y2) / 2, s, s, 0, 0, 0, a)   -- the translation is scaled too\n"
            "        res.drawCompoSprite(frame, 0, 0)\n"
            "        env.setRenderState(0, 0, 1, 1, 0, 0, 0, a)\n"
            "    end\n"
            "    res.useFont(basicFont)\n"
            "    env.setRenderState(0, 0, fs, fs, 0, 0, 0, a)\n"
            "    res.drawString('', 'No new messages', cx / fs, (top + h * 0.9) / fs, 'VCENTER', 'HCENTER')\n"
            "    env.setRenderState()\n"
            "end\n"
            "-- the analytics of Seasons 2.x (Medio): every call does nothing and answers nil\n"
            "if env.MedioEvents == nil then env.MedioEvents = _G.setmetatable({}, {__index = function() return none end}) end\n");
        for (const char* name : {"RovioAds", "RovioChannel", "MedioEvents"}) {     // the scripts read them as _G.RovioAds, _G being the real global table
            GetEnvField(name);
            lua_setfield(L_, LUA_GLOBALSINDEX, name);
        }
    }
    if (!IsSpaceDataFolder()) {          // (Space asserts that the engine leaves it undefined)
    lua_pushboolean(L_, 0);              // the scripts read isHDVersion as a plain variable (Android 2.0.0 picks its 1280x800 images when it is true)
    SetEnvField("isHDVersion");
    }
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

ScriptHost::~ScriptHost() { SaveStatesShutdown(L_); lua_close(L_); }

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
    if (!config_.commonScriptPath.empty()) return !config_.seasons4;
    std::vector<uint8_t> bytes;            // a Space build that keeps everything in one scripts folder still has blocks_planets.lua
    return ab::ReadLuaFile(config_.scriptPath + "/blocks_planets.lua", bytes) && !bytes.empty();
}

void ScriptHost::LoadPersisted(const char* tableName, const char* fileName, bool onlyIfExists) {
    {   // v3.1: Star Wars on the iPhone counts with the newer games (scripts_common) but writes its saves with the Star Wars key, as the PC game does: they are read with it too
        std::vector<uint8_t> sw;
        ab::SetSaveFamilySpace(IsSpaceDataFolder() || (ab::ReadLuaFile(config_.scriptPath + "/blocks_tatooine.lua", sw) && !sw.empty()));
    }
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
        {   // a NaN or infinity an older build wrote as text (-nan(ind), nan, inf, 1.#INF): read as nil / 1e999, so the rest of the save is kept
            static const char* const bad[][2] = {{"-nan(ind)", "nil"}, {"nan(ind)", "nil"}, {"-nan(snan)", "nil"}, {"nan(snan)", "nil"}, {"-1.#IND", "nil"}, {"1.#IND", "nil"},
                                                 {"-1.#QNAN", "nil"}, {"1.#QNAN", "nil"}, {"-1.#INF", "-1e999"}, {"1.#INF", "1e999"}, {"-nan", "nil"}, {"=nan", "=nil"}, {"-inf", "-1e999"}, {"=inf", "=1e999"}};
            for (const auto& b : bad) {
                size_t at = 0, n = std::strlen(b[0]);
                while ((at = text.find(b[0], at)) != std::string::npos) {
                    size_t end = at + n;                                    // only a whole value: followed by , } or space
                    if (end < text.size() && text[end] != ',' && text[end] != '}' && text[end] != ' ' && text[end] != '\n' && text[end] != '\r') { at = end; continue; }
                    text.replace(at, n, b[1]); at += std::strlen(b[1]);
                }
            }
        }
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
    // v3.1: the online, store, analytics and game-centre natives of the iOS games (Friends, Star Wars 1.0, Stella ...) answer "nothing there" instead of being undefined, so the start-up code can go on offline
    RunString(
        "local env = _G.getfenv(1)\n"
        "local function none() end\n"
        "local function no() return false end\n"
        "for _, n in _G.ipairs({\"checkInstalledApps\", \"checkInstalledAppsOffline\", \"checkInstalledAppsOnline\", \"determineInstalledApps\", \"getPossibleInstalledAppsProc\", \"checkForURLProc\", \"checkURLCallbacks\", \"startURLThread\", \"logFlurry\", \"logFlurryTimer\", \"prepareFlurry\", \"prepareGameCenter\", \"initGameCenter\", \"removeGameCenter\", \"hideLoadingInitGameCenter\", \"setAchievements\", \"setAchievementMetaData\", \"setAchievementsLoading\", \"achievementRegistered\", \"setLeaderboardCategories\", \"setLeaderboardScores\", \"highScoresPosted\", \"buyItem\", \"initItemPurchase\", \"restoreItems\", \"loadPurchases\", \"getPurchaseHistoryFinished\", \"notificationReceived\", \"removeAllNotifications\", \"removeNotification\", \"addNotificationAfter\", \"openBarnesAndNobleShop\", \"activateGameOverlayToStore\", \"setUseTestAds\", \"setStat\", \"setStatFloat\", \"checkLevelAvailabilityOnline\", \"updateOnlineStatus\", \"getOnlineCheckStatus\", \"checkMainMenuAd\", \"requestBannerAd\", \"hideBannerAd\", \"requestInterstitialAd\", \"requestInterstitial\", \"requestAndShowInterstitialAd\", \"enableiAds\", \"disableiAds\", \"checkForUpdates\", \"callVersionUpdater\", \"unlockRequestChecksum\"}) do\n"
        "    if env[n] == nil then env[n] = none end\n"
        "end\n"
        "for _, n in _G.ipairs({\"logFlurryTimerEvent\", \"native_startURLThread\", \"storePersistentData\", \"changeGFXQuality\", \"startCrystal\", \"setMenuParticlesScale\", \"setSelectedBirdDuringSimulation\", \"ClearSimulationTrajectory\", \"drawSimulationTrajectory\", \"setCameraLimits\", \"setDecorationObjects\", \"renderGravityVisualsNative\", \"importJSONToLuaTable\", \"drawString3D\", \"iapRestoreItems\", \"purchaseMightyEagle\", \"gotoFacebook\", \"gotoTwitter\", \"gotoMightyEagleTrailer\"}) do\n"
        "    if env[n] == nil then env[n] = none end\n"
        "end\n"
        "for _, n in _G.ipairs({\"isOnlineMode\", \"isIapAllowed\", \"isEagleDisabled\", \"isEagleUnavailableForShot\"}) do if env[n] == nil then env[n] = no end end\n"
        "for _, n in _G.ipairs({\"getAimingTime\", \"getMightyEagleNFCUnlockCooldown\"}) do if env[n] == nil then env[n] = function() return 0 end end end\n"
        "local res = _G.res\n"
        "if res and res.createCompositeAudio == nil then   -- Star Wars 1.0 (iPhone): one name for clips played one after the other (the title theme, then the menu music)\n"
        "    local parts, rawPlay, rawStop, rawIs = {}, res.playAudio, res.stopAudio, res.isAudioPlaying\n"
        "    res.createCompositeAudio = function(id, ids) parts[id] = ids end\n"
        "    res.playAudio = function(id, ...) local p = parts[id]; if p then return rawPlay(p[1], ...) end return rawPlay(id, ...) end\n"
        "    if rawStop then res.stopAudio = function(id, ...) local p = parts[id]; if p then for _, n in _G.ipairs(p) do rawStop(n, ...) end return end return rawStop(id, ...) end end\n"
        "    if rawIs then res.isAudioPlaying = function(id, ...) local p = parts[id]; if p then for _, n in _G.ipairs(p) do if rawIs(n) then return true end end return false end return rawIs(id, ...) end end\n"
        "end\n"
        "if res then for _, n in _G.ipairs({\"drawTexturedLine2D\"}) do if env[n] == nil and res[n] then env[n] = res[n] end end end   -- Star Wars 1.0 (iPhone) calls it without res.\n"
        "if env.make_voucher_consume_server == nil then env.make_voucher_consume_server = function() return {tick = none} end end   -- Rio HD 2.0.0: the in-app-purchase voucher server, never anything to consume here\n"
        "if env.force_voucher_consumption == nil then env.force_voucher_consumption = none end\n"
        "for _, n in _G.ipairs({\"activateAndRemoveDestroyJoints\", \"increment_session_count\", \"enableSpotlight\", \"disableSpotlight\"}) do if env[n] == nil then env[n] = none end end\n"
        "if env.getTimeDifferenceInCalendarDays == nil then env.getTimeDifferenceInCalendarDays = function(a, b) if env.getTimeDifferenceInSeconds and a and b then return _G.math.floor(env.getTimeDifferenceInSeconds(a, b) / 86400) end return 0 end end\n"
        "if env.setTransformWithJoint == nil then env.setTransformWithJoint = function(name, other, x, y, a) if env.setPosition then env.setPosition(name, x, y) end if a and env.setRotation then env.setRotation(name, a) end end end   -- Rio HD 2.0.0 (the caged bird moved to the sling)\n"
        "for _, n in _G.ipairs({\"clearGameParticlesNative\", \"clearMenuParticlesNative\"}) do if env[n] == nil then env[n] = env.clearParticles or none end end   -- Rio HD 2.0.0\n"
        "if env.getGameTimer == nil and env.getGameTimerMillis then env.getGameTimer = function() return env.getGameTimerMillis() / 1000 end end\n"
        "if env.getInstalledApps == nil then env.getInstalledApps = function() return {} end end\n"
        "if _G.type(env.native) == \"table\" then   -- v3.1: Friends 2.0.0's framework: files, logging, haptics and the texture budget through `native`\n"
        "    local n = env.native\n"
        "    if _G.rawget(n, \"loadLuaTable\") == nil then n.loadLuaTable = env.abNativeLoadLuaTable end\n"
        "    if _G.rawget(n, \"loadLuaScript\") == nil then n.loadLuaScript = env.abNativeLoadLuaScript end\n"
        "    for _, k in _G.ipairs({\"logInfo\", \"logWarning\", \"logError\", \"logDebug\"}) do if _G.rawget(n, k) == nil then n[k] = function(...) _G.print(...) end end end\n"
        "    if _G.rawget(n, \"haptics\") == nil then n.haptics = _G.setmetatable({}, {__index = function() return none end}) end\n"
        "    if _G.rawget(n, \"ResourceManager\") == nil then n.ResourceManager = _G.setmetatable({requiredTextureMemory = 0, usedTextureMemory = 0}, {__index = function() return none end}) end\n"
        "end\n"
        "if _G.type(env.native) == \"table\" and env.native.GetTimeStamp == nil then env.native.GetTimeStamp = {fetchTimeStamp = none, hasResult = function() return true end, getTimeStamp = function() local t = _G.os.date(\"*t\") t.secondsToNext = 86400 - (t.hour * 3600 + t.min * 60 + t.sec) t.timestamp = _G.os.time() return t end} end   -- Seasons 4.1.0 asks Rovio's server for the time (calendar, Pig Days): the computer's clock answers\n"
        "do local pt = _G.particles if _G.type(pt) == \"table\" then if pt.update == nil then pt.update = none end if pt.SCREEN == nil then pt.SCREEN = 0 end if pt.WORLD == nil then pt.WORLD = 1 end if pt.add == nil and pt.addParticles then pt.add = function(group, name, ...) return pt.addParticles(name, ...) end end end end   -- Seasons 4.1.0's menu runs its particles ahead (the engine steps them every frame anyway)\n"
        "if env.setCameraViewport == nil and env.setTopLeft then env.setCameraViewport = function(l, t, r, b) if r and l and r > l and env.setWorldScale and env.screenWidth then env.setWorldScale(env.screenWidth / (r - l)) end env.setTopLeft(l, t) end end   -- Seasons 4.1.0's main menu camera\n"
        "if env.drawScreenParticles == nil then env.drawScreenParticles = function() if env.drawParticlesNative then env.drawParticlesNative(false) end end end   -- Seasons 4.1.0: the menu's particles (screen space)\n"
        "if env.createBlock == nil and env.createBox then env.createBlock = function(t)   -- Rio HD 2.0.0: a block made from one table, its size from its sprite; a block with `shapes` and `fixtures` (fruit box, awnings) is a body of those polygons (in units of its size, y up)\n"
        "    local b = t.block or {}\n"
        "    local d, f, r = b.density or 1, b.friction or 0.5, b.restitution or 0\n"
        "    if _G.type(b.fixtures) == \"table\" and _G.type(b.shapes) == \"table\" and env.clearShapesArray and env.addPolygonToCompoundList and env.createCompoundObject and env.clearVertices then\n"
        "        env.clearShapesArray()\n"
        "        local first = true\n"
        "        for _, fx in _G.ipairs(b.fixtures) do\n"
        "            local s = b.shapes[fx.shape]\n"
        "            if _G.type(s) == \"table\" and #s >= 3 then\n"
        "                local pts, area = {}, 0\n"
        "                for i, p in _G.ipairs(s) do pts[i] = {p.x * t.w, -p.y * t.h} end\n"
        "                for i = 1, #pts do local a, c = pts[i], pts[i % #pts + 1] area = area + a[1] * c[2] - c[1] * a[2] end\n"
        "                env.clearVertices()\n"
        "                if area >= 0 then for i = 1, #pts do env.addVertex(pts[i][1], pts[i][2]) end else for i = #pts, 1, -1 do env.addVertex(pts[i][1], pts[i][2]) end end\n"
        "                env.addPolygonToCompoundList(t.sprite, 0, 0, 0, t.w, t.h)\n"
        "                local m = b.materials and b.materials[fx.material]\n"
        "                if first and m then d, f, r = m.density or d, m.friction or f, m.restitution or r end\n"
        "                first = false\n"
        "            end\n"
        "        end\n"
        "        if not first then return env.createCompoundObject(t.name, t.x, t.y, nil, d, f, r, t.collision ~= false, t.controllable, t.z_order) end\n"
        "    end\n"
        "    return env.createBox(t.name, t.sprite, t.x, t.y, t.w, t.h, d, f, r, t.collision ~= false, t.controllable, t.z_order)\n"
        "end end\n"
        "if env.getCameraTopLeft == nil and env.getCamera then env.getCameraTopLeft = function() local x, y = env.getCamera() return x, y end end   -- Seasons 4.1.0's main menu\n"
        "if env.isTrialMode == nil then env.isTrialMode = no end\n"
        "if env.enableInGameParticlesNative == nil then env.enableInGameParticlesNative = function(on) env.tuning = env.tuning or {}; env.tuning[\"particles.inGame\"] = on and 1 or 0 end end   -- (4.1.2.1) Rio HD 2.0.0 calls it too, not only Space and Star Wars: a script error here stopped every frame\n"
        "do   -- the newer iOS games call many native_* functions of the phone (ads, store, analytics): any such name that is not there is a function that does nothing\n"
        "    local nativeModule = _G.setmetatable({}, {__call = none, __index = function(_, k) if _G.type(k) == \"string\" and (_G.string.find(k, \"^is\") or _G.string.find(k, \"^has\")) then return no end return none end})   -- called it does nothing, indexed (native_RovioAds.addPlacement, Rio HD 2.0.0) its functions do nothing\n"
        "    local function nativeStub(t, k) if _G.type(k) == \"string\" and _G.string.sub(k, 1, 7) == \"native_\" then return nativeModule end end\n"
        "    if _G.getmetatable(_G) == nil then _G.setmetatable(_G, {__index = nativeStub}) end\n"
        "    local mt = _G.getmetatable(env)\n"
        "    if mt == nil then _G.setmetatable(env, {__index = nativeStub}) elseif mt.__index == nil then mt.__index = nativeStub end\n"
        "    mt = _G.getmetatable(env)\n"
        "    if mt.__newindex == nil then mt.__newindex = function(t, k, v)\n"
        "        -- Friends: selectAssetProfile takes the iPhone picture size to be the screen size (true on a phone, not in a 1280x720 window),\n"
        "        -- so g_assetSizeX/Y, which the menus scale by, become the size of the profile it picked (480x320, 1136x640 ...)\n"
        "        if k == \"selectAssetProfile\" and _G.type(v) == \"function\" then\n"
        "            local pick = v\n"
        "            v = function(...)\n"
        "                local p = pick(...)\n"
        "                local w, h = _G.string.match(_G.tostring(p), \"^(%d+)x(%d+)\")\n"
        "                if w and (env.deviceModel == \"iphone\" or env.deviceModel == \"ipad\") then env.g_assetSizeX, env.g_assetSizeY = _G.tonumber(w), _G.tonumber(h) end\n"
        "                return p\n"
        "            end\n"
        "        end\n"
        "        _G.rawset(t, k, v)\n"
        "    end end\n"
        "end\n"
        "if _G.tutorial == nil then   -- Angry Birds Friends: the tutorial the phone runs. States: 0 start, 1-4 tutorial level N cleared, 5 level 1 played again,\n"
        "    -- 6 tutorial finished (shown once), 7 power-up practice. The state is kept in settings.lua (abTutorialState); 1-4 also follow the saved highscores.\n"
        "    local function cleared() local hs, n = env.highscores, 0 if _G.type(hs) == \"table\" then for i = 1, 4 do local e = _G.rawget(hs, \"LevelTutorial\" .. i) if _G.type(e) == \"table\" and (e.score or 0) > 0 then n = i else break end end end return n end\n"
        "    local function state()\n"
        "        local s = _G.type(env.settings) == \"table\" and env.settings.abTutorialState or 0\n"
        "        if _G.type(env.settings) == \"table\" and env.settings.tutorialFinished == true and s < 5 then s = 7 end   -- a save from before this kept only tutorialFinished\n"
        "        if s < 4 then s = _G.math.max(s, cleared()) end\n"
        "        return s\n"
        "    end\n"
        "    local function setState(s) if _G.type(env.settings) == \"table\" then env.settings.abTutorialState = s end end\n"
        "    local t = {setState = setState, getTutorialStepCount = function() return 4 end, showHelpers = none, disableSlingShot = no}\n"
        "    t.getState = function() local s = state() if s == 6 then setState(7) end return s end   -- 'Tutorial Finished' once, then the power-up practice\n"
        "    t.isTutorialMode = function() return state() < 6 end\n"
        "    t.isPracticeMode = function() return state() >= 6 end\n"
        "    t.levelLoaded = function(name) if name == \"LevelTutorial1\" and state() == 4 then setState(5) end end   -- the menu points at level 1 again after the four\n"
        "    t.startCountDown = function(seconds, done)   -- the end of the tutorial tournament\n"
        "        setState(6)\n"
        "        if env.saveLuaFileWrapper then env.saveLuaFileWrapper(\"settings.lua\", \"settings\", true) end\n"
        "        if _G.type(done) == \"function\" then done() end\n"
        "    end\n"
        "    _G.tutorial = _G.setmetatable(t,\n"
        "        {__index = function(t, k) _G.print(\"tutorial.\" .. _G.tostring(k) .. \" is not there\") return none end})\n"
        "end\n"
        "if _G.facebook == nil then   -- Angry Birds Friends: no Facebook here; the game's own offline session (Practice) is what runs\n"
        "    local fb = {abOffline = true}   -- friends_server.lua builds the rest of the server on this\n"
        "    for _, n in _G.ipairs({\"testNetwork\", \"hasToken\", \"isOnlineMode\", \"isUnknownMode\", \"isFacebookSessionOpen\", \"isTestingMode\", \"hasLastTournamentResult\", \"hasClaimableBundle\"}) do fb[n] = no end\n"
        "    for _, n in _G.ipairs({\"getCreditCount\", \"getNumberOfPendingGifts\", \"getTournamentTime\", \"getTournamentScoreCount\"}) do fb[n] = function() return 0 end end\n"
        "    for _, n in _G.ipairs({\"getTournamentLevels\", \"getPendingGifts\", \"getInvitableFriends\", \"getExcludedUserIds\", \"getShopListing\", \"getCreditShopListing\"}) do fb[n] = function() return {} end end\n"
        "    fb.startOfflineSession = function(cb) if _G.type(cb) == \"function\" then cb() end end\n"
        "    fb.startTestingSession = fb.startOfflineSession\n"
        "    fb.resetOfflineSession = none\n"
        "    fb.resolveIdentity = function(cb) if _G.type(cb) == \"function\" then cb(true) end end   -- the login page then goes on to the main menu\n"
        "    fb.initTournament = fb.resolveIdentity\n"
        "    fb.refreshTournament = fb.resolveIdentity\n"
        "    fb.refreshDailyRewardInfo = none\n"
        "    fb.drawSlingScopeNative = env.drawSlingScopeNative   -- Friends keeps the Sling Scope's native here (menu_flow copies it to gamelua)\n"
        "    fb.getTournamentLevels = function()   -- offline: the levels in the game's own folders (episode 1 = the tournament levels in pack57, 2 = tutorial, 3 = practice)\n"
        "        local ids = env.g_episodeIds\n"
        "        local ep = (_G.type(ids) == \"table\" and ids[1]) or 3\n"
        "        local codes = {}\n"
        "        if ep == 1 then for _, n in _G.ipairs({1, 2, 5, 6, 7, 8, 15, 16, 20, 24, 26, 27, 41, 56, 57, 58, 60, 61, 126, 128, 130, 131, 132, 133, 135, 136, 137, 139, 140, 155}) do codes[#codes + 1] = \"2000-\" .. n end\n"
        "        else for i = 1, 4 do codes[i] = (ep == 2 and \"Tutorial\" or \"Practice\") .. i end end\n"
        "        local list = {}\n"
        "        local hs = env.highscores\n"
        "        for i, c in _G.ipairs(codes) do\n"
        "            if _G.type(hs) == \"table\" and _G.type(_G.rawget(hs, \"Level\" .. c)) ~= \"table\" then _G.rawset(hs, \"Level\" .. c, {score = 0, eagleScore = 0}) end   -- the phone keeps an entry for every level of the list (level end counts the stars of all of them)\n"
        "            local e = _G.type(hs) == \"table\" and _G.rawget(hs, \"Level\" .. c)\n"
        "            list[i] = {code = c, level = i, locked = false, new = false, points = (e and e.score) or 0, rank = 0}   -- the best score here (the stars on the button)\n"
        "        end\n"
        "        return {episode = ep, online = false, levels = list}\n"
        "    end\n"
        "    fb.useDynamicLevelLoading = no\n"
        "    fb.openFacebookSession = function(cb) if _G.type(cb) == \"function\" then cb(false, \"offline\") end end\n"
        "    fb.getStoredUser = function() return nil end\n"
        "    fb.getOwnUserInfo = function() return {id = \"0\", name = \"Player\", first_name = \"Player\"} end\n"
        "    if env.loadLevel then   -- the phone keeps a highscore entry for every level it loads; the hud reads highscores[level].score\n"
        "        local load = env.loadLevel\n"
        "        env.loadLevel = function(path, ...)\n"
        "            local n = _G.string.match(_G.tostring(path), \"([^/]+)$\")\n"
        "            n = n and _G.string.gsub(n, \"%.lua$\", \"\")\n"
        "            local hs = env.highscores\n"
        "            if n and _G.type(hs) == \"table\" and _G.type(_G.rawget(hs, n)) ~= \"table\" then _G.rawset(hs, n, {score = 0, eagleScore = 0}) end\n"
        "            fb.currentLevel = n\n"
        "            if n and _G.rawget(_G.tutorial, \"levelLoaded\") then _G.tutorial.levelLoaded(n) end\n"
        "            return load(path, ...)\n"
        "        end\n"
        "    end\n"
        "    fb.validatePowerup = function(name, cb) if _G.type(cb) == \"function\" then cb(true) end end   -- the server's yes to using a power-up (the hud waits for it, paused)\n"
        "    -- the score lists: the player and the two bots of the offline game (picture ids -1 and -2 are Red's and the Yellow Bird's pictures).\n"
        "    -- Red Bird scores each level's three-star score (goldScore), the Yellow Bird its two-star score (silverScore)\n"
        "    local function scoreList(own, red, yellow)\n"
        "        local list = {{id = \"-1\", name = \"Red Bird\", points = red, isBot = true}, {id = \"-2\", name = \"Yellow Bird\", points = yellow, isBot = true},\n"
        "                      {id = \"0\", name = \"Player\", points = own, isOwnPlayer = true}}\n"
        "        _G.table.sort(list, function(a, b) if a.points ~= b.points then return a.points > b.points end return a.isOwnPlayer == true end)\n"
        "        local ownRank = 1\n"
        "        for i, e in _G.ipairs(list) do e.rank = i e.stars = 0 e.invite = false e.gift = false e.isBot = e.isBot or false e.isOwnPlayer = e.isOwnPlayer or false e.firstName = e.name e.lastName = \"\" if e.isOwnPlayer then ownRank = i end end\n"
        "        return list, ownRank\n"
        "    end\n"
        "    local function best(level) local e = _G.type(env.highscores) == \"table\" and _G.rawget(env.highscores, level) return (_G.type(e) == \"table\" and e.score) or 0 end\n"
        "    local function stars(level) local t = _G.type(env.starTable) == \"table\" and _G.rawget(env.starTable, level) if _G.type(t) ~= \"table\" then return 0, 0 end return t.goldScore or 0, t.silverScore or 0 end\n"
        "    fb.starScores = stars\n"
        "    fb.getLevelScores = function(cb)\n"
        "        if _G.type(cb) ~= \"function\" then return end\n"
        "        local gold, silver = stars(fb.currentLevel)\n"
        "        local list, rank = scoreList(best(fb.currentLevel), gold, silver)\n"
        "        cb(true, fb.currentLevel, list, rank)\n"
        "    end\n"
        "    local function episodeScores()\n"
        "        local own, red, yellow = 0, 0, 0\n"
        "        for _, l in _G.ipairs(fb.getTournamentLevels().levels) do local gold, silver = stars(\"Level\" .. l.code) own = own + best(\"Level\" .. l.code) red = red + gold yellow = yellow + silver end\n"
        "        return scoreList(own, red, yellow)\n"
        "    end\n"
        "    fb.getTournamentScoreCount = function() return 3 end\n"
        "    fb.getTournamentScores = function() return (episodeScores()) end\n"
        "    local zero = function() return 0 end\n"
        "    _G.facebook = _G.setmetatable(fb, {__index = function(t, k)   -- the rest by name: counts and times are 0, is/has questions are no, lists are empty\n"
        "        if _G.type(k) ~= \"string\" then return nil end\n"
        "        if _G.string.find(k, \"^getNumberOf\") or _G.string.find(k, \"Count$\") or _G.string.find(k, \"Time$\") then return zero end\n"
        "        if _G.string.find(k, \"^is\") or _G.string.find(k, \"^has\") then return no end\n"
        "        if _G.string.find(k, \"s$\") and _G.string.find(k, \"^get\") then return function() return {} end end\n"
        "        return none\n"
        "    end})\n"
        "end\n"
        "if _G.WebView == nil then   -- the phone's web view (Rovio News in the pause menu): every page fails to load, so the game shows no news (without one it drew a red test box)\n"
        "    local view = {}\n"
        "    view.setOnLinkClickedCallback = none\n"
        "    view.setOnPageLoadedCallback = function(self, cb) self.loaded = cb end\n"
        "    view.loadPage = function(self) if _G.type(self.loaded) == \"function\" then self.loaded(self, false, \"\") end end\n"
        "    _G.WebView = {new = function() return _G.setmetatable({}, {__index = function(t, k) return view[k] or none end}) end}\n"
        "end\n"
        "if _G.native_RovioAccount == nil then _G.native_RovioAccount = _G.setmetatable({isAccountAvailable = no}, {__index = function() return none end}) end   -- Rio HD 2.0.0: the Rovio account (sign in, profile) is not there\n"
        "if env.getUnlockRequestChecksum == nil then env.getUnlockRequestChecksum = function() return \"\" end end\n"
        "if env.setRevoluteJointSpeed == nil then env.setRevoluteJointSpeed = function(j, speed) if env.setJointParameters and j ~= nil then env.setJointParameters({name = (_G.type(j) == \"table\" and j.name or j), motorSpeed = speed}) end end end\n"
        "if env.getAvailableItemCount == nil then env.getAvailableItemCount = function() return 0 end end\n"
        "if env.getAchievements == nil then env.getAchievements = function() return {} end end\n"
        "if env.getLeaderboardCategories == nil then env.getLeaderboardCategories = function() return {} end end\n");
    // v3.1: Angry Birds Friends' server (login, Weekly Tournament, coins, shop, daily rewards) answered on this computer (friends_server.lua); it does nothing in the other games
    lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
    if (luaL_loadbuffer(L_, (const char*)kFriendsServerLua, kFriendsServerLuaSize, "friends_server.lua") == 0) {
        lua_pushvalue(L_, -2);
        if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "friends server: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
    } else { std::fprintf(stderr, "friends server: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
    lua_pop(L_, 1);
    {   // v3.1: Angry Birds Friends 2.0.0's native services (service, social, user, tournament ...), only for its data (scripts/TutorialMode.lua)
        std::vector<uint8_t> probe;
        if (ab::ReadLuaFile(config_.scriptPath + "/TutorialMode.lua", probe) && !probe.empty()) {
            friends2_ = true;
            lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
            if (luaL_loadbuffer(L_, (const char*)kFriends2NativesLua, kFriends2NativesLuaSize, "friends2_natives.lua") == 0) {
                lua_pushvalue(L_, -2);
                if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "friends 2.0.0: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
            } else { std::fprintf(stderr, "friends 2.0.0: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
            lua_pop(L_, 1);
        }
    }
    {   // v3.1: Angry Birds Stella 1.0.0's native services (ResourceManager, Skynest, the social manager, shaders ...), only for its data (scripts/PolaroidMode.lua)
        std::vector<uint8_t> probe;
        if (ab::ReadLuaFile(config_.scriptPath + "/PolaroidMode.lua", probe) && !probe.empty()) {
            stella_ = true;
            lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
            if (luaL_loadbuffer(L_, (const char*)kStellaNativesLua, kStellaNativesLuaSize, "stella_natives.lua") == 0) {
                lua_pushvalue(L_, -2);
                if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "stella: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
            } else { std::fprintf(stderr, "stella: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
            lua_pop(L_, 1);
        }
    }
    auto installSpaceEditor = [&]() {   // v2.5: Angry Birds Space has no editor scripts, the engine brings one (F2 in a level, space_editor.lua)
        lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
        if (luaL_loadbuffer(L_, (const char*)kSpaceEditorLua, kSpaceEditorLuaSize, "space_editor.lua") == 0) {
            lua_pushvalue(L_, -2);
            if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "space editor: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        } else { std::fprintf(stderr, "space editor: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        lua_pop(L_, 1);
    };
    if (!config_.commonScriptPath.empty() && !config_.seasons4) {
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
    if (config_.seasons4) {      // Seasons 4.x: scripts_common/gamelogic.lua pulls the rest in with requireFile
        std::vector<uint8_t> tatooine;   // v3.1: Star Wars on the iPhone loads like the newer games but plays by the Star Wars rules (contacts, sensors, the lightsaber)
        if (ab::ReadLuaFile(config_.scriptPath + "/blocks_tatooine.lua", tatooine) && !tatooine.empty()) starWars_ = true;
        if (!starWars_) { lua_pushboolean(L_, 1); SetEnvField("abObjectThis"); }   // Seasons 4.x: `this` in a file loaded into an object is that object (lua_api.cpp)
        if (!RunFile(config_.commonScriptPath + "/gamelogic.lua")) return false;
        // v3.1: without the phone's dynamic asset handler the game loads the picture groups of loadgroups.lua (g_loadgroups), an older list:
        // the groups the newer screens need (HUD_POTION's Power Potion icons, SHOP_NEW, ID ...) are only named in scripts/dynamic/default.lua.
        // Those of its common, main menu, game HUD and in-game lists are added to g_loadgroups
        RunString("local env = _G.getfenv(1)\n"
                  "local load = env.loadImages\n"
                  "if _G.type(load) ~= \"function\" or not env.loadTextFileToString then return end\n"
                  "local function extra()\n"
                  "    local code = env.loadTextFileToString(env.scriptPath .. \"/dynamic/default.lua\")\n"
                  "    local fn = code and _G.loadstring(code)\n"
                  "    if not fn then return {} end\n"
                  "    local lists\n"
                  "    _G.setfenv(fn, _G.setmetatable({dynamic = {addreq = function(t) lists = t end}}, {__index = env}))\n"
                  "    if not _G.pcall(fn) or _G.type(lists) ~= \"table\" then return {} end\n"
                  "    local out = {}\n"
                  "    for _, name in _G.ipairs({\"common\", \"mainMenu\", \"gameHud\", \"ingame\"}) do for _, g in _G.ipairs(lists[name] or {}) do out[#out + 1] = g end end\n"
                  "    return out\n"
                  "end\n"
                  "env.loadImages = function(groups, ...)\n"
                  "    if groups ~= nil and groups == env.g_loadgroups and env.useDynamicAssets ~= true and not env.abExtraGroups then\n"
                  "        env.abExtraGroups = true\n"
                  "        local have = {}\n"
                  "        for _, g in _G.ipairs(groups) do have[g] = true end\n"
                  "        for _, g in _G.ipairs(extra()) do if _G.type(g) == \"string\" and not have[g] then have[g] = true; groups[#groups + 1] = g end end\n"
                  "    end\n"
                  "    return load(groups, ...)\n"
                  "end\n");
    } else {
        const char* files[] = {"/loadlist.lua", "/gamelogic.lua"};
        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
            if (i == 0) { std::vector<uint8_t> ll; const std::string& sp = config_.scriptPath; const bool flat = sp.size() < 7 || sp.compare(sp.size() - 7, 7, "scripts") != 0; if (!ab::ReadLuaFile(config_.scriptPath + files[i], ll) || ll.empty()) { if (!flat) continue;   /* v3.1: Rio HD 2.0.0 has no loadlist.lua either (it builds its list in its script) but is not the first game: only a flat data folder is */ first_ = true; lua_pushnumber(L_, 0); SetEnvField("touches"); lua_pushboolean(L_, 1); SetEnvField("abBoxHalfExtents"); continue; } }   // Angry Birds 1.0 has no loadlist.lua: its gamelogic.lua loads its sheets itself
            if (!RunFile(config_.scriptPath + files[i])) return false;
        }
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
    } else if (stella_) {
        // Angry Birds Stella: game_init.lua loads its own definitions (scripts/definitions/blocks_*.lua, materials, themes ...)
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
    std::vector<uint8_t> dcBytes;
    const bool debugConsole = !config_.commonScriptPath.empty() && ReadLuaFile(config_.commonScriptPath + "/menus/DebugConsole.lua", dcBytes) && !dcBytes.empty();
    std::vector<uint8_t> rioHdProbe;      // v3.1: Rio HD 2.0.0 ships parts of its editor that cannot start (menu/EditorParticlesPage.lua reads the editor scene before it is made): its editor stays off
    bool keepEditorOff = ReadLuaFile(config_.scriptPath + "/menu/EditorParticlesPage.lua", rioHdProbe) && !rioHdProbe.empty();
    // v3.1: Angry Birds Space starts on its debug page (Play / Editor / Object Editor on a bare background) when showEditor is set; the
    // retail builds (they ship levels/MainMenu.lua, the real main menu) start normally, only the prototype keeps the debug page. Its
    // editor is the engine's own (F2), which does not need showEditor.
    std::vector<uint8_t> spaceMenuProbe;
    if (IsSpaceDataFolder() && ReadLuaFile(config_.scriptPath + "/../levels/MainMenu.lua", spaceMenuProbe) && !spaceMenuProbe.empty()) keepEditorOff = true;
    if (!std::getenv("AB_NODEBUGKEYS")) RunString((std::string(
        "local env = _G.getfenv(1)\n") +
        // v3.1: Seasons 4.x's achievement processor sets tiny test limits (5 birds, 1 golden egg ...) when cheatsEnabled is on as it is made, so
        // every start unlocked Bird Slinger, Egg Hunter, Smash Maniac ...: there the cheats are on only for the two per-frame functions below
        std::string(config_.seasons4 || mobileData_ ? "" : "env.cheatsEnabled = true\n") +   // v3.2: the phone data too (Angry Birds 2.0.0 for Android: Angry Birds Fan after 60 seconds)
        // the 3.x gamelogic loads menus/DebugConsole.lua when cheatsEnabled is already true at load time and then calls DebugConsole every frame: load it now
        (debugConsole ? std::string("if env.DebugConsole == nil and _G.type(env.loadLuaFileToObject) == \"function\" then env.loadLuaFileToObject(env.commonScriptPath .. \"/menus/DebugConsole.lua\", env.this, \"DebugConsole\") end\n") : std::string()) +
        // the same for Seasons 4.x's debugUtils.lua (its update runs every frame once cheatsEnabled is true)
        std::string("if env.debugUtils == nil and env.commonScriptPath and _G.type(env.loadLuaFile) == \"function\" then _G.pcall(env.loadLuaFile, env.commonScriptPath .. \"/debugUtils.lua\", \"\") if env.debugUtils == nil then env.debugUtils = {update = function() end, draw = function() end} end end\n") +
        // Friends 2.0.0 keeps it as scripts/DebugConsole.lua, loaded with requireFile
        std::string("if env.DebugConsole == nil and env.scriptPath and _G.type(env.requireFile) == \"function\" and env.fileExists and env.fileExists(env.scriptPath .. \"/DebugConsole.lua\") then _G.pcall(env.requireFile, \"DebugConsole.lua\") end\n") +
        // v3.2: the debug console opened on five clicks in the bottom-right corner (DebugConsole.gestureActivation, a phone gesture): on a PC
        // that corner is easy to click by chance, and the console then took every click (the level buttons seemed dead). Shift+D still opens it
        std::string("if _G.type(env.DebugConsole) == \"table\" and _G.type(env.DebugConsole.gestureActivation) == \"function\" then env.DebugConsole.gestureActivation = function() return false end end\n") +
        // v3.2: drawLevelSelectionItems ends by drawing the page's first item again from BUTTONS_SHEET_1, to keep it on top. In HD 1.4.2 that
        // item is the birds in the corner (LS_THEME_n_LEFT, not in that sheet, so the phone drew nothing): here they covered the back button
        std::string("if _G.type(env.drawLevelSelectionItems) == \"function\" then local draw = env.drawLevelSelectionItems\n"
            " env.drawLevelSelectionItems = function(page, ...) local first = _G.type(page) == \"table\" and _G.type(page.items) == \"table\" and page.items[1]\n"
            "  if not (first and first.selectable == false and env.res) then return draw(page, ...) end\n"
            "  local res = env.res local drawSprite = res.drawSprite\n"
            "  res.drawSprite = function(sheet, sprite, ...) if sheet == \"BUTTONS_SHEET_1\" and sprite == first.sprite then return end return drawSprite(sheet, sprite, ...) end\n"
            "  local ok, err = _G.pcall(draw, page, ...) res.drawSprite = drawSprite if not ok then _G.error(err, 0) end end end\n") +
        // Seasons 4.1.0's menus lay out with Scaling (Scaling.lua), which the phone has loaded before the first page is made
        std::string("if env.Scaling == nil and env.commonScriptPath and _G.type(env.requireFile) == \"function\" and env.fileExists and env.fileExists(env.commonScriptPath .. \"/Scaling.lua\") then _G.pcall(env.requireFile, \"Scaling.lua\") end\n") +
        // initSubsystems builds the `Editor` class when showEditor is set; a data folder without the editor (an Android .apk) has no such class
        std::string("local init = env.initSubsystems\n") +
        (keepEditorOff ? std::string() : std::string("if not (env.Editor == nil and _G.type(init) == \"function\" and _G.string.find(_G.string.dump(init), \"Editor\", 1, true)) then env.showEditor = true end\n")) +
        std::string(
        "local view = _G.setmetatable({releaseBuild = false" + std::string(config_.seasons4 || mobileData_ ? ", cheatsEnabled = true" : "") + "}, {__index = env, __newindex = env})\n"
        "for _, name in _G.ipairs({\"update\", \"updateGame\"}) do\n"
        "    if _G.type(env[name]) == \"function\" then _G.setfenv(env[name], view) end\n"
        "end\n"
        "env.__abview = view\n")).c_str());
    // the Space prototype's debug page has a Credits button, but its credits page is commented out (MainMenuScene.lua) and it ships no
    // levels/Credits.lua: ShowCredits indexed the missing page and stopped the menu. With no credits page the button does nothing.
    if (IsSpaceDataFolder()) RunString(
        "local env = _G.getfenv(1)\n"
        "local m = env.menus and env.menus.mainMenuInitialPage and env.menus.mainMenuInitialPage.MainMenuInitialPage\n"
        "if m and _G.type(m.ShowCredits) == \"function\" then\n"
        "    local show = m.ShowCredits\n"
        "    m.ShowCredits = function(self, ...)\n"
        "        local s = env.sm and env.sm.scenes and env.sm.scenes.mainMenu\n"
        "        if not (s and s.pages and s.pages.creditsPage) then return end\n"
        "        return show(self, ...)\n"
        "    end\n"
        "end\n");
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
        if (!strict && (IsSpaceDataFolder() || config_.seasons4 || mobileData_) && (std::strcmp(name, "update") == 0 || std::strcmp(name, "draw") == 0)) {      // v3.1: the iPhone / iPad / Android games too
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
    if (!iapOff_) {      // v3.1: no store here: a game that says in-app purchases are available (Rio HD 2.0.0's options.lua) builds its shop and voucher server, which need the store; it is told there is none
        iapOff_ = true;
        GetEnvField("iap_available");
        bool on = lua_toboolean(L_, -1) != 0;
        lua_pop(L_, 1);
        if (on) { lua_pushboolean(L_, 0); SetEnvField("iap_available"); }
        // Rio HD 2.0.0's localeToPostfix has en_EN, fr_FR ...: a locale it does not list (en_US here) gets the English postfix
        RunString((std::string("local env = _G.getfenv(1)\nlocal on = ") + (on ? "true" : "false") + "\n"
                  "local t, loc = env.localeToPostfix, _G.res and _G.res.getLocale and _G.res.getLocale()\n"
                  "if _G.type(t) == \"table\" and loc and t[loc] == nil then t[loc] = t.en_EN or t.en_US or \"\" end\n"
                  // with the store off, the game's IAP table stays empty; its questions (is_provider_initialized, is_payment_enabled ...) answer no
                  "if on then\n"
                  "    if env.IAP == nil then env.IAP = {} end\n"
                  "    if _G.type(env.IAP) == \"table\" then for _, n in _G.ipairs({\"is_provider_initialized\", \"is_payment_enabled\", \"is_purchase_limit_reached\", \"is_restore_automatic\", \"start_automatic_restore\", \"await_info\", \"await_purchase\", \"await_restore\", \"consumable\"}) do if _G.rawget(env.IAP, n) == nil then env.IAP[n] = function() return false end end end end\n"
                  "end\n"
                  // power-ups (Classic 3.x, Seasons, Rio, Friends ...): the store that sells them is not here, so every game that has them gets them: the bar is on, and a power-up the player has none of left counts as one (using it takes nothing away); bought or won ones count down as usual
                  // Seasons 4.1.0's main menu sets up Game Center when releaseBuild reads false (the debug keys' view) on iOS data; Game Center is not loaded here
                  "do local ui = env.ui local F = _G.type(ui) == \"table\" and _G.rawget(ui, \"Frame\") if _G.type(F) == \"table\" and env.gameCenterSupported ~= true and F.initGameCenter == nil then F.initGameCenter = function() end end end\n"
                  // the phone games turn some pictures by a fixed step each update, made for 60 updates a second (Friends: the shine behind a button +0.03, the gift and light-beam rays +0.025 / +0.02); here a step is scaled by the frame's time, so they turn as fast as on the phone at any frame rate
                  // v3.1: the step is taken 30 times a second (the phone menus' rate; at 60 the shine still turned too fast), env.abPictureRate changes it
                  "if " + std::string(mobileData_ ? "true" : "false") + " then\n"
                  "    if env.abPictureRate == nil then env.abPictureRate = " + std::to_string(std::getenv("AB_PICTURE_RATE") ? std::atoi(std::getenv("AB_PICTURE_RATE")) : 30) + " end\n"
                  "    local done = {}\n"
                  "    local function img(name) return function(self) return self.getChild and self:getChild(name) end end\n"
                  "    local list = {{\"ui\", \"GlowImageButton\", function(self) return self.glowImageFrame or (self.getChild and self:getChild(\"_glowImage\")) end}, {nil, \"GlowImageButton\", img(\"_glowImage\")},\n"
                  "        {\"animations\", \"GiftReceivedBurst\", img(\"rotatingBgImage\")}, {nil, \"GiftReceivedBurst\", img(\"rotatingBgImage\")}, {\"animations\", \"BeamsOfLightWrapper\", function(self) return self.image end}, {nil, \"BeamsOfLightWrapper\", function(self) return self.image end}}\n"
                  "    env.abFrameSteps = function()\n"
                  // Seasons 4.1.0: the episode strip coasts after a swipe and then stays where it stopped, between two episodes; the phone's
                  // ScrollFrame settles on the nearest episode (setTargetToNearestAnchor) and so does this one now, once it has stopped
                  "        local esf = _G.rawget(env, \"EpisodeScrollFrame\")\n"
                  "        if _G.type(esf) == \"table\" and _G.type(_G.rawget(esf, \"updateSelf\")) == \"function\" and not _G.rawget(esf, \"abSnap\") then\n"
                  "            esf.abSnap = true\n"
                  "            local upd = esf.updateSelf\n"
                  "            esf.updateSelf = function(self, ...)\n"
                  "                local r = upd(self, ...)\n"
                  "                if not self.dragging and self.velX == nil and self.target_anchor == nil and _G.type(self.anchors) == \"table\" and _G.type(self.scroll) == \"table\" and self.getNearestAnchor and self.setTargetAnchor then\n"
                  "                    local n = self:getNearestAnchor()\n"
                  "                    local a = n and self.anchors[n]\n"
                  "                    if a and _G.math.abs((a.x or 0) - (self.scroll.x or 0)) > 0.5 then self:setTargetAnchor(n) end\n"
                  "                end\n"
                  "                return r\n"
                  "            end\n"
                  "        end\n"
                  // Rio HD 2.0.0 puts its power-up menu one slot right of the Mighty Eagle button, which exists only when the store does;
                  // without it the menu (and its open / close button) sat left of the screen over the pause button, open for good. It now
                  // takes the eagle's slot, right of the pause button
                  "        if env.g_eagleButtonW == 0 and env.g_eagleButtonPX == 0 and _G.type(env.g_pauseButtonPX) == \"number\" and _G.type(env.g_pauseButtonW) == \"number\" and _G.type(env.isEagleAvailable) == \"function\" and not env.isEagleAvailable() then\n"
                  "            env.g_eagleButtonPX = env.g_pauseButtonPX + env.g_pauseButtonW * 1.1 / 2.4\n"
                  "        end\n"
                  // v3.2: Angry Birds HD 1.4.2's menu balloons (animateBirds, entries with reward 2) sway by x = x - angle * layer / 2 every update,
                  // a step with no time in it, so at hundreds of frames a second they swung far across the screen. That sideways step is now
                  // taken env.abPictureRate (30) times a second, scaled by the frame's time like the Friends shine; the speed-and-time move is left as it is
                  "        local ab = _G.rawget(env, \"animateBirds\")\n"
                  "        if _G.type(ab) == \"function\" and not _G.rawget(env, \"abBalloonsScaled\") then\n"
                  "            env.abBalloonsScaled = true\n"
                  "            env.animateBirds = function(dt, ...)\n"
                  "                local before = {}\n"
                  "                if _G.type(env.birdAnimations) == \"table\" then for _, b in _G.ipairs(env.birdAnimations) do if _G.type(b) == \"table\" and b.reward == 2 and _G.type(b.x) == \"number\" then before[b] = b.x end end end\n"
                  "                local r1, r2 = ab(dt, ...)\n"
                  "                local k = _G.math.min((env.abFrameDt or 1 / 60) * (env.abPictureRate or 30), 6)\n"
                  "                for b, x in _G.pairs(before) do\n"
                  "                    if _G.type(b.x) == \"number\" then\n"
                  "                        local sway = b.x - x - (b.xVel or 0) * (dt or 0)\n"
                  "                        b.x = b.x + sway * (k - 1)\n"
                  "                    end\n"
                  "                end\n"
                  "                return r1, r2\n"
                  "            end\n"
                  "        end\n"
                  "        for i, c in _G.ipairs(list) do\n"
                  "            local t = c[1] and _G.rawget(env, c[1]) or env\n"
                  "            local cls = _G.type(t) == \"table\" and _G.rawget(t, c[2])\n"
                  "            if not done[i] and _G.type(cls) == \"table\" and _G.type(_G.rawget(cls, \"update\")) == \"function\" and not _G.rawget(cls, \"abStepScaled\") then\n"
                  "                done[i] = true; cls.abStepScaled = true\n"
                  "                local update, picture = cls.update, c[3]\n"
                  "                cls.update = function(self, a, dt, ...)\n"
                  "                    local p = picture(self)\n"
                  "                    local before = _G.type(p) == \"table\" and p.angle\n"
                  "                    local r1, r2 = update(self, a, dt, ...)\n"
                  "                    if _G.type(before) == \"number\" and _G.type(p.angle) == \"number\" then p.angle = before + (p.angle - before) * _G.math.min((env.abFrameDt or 1 / 60) * (env.abPictureRate or 30), 6) end   -- by the frame's time (the second argument is not always dt: Friends 2.0.0 passes about 10 there)\n"
                  "                    return r1, r2\n"
                  "                end\n"
                  "            end\n"
                  "        end\n"
                  "    end\n"
                  // the slingshot: the phone scripts judge the finger over the last 22 updates, made for 30-60 updates a second. Space's
                  // updateDraggingCursor freezes the bird when those updates moved it less than a hair and frees it only after a long move;
                  // at hundreds of frames a second 22 updates are a few milliseconds, so the bird stuck and jumped while dragged. It now
                  // looks at the cursor 60 times a second and follows it in between. Star Wars' updateDraggingCursorLagOnly keeps the bird
                  // 0.1 s behind the finger (22 updates at most); a mouse needs no such lag, so the bird is at the cursor, as on the PC
                  "    local drag, lag = env.updateDraggingCursor, env.updateDraggingCursorLagOnly\n"
                  "    if _G.type(drag) == \"function\" then\n"
                  "        local wait = 1\n"
                  "        env.updateDraggingCursor = function(time, dt)\n"
                  "            local c, p = env.g_aimingAidCursor, env.cursorPhysics\n"
                  "            wait = wait + (env.abFrameDt or 1 / 60)\n"
                  "            if wait >= 1 / 60 or _G.type(c) ~= \"table\" or _G.type(p) ~= \"table\" then wait = _G.math.min(wait - 1 / 60, 1 / 60) return drag(time, dt) end\n"
                  "            if not c.onHold and c.cursorToUse then c.cursorToUse = {x = p.x, y = p.y} end\n"
                  "        end\n"
                  "    end\n"
                  "    if _G.type(lag) == \"function\" then\n"
                  "        env.updateDraggingCursorLagOnly = function(time, dt)\n"
                  "            lag(time, dt)\n"
                  "            local c, p = env.g_aimingAidCursor, env.cursorPhysics\n"
                  "            if _G.type(c) == \"table\" and _G.type(c.cursorToUse) == \"table\" and _G.type(p) == \"table\" then c.cursorToUse.x, c.cursorToUse.y = p.x, p.y end\n"
                  "        end\n"
                  "    end\n"
                  "end\n"
                  "local sw = env.settingsWrapper\n"
                  "if (env.powerupManager ~= nil or env.PowerupManager ~= nil) and env.g_powerups_enabled == false then env.g_powerups_enabled = true end\n"
                  "if _G.type(sw) == \"table\" and _G.type(sw.getItemAmount) == \"function\" and not sw.abFreePowerups then\n"
                  "    sw.abFreePowerups = true\n"
                  "    local amount, consume = sw.getItemAmount, sw.consumeItem\n"
                  "    local function tutorial() local s = env.social return _G.type(s) == \"table\" and _G.type(s.getSessionType) == \"function\" and s.getSessionType() == \"Tutorial\" end   -- Friends 2.0.0's tutorial hands out the power-ups of each step itself\n"
                  "    sw.getItemAmount = function(self, item, ...) local a = amount(self, item, ...) if _G.type(a) == \"number\" and a < 1 and item ~= \"credits\" and not tutorial() then return 1 end return a end   -- (coins are not a power-up: Friends 2.0.0 keeps them as the item credits)\n"
                  "    if _G.type(consume) == \"function\" then sw.consumeItem = function(self, item, ...) local a = amount(self, item) if _G.type(a) == \"number\" and a < 1 and not tutorial() then return end return consume(self, item, ...) end end\n"
                  "end\n"
                  // Rio HD 2.0.0 keeps its power-ups in gamelua.Bank (settings.bank): the same rule, one counts as one when there are none
                  "local bank = env.Bank or (_G.type(env.gamelua) == \"table\" and env.gamelua.Bank)\n"
                  "if _G.type(bank) == \"table\" and _G.type(bank.getAmount) == \"function\" and _G.type(bank.withdraw) == \"function\" and not bank.abFree then\n"
                  "    bank.abFree = true\n"
                  "    local get, take, cons = bank.getAmount, bank.withdraw, env.Consumables or (_G.type(env.gamelua) == \"table\" and env.gamelua.Consumables)\n"
                  "    bank.getAmount = function(id, ...) local a = get(id, ...) if _G.type(a) == \"number\" and a < 1 then return 1 end return a end\n"
                  "    bank.withdraw = function(id, ...) local a = get(id) if _G.type(a) == \"number\" and a < 1 then return _G.type(cons) == \"table\" and cons.consumables and cons.consumables[id] end return take(id, ...) end\n"
                  "end\n"
                  // the Sling Scope's path is shown only in the frames whose drawSlingScope draws it
                  "local scope, draw = env.abScope or _G.abScope, env.drawGame\n"
                  "if scope and _G.type(draw) == \"function\" then\n"
                  "    env.drawGame = function(...)\n"
                  "        if scope.on and not scope.drawn then   -- not drawn since the last frame (the scope may be drawn after drawGame, by the hud)\n"
                  "            scope.on = false\n"
                  "            local aid = env.aimingAid\n"
                  "            if not (_G.type(aid) == \"table\" and aid.showing) then env.enableAimingAid(false) if env.clearAimingAid then env.clearAimingAid(1) end end\n"
                  "        end\n"
                  "        scope.drawn = false\n"
                  "        return draw(...)\n"
                  "    end\n"
                  "end\n").c_str());
        // v3.1: Game Center achievements: the list, the unlock pop-up, the how-to texts and the achievement page (achievements_page.lua)
        lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
        if (luaL_loadbuffer(L_, (const char*)kAchievementsPageLua, kAchievementsPageLuaSize, "achievements_page.lua") == 0) {
            lua_pushvalue(L_, -2);
            if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "achievements: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        } else { std::fprintf(stderr, "achievements: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        lua_pop(L_, 1);
        // v4.1: the wallet of coins (shared with the Social stand-in server), the fake ad and the language list (coins_language.lua)
        lua_getfield(L_, LUA_REGISTRYINDEX, "ab.env");
        if (luaL_loadbuffer(L_, (const char*)kCoinsLanguageLua, kCoinsLanguageLuaSize, "coins_language.lua") == 0) {
            lua_pushvalue(L_, -2);
            if (lua_pcall(L_, 1, 0, 0) != 0) { std::fprintf(stderr, "coins: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        } else { std::fprintf(stderr, "coins: %s\n", lua_tostring(L_, -1)); lua_pop(L_, 1); }
        lua_pop(L_, 1);
    }
    lua_pushnumber(L_, realDt > 0 ? realDt : dt);
    SetEnvField("abFrameDt");                // the frame's real time, for the scripts' fixed per-update steps (abFrameSteps)
    if (!Call("abAchievementInput")) return false;   // the achievement page takes the input while it is open
    GetEnvField("update");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushnumber(L_, dt);
    lua_pushnumber(L_, realDt);
    bool ok = CallWithArgs("update", 2);
    if (ok && !nativeDots_) {   // the later games record the aiming dots in the exe (addTrajectoryPositionsForBirds), 1.0 and the other early ones in their script (addToTrajectory)
        nativeDots_ = true;
        RunString(
            "local env = _G.getfenv(1)\n"
            "local rawAdd = env.addToTrajectory\n"
            "if rawAdd == nil then return end\n"
            "local scriptDots = false\n"
            "env.addToTrajectory = function(...) scriptDots = true; return rawAdd(...) end\n"
            "local lastDot = {}   -- the last dot of each list, for the games that keep no birdTrajectory table (Friends 2.0.0)\n"
            "local rawNew = env.startNewTrajectory\n"
            "if rawNew then env.startNewTrajectory = function(...) lastDot = {} return rawNew(...) end end\n"
            "local function rec(o, n)\n"
            "    if o == nil or o.recordTrajectory == false or env.physicsToWorldTransform == nil then return end\n"
            "    local x, y = env.physicsToWorldTransform(o.x, o.y)\n"
            "    local list = env.birdTrajectory and env.birdTrajectory[n]\n"
            "    local last = (list and list[#list]) or lastDot[n]\n"
            "    if last and (x - last.x) * (x - last.x) + (y - last.y) * (y - last.y) <= 400 then return end\n"
            "    if list then list[#list + 1] = {x = x, y = y} end\n"
            "    lastDot[n] = {x = x, y = y}\n"
            "    rawAdd(n, x, y)\n"
            "end\n"
            "env.abEditorEscape = function()   -- Esc on the level-clear / level-failed page of a playtest from the editor goes back to the editor (the game's own pages ignore it)\n"
            "    local p = env.currentMenuPage\n"
            "    if env.startedFromEditor == true and env.returnToEditor and p ~= nil and (p == env.levelComplete or p == env.levelFailed) and env.keyReleased and env.keyReleased[\"ESCAPE\"] then\n"
            "        env.keyReleased[\"ESCAPE\"] = nil; if env.keyPressed then env.keyPressed[\"ESCAPE\"] = nil end\n"
            "        env.returnToEditor()\n"
            "    end\n"
            "end\n"
            "env.abRestartKey = function()   -- Angry Birds 1.0 has no key for restarting; R restarts the level like the pause menu's button\n"
            "    if env.abBoxHalfExtents and env.restartLevel and env.keyPressed and env.keyPressed[\"R\"] and env.currentGameMode == env.updateGame and env.keyHold and not env.keyHold[\"CONTROL\"] and not env.keyHold[\"SHIFT\"] then\n"
            "        env.keyPressed[\"R\"] = nil\n"
            "        env.restartLevel()\n"
            "    end\n"
            "end\n"
            "local gravNative\n"
            "env.abGravityKeys = function()   -- world gravity in the editor of every game: Shift+G +1, Ctrl+G -1, Shift+Ctrl+G back to 20 (a game whose editor already has these keys, like the classic one and Seasons 2.4.1's, keeps its own)\n"
            "    if gravNative == nil then\n"
            "        gravNative = true\n"
            "        local okd, dump = _G.pcall(_G.string.dump, env.updateEditor)\n"
            "        if okd and env.updateEditor and env.setWorldGravity and not _G.string.find(dump, \"orldGravity\", 1, true) then gravNative = false end\n"
            "    end\n"
            "    if gravNative or env.currentGameMode ~= env.updateEditor or env.objects == nil or env.keyPressed == nil or not env.keyPressed[\"G\"] or not env.keyHold then return end\n"
            "    if not (env.keyHold[\"SHIFT\"] or env.keyHold[\"CONTROL\"]) then return end\n"
            "    if env.objects.worldGravity == nil then local gx, gy = nil, nil; if env.getWorldGravity then gx, gy = env.getWorldGravity() end; env.objects.worldGravity = gy or 20 end\n"
            "    if env.keyHold[\"SHIFT\"] and env.keyHold[\"CONTROL\"] then env.objects.worldGravity = 20 elseif env.keyHold[\"SHIFT\"] then env.objects.worldGravity = env.objects.worldGravity + 1 else env.objects.worldGravity = env.objects.worldGravity - 1 end\n"
            "    env.setWorldGravity(0, env.objects.worldGravity)\n"
            "    if env.levelSaved ~= nil then env.levelSaved = false end\n"
            "    _G.print(\"world gravity: \" .. env.objects.worldGravity .. \"\\n\")\n"
            "end\n"
            "env.abEditorPlay = function()   -- 3.6: Ctrl+P in the editor of every game turns the physics on and off, so blocks fall, take damage and run their script (the earlier games' editors have no key for it); env.editorBehaviours = false turns the key off\n"
            "    if env.editorBehaviours == false or env.currentGameMode ~= env.updateEditor or env.keyPressed == nil or not env.keyPressed[\"P\"] or not env.keyHold or not env.keyHold[\"CONTROL\"] or env.keyHold[\"SHIFT\"] then return end\n"
            "    if env.setPhysicsEnabled == nil or env.isPhysicsEnabled == nil then return end\n"
            "    local on = not env.isPhysicsEnabled()\n"
            "    env.setPhysicsEnabled(on)\n"
            "    _G.print(\"editor physics: \" .. (on and \"on\" or \"off\") .. \"\\n\")\n"
            "end\n"
            "env.abRecordDots = function()\n"
            "    if scriptDots or env.objects == nil or env.objects.world == nil then return end\n"
            "    rec(env.flyingBird, 1)\n"
            "    if env.otherBirds then for i = 1, 2 do local o = env.objects.world[env.otherBirds[i]]; if o ~= nil then rec(o, i + 1) end end end\n"
            "end\n");
    }
    if (ok && !pausePops_) {      // Seasons 2.3.0: the point pop-ups (5K pig sprites, scores) are drawn only with the HUD, so they vanished while paused; installed once the game has made floatingScores
        GetEnvField("floatingScores");
        bool have = lua_istable(L_, -1);
        lua_pop(L_, 1);
        if (have) {
            pausePops_ = true;
            RunString(
            "local env = _G.getfenv(1)\n"
            "if env.floatingScores ~= nil and env.drawMenuPage and env.pausePage then   -- Seasons 2.3.0: the point pop-ups (5K pig sprites, scores) are only drawn with the HUD, so they vanished while paused; they stay now\n"
            "    local rawDraw = env.drawMenuPage\n"
            "    env.drawMenuPage = function(page, ...)\n"
            // drawn before the page, so they sit over the level and under the pause menu's shade and buttons (they were drawn over all of it)
            "        if page == env.pausePage and env.currentGameMode == env.updateMenu and #env.floatingScores > 0 and env.physicsToWorldTransform and env.screen then\n"
            "            local okp, perr = _G.pcall(function()\n"
            "                env.setRenderState(0, 0, 1, 1, 0); env.setFont(\"FONT_SCORE\")\n"
            "                for i = 1, #env.floatingScores do\n"
            "                    local fs = env.floatingScores[i]\n"
            "                    local fx, fy = env.physicsToWorldTransform(fs.x, fs.y)\n"
            "                    local ws = env.tempWorldScale or env.worldScale\n"
            "                    fx = (fx - env.screen.left) * ws; fy = (fy - env.screen.top) * ws\n"
            "                    local xs = fs.xs\n"
            "                    if xs and xs > 0 then\n"
            "                        env.setRenderState(0, 0, xs, xs)\n"
            "                        if fs.text ~= nil then env.res.drawString(\"TEXTS_BASIC\", fs.text, fx / xs, fy / xs, \"BOTTOM\", \"HCENTER\") end\n"
            "                        if fs.sprite ~= nil then env.res.drawSprite(\"MENU_ELEMENTS_1\", fs.sprite, _G.math.floor(fx / xs), _G.math.floor(fy / xs), \"BOTTOM\", \"HCENTER\") end\n"
            "                    end\n"
            "                end\n"
            "                env.setRenderState(0, 0, 1, 1, 0); if env.fontBasic then env.setFont(env.fontBasic) end\n"
            "            end)\n"
            "            if not okp and not env.abPopWarned then env.abPopWarned = true; _G.print(\"pause pop-ups: \" .. _G.tostring(perr)) end\n"
            "        end\n"
            "        return rawDraw(page, ...)\n"
            "    end\n"
            "end\n"            );
        }
    }
    if (ok && mobileData_) ok = Call("abFrameSteps");
    if (ok && stella_) ok = Call("abGuardStellaScene");   // v3.2: GameScene:onExit once per visit (stella_natives.lua)
    if (ok && nativeDots_) ok = Call("abRecordDots");
    if (ok && nativeDots_) ok = Call("abEditorEscape");
    if (ok && nativeDots_) ok = Call("abRestartKey");
    if (ok && nativeDots_) ok = Call("abGravityKeys");
    if (ok && nativeDots_) ok = Call("abEditorPlay");
    if (ok) ok = Call("waterUpdate");        // Seasons 2.4.1 (Piglantis): the exe calls the script's waterUpdate() every frame (buoyancy, drag and splashes of the objects below objects.waterLevel)
    {   // the water layers scroll and wobble only while the level runs (or is edited): not under the pause menu or other pages
        GetEnvField("currentGameMode"); GetEnvField("updateGame"); GetEnvField("updateEditor");
        bool running = lua_rawequal(L_, -3, -2) || lua_rawequal(L_, -3, -1) || lua_isnil(L_, -2);
        lua_pop(L_, 3);
        if (running) platform_.ScrollWaterLayers(dt);
    }
    if (ok) ok = DrawEditorOverlay();
    EmitHook(L_, "update", nullptr, nullptr, 2, dt, realDt);   // engine.on("update", function(dt, realDt) ... end)
    SaveStatesFrame(L_, realDt > 0 ? realDt : dt);
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
    bool ok = Call("drawEditor");
    GetEnvField("__abDrawEditorPage");
    if (lua_isfunction(L_, -1)) { lua_pop(L_, 1); Call("__abDrawEditorPage"); } else lua_pop(L_, 1);
    return ok;
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

bool ScriptHost::BlockCollision(const std::string& a, const std::string& b, float force, bool wasDamageDone, float damage) {
    GetEnvField("blockCollision");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return true; }
    lua_pushlstring(L_, a.data(), a.size());
    lua_pushlstring(L_, b.data(), b.size());
    lua_pushnumber(L_, force);
    if (config_.seasons4 || friends2_) lua_pushnumber(L_, wasDamageDone ? (damage > 0 ? damage : 1.0f) : 0.0f);   // 3.x and Friends 2.0.0: blockCollision(name, other, force, damage)
    else lua_pushboolean(L_, wasDamageDone);
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
