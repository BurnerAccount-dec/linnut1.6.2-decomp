-- Angry Birds Stella 1.0.0 (iPhone .ipa): the natives its scripts call that the exe had and this engine has not.
-- Stella is built on the Star Wars II code (gravity filters, Yoda force objects ...) with new services: Skynest (the Rovio account and
-- cloud saves), the social manager, the Telepods scanner, Flash animations (AnimationWrapperNative) and the shader parameters.
-- Online services answer "not available" or "not logged in"; the rest do the nearest thing the engine can.
local env = ...
local _G = _G
local none = function() end
local no = function() return false end

-- a table whose missing functions do nothing (and answer nil): the services the game asks about but cannot use here
local function module(name, fields)
    local t = fields or {}
    return setmetatable(t, {__index = function(_, k) return none end})
end
local function global(name, t)
    if rawget(_G, name) == nil then rawset(_G, name, t) end
    if rawget(env, name) == nil then rawset(env, name, rawget(_G, name)) end
end

-- table.remove as in Lua 5.1.5, which Stella was built with: a position outside 1..#t removes nothing. The engine's Lua (5.1.4) then
-- cleared the last element instead; the camera code removes its temporary camera (kept at index 99) that way, and losing the castle
-- camera stopped every shot when the bird was released
do
    local remove = table.remove
    table.remove = function(t, pos)
        if pos ~= nil then
            local n = #t
            if pos < 1 or pos > n then return end
        end
        return remove(t, pos)
    end
end

-- the render state the scripts set last: the Flash animations and the scaled sprite draws below set their own transform, keep the
-- scripts' transparency (the slingshot's pig highlight is drawn at alpha 0 until the player aims) and put the scripts' state back afterwards
local nativeSetRenderState = env.setRenderState
local nativeSetShader, nativeCreateShader, nativeShaderParam = env.setShader, env.createShader, env.setShaderParam      -- (the scripts define a setShader of their own for objects)
local renderArgs = {n = 0}
env.setRenderState = function(...)
    renderArgs = {n = select("#", ...), ...}
    return nativeSetRenderState(...)
end
local function scriptAlpha()
    local a = renderArgs.n >= 8 and renderArgs[8] or 1
    return type(a) == "number" and a or 1
end
local function restoreRenderState() nativeSetRenderState(unpack(renderArgs, 1, renderArgs.n)) end

-- shaders: the scripts ask for numbered copies of a shader ("2d-sprite-gold#1" ...) and set parameters on them (Shader.lua keeps those in Lua);
-- the drawing here has no shaders, so the names are only names
if env.createUniqueShaders == nil then
    env.createUniqueShaders = function(name, count)
        local list = {}
        for i = 1, count or 1 do list[i] = tostring(name) .. "#" .. i end
        return list
    end
end

-- ResourceManager: the sprite sheets and sounds go through the engine's own res functions
local res = _G.res
local nativePlay = res.playAudio      -- (the game puts its own playAudio into res.playAudio, which calls native_playAudio)
global("ResourceManager", module("ResourceManager", {
    native_createSpriteSheet = function(...) return res.createSpriteSheet(...) end,
    native_releaseSpriteSheet = function(...) return res.releaseSpriteSheet(...) end,
    native_createAudio = function(...) return res.createAudio and res.createAudio(...) end,
    native_createAudioFromAppData = function(...) return res.createAudio and res.createAudio(...) end,
    native_releaseAudio = function(...) return res.releaseAudio and res.releaseAudio(...) end,
    native_playAudio = function(...) return nativePlay(...) end,
}))
-- playAudioReturnUniqueHandle(name, volume, loop, pan): soundManager.lua keeps the native at load time; the clip name is the handle
-- (fadeAudioClip / setAudioClipVolume take it back)
if env.playAudioReturnUniqueHandle == nil then
    env.playAudioReturnUniqueHandle = function(name, volume, loop)
        if type(name) ~= "string" then return nil end
        nativePlay(name, volume or 1, loop and true or false)
        return name
    end
end
if env.setAudioClipVolume == nil then env.setAudioClipVolume = function(h, v) if type(h) == "string" then res.setAudioClipVolume(h, v or 1) end end end
if env.setMusicVolume == nil then env.setMusicVolume = none end
global("Assets", module("Assets", {
    createSpriteSheet = function(...) return res.createSpriteSheet(...) end,
    loadFiles = function(list, callback) if type(callback) == "function" then callback(true) end return true end,
}))

-- online services: not here
global("SkynestAccount", module("SkynestAccount", {native_isLoggedIn = no}))
global("SkynestStorage", module("SkynestStorage", {native_isTransactionInProcess = no}))
global("CloudSync", module("CloudSync", {}))
global("SocialManager", module("SocialManager", {native_isConnectedToSocialNetwork = no, native_getSocialNetworkName = function() return "" end,
    getFriendProgressForLevel = function() return {} end, isAvatarInGraphicsMemory = no}))
global("FusionGamerServices", module("FusionGamerServices", {isSupported = no, isLocalPlayerAuthenticated = no, getBackendName = function() return "" end}))
global("QrScanner", module("QrScanner", {isCameraSupported = no, isFrontCameraSupported = no}))
global("Analytics", module("Analytics", {}))
global("Apprater", module("Apprater", {}))
global("Telepods", module("Telepods", {}))
-- The Telepod configuration (assets_service/telepod_configuration.dat) is a cloud download the game never gets here: an empty one, so the bird skins
-- saved in a save file (read while the level is built: BirdAnimation) find no skin entry instead of stopping the level load with an error
global("g_telepodConfiguration", {})

-- native_setDensity(name, density), matched against the exe (0x34990): the density goes into the object's native data and its Lua table (world[name].density),
-- and the body takes it. Willow's roll and Stella's ability change the density of the bird this way.
env.native_setDensity = function(name, density)
    local w = type(env.objects) == "table" and env.objects.world
    if type(w) == "table" and type(w[name]) == "table" then w[name].density = density end
    if env.setObjectDensity then env.setObjectDensity(name, density) end
end
-- native_resizeRadius(name, radius, density, friction, restitution), matched against the exe (0x53d1c): the object's circle gets the new radius and the
-- values are stored in its native data (the exe stores them at +0x10c, +0xe4, +0xec ... and builds the circle again); the potion of the Inflate
-- power-up calls it with the definition's radius times the multiplier, and again with the original radius and values to shrink the pig back.
env.native_resizeRadius = function(name, radius, density, friction, restitution)
    local w = type(env.objects) == "table" and env.objects.world
    if type(w) == "table" and type(w[name]) == "table" then
        w[name].density, w[name].friction, w[name].restitution = density, friction, restitution
    end
    if env.resizeCircle then env.resizeCircle(name, radius) end
    if env.setObjectDensity and density then env.setObjectDensity(name, density) end
    if env.setFriction and friction then env.setFriction(name, friction) end
    if env.setRestitution and restitution then env.setRestitution(name, restitution) end
end
-- native_applySensorForces(sensor, object), matched against the exe (0x55ac4 -> 0x58104): the pull of a gravitation sensor (or the push of a water
-- sensor) on one object, applied once, now. The box water (BlockComponents/BoxWater.lua) calls it every physics step for each block inside it.
env.native_applySensorForces = function(sensor, object)
    if env.applySensorForces then env.applySensorForces(sensor, object) end
end
-- The trajectory simulation, matched against the exe (none of Stella's scripts use it): updateBirdTrajectoryTable() flies the object "BirdSimulation"
-- simulationIterations steps of objects.currentTimeStep * simulationTimeStepMultiplier under the gravitation and water sensors it overlaps and the
-- additional bird gravity, storing a point every simulationStorePointsSampler steps; getSimulationTrajectoryPoints() returns them ({x =, y =} in metres),
-- ClearSimulationTrajectory() empties them, native_drawSimulationTrajectory() draws the aiming aid (enableAimingAid, setAimingAidSprite) along them.
-- native_setAdditionalBirdGravity(g) (0x34e88): a downward force of g times the mass, in the simulation only; 0 or less is off (-1 at start).
env.native_setAdditionalBirdGravity = function(g) if env.setAdditionalBirdGravity and tonumber(g) then env.setAdditionalBirdGravity(tonumber(g)) end end
env.native_drawSimulationTrajectory = function() if env.drawSimulationTrajectory then env.drawSimulationTrajectory() end end
-- native_setThemeFgLayerOffsetY(theme, layer, offsetY), matched against the exe (0x3f630): foreground layer `layer` (1 is the first) of the theme takes
-- offsetY as its y offset (+52 of its record, the field the theme's offsetY fills); `theme` only names the theme in the exe's own bookkeeping.
-- Stella registers it but none of its scripts call it.
env.native_setThemeFgLayerOffsetY = function(theme, layer, offsetY)
    if env.setThemeLayer and tonumber(layer) and tonumber(offsetY) then env.setThemeLayer(math.floor(tonumber(layer)), {offY = tonumber(offsetY)}, 1) end
end
-- Natives Stella registers that none of its scripts call, matched against the exe (4.4):
-- getObjectVertices, decomposePolygon, objectAndTrackOverlap and drawPolygon are the engine's own functions of those names (see lua_api.cpp).
-- native_shareScreenShot() (0x552dc): saves the finished frame as Stella_Screenshot<n>.png, n counting from 1 (the game then opens the share sheet)
env.native_shareScreenShot = function() if env.shareScreenShot then env.shareScreenShot() end end
-- native_getOSName() (0x55a1c): builds an empty value and returns it, so it is always nil in this build
env.native_getOSName = function() return nil end
-- getDirectoryFileList(path) (0x54974): makes a new table and returns it without filling it, so it is always {}
env.getDirectoryFileList = function() return {} end
-- printWithTag(...) (0x348b8): an empty function in the shipped game
env.printWithTag = none
-- destroyUniqueShaders(list) (0x4b718): removes the named copies from the shader list, entries 1 to #list - 1 only (a list of fewer than
-- two names removes nothing). This engine draws without shaders and createUniqueShaders only makes names, so there is nothing to remove.
env.destroyUniqueShaders = none
-- onLoadLuaFileFail(path) (0x51640): the game calls the script's onLoadLuaFileFail(path) when gamelogic.lua does not load; a script that
-- defines one replaces this, which on its own does nothing
if env.onLoadLuaFileFail == nil then env.onLoadLuaFileFail = none end
-- native_setSprite(name, sprite): the object is drawn with that sprite (utils.lua setSprite, which keeps the name it last set)
-- native_getLevelLimits (0x54468) returns the four floats the scene keeps at +0x360 (minX, minY, maxX, maxY), which setLevelLimits (0x4c850) stores
local levelLimits = {0, 0, 0, 0}
do
    local engineSetLevelLimits = env.setLevelLimits
    if engineSetLevelLimits then
        env.setLevelLimits = function(a, b, c, d)
            levelLimits = {tonumber(a) or 0, tonumber(b) or 0, tonumber(c) or 0, tonumber(d) or 0}
            return engineSetLevelLimits(a, b, c, d)
        end
    end
end
env.native_getLevelLimits = function() return levelLimits[1], levelLimits[2], levelLimits[3], levelLimits[4] end
-- 0x49020: stores the two numbers on the scene; drawGameNative (0x4902c) draws the z layers lo .. hi-1 (hi <= -1: 170)
env.native_setZOrderRange = function(lo, hi)
    if env.setDrawZRange then env.setDrawZRange(math.floor(tonumber(lo) or -1), math.floor(tonumber(hi) or -1)) end
end
local engineSetSprite = env.setSprite   -- the engine's own: utils.lua defines a global setSprite later that calls native_setSprite
env.native_setSprite = function(name, sprite)
    if engineSetSprite then engineSetSprite(name, sprite) end
end
-- native_getSimpleRandom(seed), matched against the exe (0x55cb0): returns the next seed and a number from 0 to 1. The phone computes in 32 bit integers and
-- 32 bit floats (the seed travels through a float between two calls), so does this, and the same seed gives the same numbers
local function f32(x)
    if x ~= x or x == 0 or x == math.huge or x == -math.huge then return x end
    local m, e = math.frexp(x)
    local t = math.abs(m) * 16777216
    local f = math.floor(t)
    local d = t - f
    if d > 0.5 or (d == 0.5 and f % 2 == 1) then f = f + 1 end
    return (m < 0 and -f or f) / 16777216 * 2 ^ e
end
env.native_getSimpleRandom = function(seed)
    local s = f32(tonumber(seed) or 0)
    if s ~= s then s = 0 end
    s = s >= 0 and math.floor(s) or math.ceil(s)                 -- vcvt.s32.f32: toward zero, saturating
    if s > 2147483647 then s = 2147483647 elseif s < -2147483648 then s = -2147483648 end
    local r = (s * 214013 + 2531011) % 4294967296                -- mla r0, r0, 0x343FD, 0x269EC3 (wraps at 32 bit)
    if r == 4294967295 then r = 0 end                              -- the exe's modulo step adds 1 here
    local signed = r >= 2147483648 and r - 4294967296 or r
    local rnd = math.floor(r / 65536) % 32768                       -- bits 16..30
    return f32(signed), f32(rnd * (2 ^ -15) * (1 + 2 ^ -15))      -- (constant 0x38000100)
end
-- EconomyParameters.loadConfiguration(file): the prices and rewards (config/economy.dat, plain JSON) are handed to economyParametersLoaded;
-- the copy in the app data is never made here, so the bundle's file is read
global("EconomyParameters", module("EconomyParameters", {
    loadConfiguration = function(file)
        local t = env.abReadJson and (env.abReadJson(tostring(env.configPath or "config") .. "/economy.dat") or env.abReadJson(file))
        if type(t) == "table" and type(env.economyParametersLoaded) == "function" then env.economyParametersLoaded(t) end
    end,
}))
global("GiftBirdSkins", module("GiftBirdSkins", {}))
global("ParticleEditorNative", module("ParticleEditorNative", {}))

if env.copyFileFromBundleToAppData == nil then env.copyFileFromBundleToAppData = function() return true end end   -- (the game then reads the bundle's copy)

-- ---------------------------------------------------------------------------------------------------------------------------------
-- AnimationWrapperNative: the Flash animations (animations/<name>.anim.json and .skins.json, exported from Spine: a tree of entities
-- (bones), slots that show a sprite, and actions whose tracks move them). The exe played and drew them; this is a player in Lua.
--   entity tracks: translation (LinearFloat2), rotation (LinearFloat, radians), scale (LinearFloat2)
--   slot tracks:   sprite (DiscreteString), alpha (LinearFloat), zOrder (DiscreteInt); spineEvent (DiscreteString) on any entity
--   skins:         skins[skin][slot name without SLOT_][sprite] = {x, y}: where the sprite's centre sits on its slot
-- The animation is drawn at the translation / rotation / scale the scripts set (screen pixels), y down.
local floor, sin, cos, abs = math.floor, math.sin, math.cos, math.abs
local files = {}         -- parsed files by name
local anims = {}         -- by tag

local function parse(file)
    if files[file] ~= nil then return files[file] end
    local data = env.abReadJson and env.abReadJson(file)
    if type(data) ~= "table" then files[file] = false return false end
    local f = {entities = {}, order = {}, actions = {}, skins = {}}
    local function walk(node, parent)
        if type(node) ~= "table" then return end
        if node.name then
            local e = {name = node.name, parent = parent, index = #f.order + 1, slot = string.sub(node.name, 1, 5) == "SLOT_"}
            f.entities[node.name] = e
            f.order[#f.order + 1] = e
            parent = e
        end
        if type(node.children) == "table" then for _, c in ipairs(node.children) do walk(c, parent) end end
    end
    walk(data, nil)
    for _, comp in ipairs(type(data.comps) == "table" and data.comps or {}) do
        local acts = type(comp.data) == "table" and comp.data.actions
        if type(acts) == "table" then
            for name, a in pairs(acts) do
                local targets, length = {}, 0
                for _, clip in pairs(type(a.clips) == "table" and a.clips or {}) do
                    for tname, tracks in pairs(type(clip.targets) == "table" and clip.targets or {}) do
                        targets[tname] = targets[tname] or {}
                        for k, tr in pairs(tracks) do
                            targets[tname][k] = tr
                            local kf = tr.keyframes
                            if type(kf) == "table" and #kf > 0 and kf[#kf][1] > length then length = kf[#kf][1] end
                        end
                    end
                end
                f.actions[name] = {targets = targets, length = length}
            end
        end
    end
    local skinFile = string.gsub(file, "%.anim%.json$", ".skins.json")
    local skins = skinFile ~= file and env.abReadJson and env.abReadJson(skinFile)
    if type(skins) == "table" then f.skins = skins end
    files[file] = f
    return f
end

-- a track's value at time t: discrete tracks keep the last key at or before t, linear ones blend the two around it
local function sample(track, t)
    local kf = track.keyframes
    if type(kf) ~= "table" or #kf == 0 then return nil end
    if t <= kf[1][1] then return kf[1][2] end
    local n = #kf
    if t >= kf[n][1] then return kf[n][2] end
    local lo, hi = 1, n
    while hi - lo > 1 do local mid = floor((lo + hi) / 2) if kf[mid][1] <= t then lo = mid else hi = mid end end
    local a, b = kf[lo], kf[hi]
    if string.sub(track.type or "", 1, 8) == "Discrete" then return a[2] end
    local span = b[1] - a[1]
    local k = span > 0 and (t - a[1]) / span or 0
    if type(a[2]) == "table" then return {a[2][1] + (b[2][1] - a[2][1]) * k, a[2][2] + (b[2][2] - a[2][2]) * k} end
    return a[2] + (b[2] - a[2]) * k
end

-- the pose of every entity at the animation's time: local values from the action's tracks (kept from the last action that set them)
local function pose(a)
    local f = a.data
    if not f then return end
    local act = a.action and f.actions[a.action]
    a.state = a.state or {}
    for _, e in ipairs(f.order) do
        local s = a.state[e.name]
        if not s then s = {x = 0, y = 0, r = 0, sx = 1, sy = 1, alpha = 1, z = e.index} a.state[e.name] = s end
        local tr = act and act.targets[e.name]
        if tr then
            local v
            if tr.translation then v = sample(tr.translation, a.time) if v then s.x, s.y = v[1], v[2] end end
            if tr.rotation then v = sample(tr.rotation, a.time) if v then s.r = v end end
            if tr.scale then v = sample(tr.scale, a.time) if v then s.sx, s.sy = v[1], v[2] end end
            if tr.alpha then v = sample(tr.alpha, a.time) if v then s.alpha = v end end
            if tr.sprite then v = sample(tr.sprite, a.time) if v ~= nil then s.sprite = v end end
            if tr.zOrder then v = sample(tr.zOrder, a.time) if v then s.z = v end end
        end
    end
    -- world (animation space) transforms, parents first (the tree order puts them first)
    for _, e in ipairs(f.order) do
        local s = a.state[e.name]
        local p = e.parent and a.state[e.parent.name]
        if p then
            local c, n = cos(p.wr), sin(p.wr)
            local lx, ly = s.x * p.wsx, s.y * p.wsy
            s.wx, s.wy = p.wx + c * lx - n * ly, p.wy + n * lx + c * ly
            s.wr, s.wsx, s.wsy, s.walpha = p.wr + s.r, p.wsx * s.sx, p.wsy * s.sy, p.walpha * s.alpha
        else
            s.wx, s.wy, s.wr, s.wsx, s.wsy, s.walpha = s.x, s.y, s.r, s.sx, s.sy, s.alpha
        end
    end
end

local function fire(a, event, ...)
    if type(a.handler) == "function" then
        local ok, err = pcall(a.handler, a.tag, a.action, event, ...)
        if not ok then print("animation event " .. tostring(event) .. ": " .. tostring(err)) end
    end
end

-- the spine events whose key time was passed between t0 and t1 ("playAudio:::sound" is the event playAudio with the parts after it)
local function events(a, t0, t1)
    local act = a.data and a.action and a.data.actions[a.action]
    if not act then return end
    for _, tr in pairs(act.targets) do
        local ev = tr.spineEvent
        if ev and type(ev.keyframes) == "table" then
            for _, k in ipairs(ev.keyframes) do
                if k[1] > t0 and k[1] <= t1 and type(k[2]) == "string" and k[2] ~= "" then
                    local parts = {}
                    for piece in string.gmatch(k[2] .. ":", "([^:]*):") do parts[#parts + 1] = piece end
                    fire(a, parts[1], select(2, unpack(parts)))
                end
            end
        end
    end
end

local function get(tag) return anims[tag] end
local function load(tag, file)
    local a = anims[tag] or {tag = tag, tx = 0, ty = 0, rot = 0, sx = 1, sy = 1, speed = 1, time = 0, skin = "default"}
    a.file, a.data, a.action, a.playing, a.time, a.state, a.placed = file, parse(file) or nil, nil, false, 0, nil, nil
    anims[tag] = a
    return a
end

local W = {}
W.preloadFromBundle = function(file) parse(file) end
W.preloadFromAppData = W.preloadFromBundle
W.loadFromBundle = function(tag, file) load(tag, file) end
W.loadFromAppData = W.loadFromBundle
W.close = function(tag) anims[tag] = nil end
W.clearCache = function() files = {} end
W.setPlaybackEvent = function(tag, handler) local a = get(tag) if a then a.handler = handler end end
W.start = function(tag, action, mode)
    local a = get(tag)
    if not a then return end
    a.action, a.mode, a.time, a.playing, a.paused = action, mode or "once", 0, true, false
    pose(a)
    events(a, -1, 0)
end
W.stop = function(tag) local a = get(tag) if a then a.playing = false end end
W.pause = function(tag) local a = get(tag) if a then a.paused = true end end
W.resume = function(tag) local a = get(tag) if a then a.paused = false end end
W.isPlaying = function(tag) local a = get(tag) return a ~= nil and a.playing == true end
W.seek = function(tag, t) local a = get(tag) if a then a.time = t or 0 pose(a) end end
W.setSpeed = function(tag, s) local a = get(tag) if a then a.speed = s or 1 end end
W.setTranslation = function(tag, x, y) local a = get(tag) if a then a.tx, a.ty, a.placed = x or 0, y or 0, true end end
W.setRotation = function(tag, r) local a = get(tag) if a then a.rot = r or 0 end end
W.setScale = function(tag, sx, sy) local a = get(tag) if a then a.sx, a.sy = sx or 1, sy or sx or 1 end end
W.setShader = function(tag, s) local a = get(tag) if a then a.shader = s end end
W.setSkin = function(tag, s) local a = get(tag) if a then a.skin = s or "default" end end
W.getActions = function(tag)
    local a = get(tag)
    local list = {}
    if a and a.data then for name in pairs(a.data.actions) do list[#list + 1] = name end end
    table.sort(list)
    return list
end
W.containsEntity = function(tag, name) local a = get(tag) return a ~= nil and a.data and a.data.entities[name] ~= nil or false end

local function entityState(tag, name)
    local a = get(tag)
    if not (a and a.data and a.data.entities[name]) then return nil end
    if not a.state then pose(a) end
    return a.state and a.state[name]
end
-- the entity in the animation's own space: x, y, scaleX, scaleY, angle
W.getEntityWorldTransform = function(tag, name)
    local s = entityState(tag, name)
    if not s then return nil end
    return s.wx, s.wy, s.wsx, s.wsy, s.wr
end
W.getEntityWorldPosition = function(tag, name) local s = entityState(tag, name) if s then return s.wx, s.wy end return 0, 0 end
W.getEntityPosition = function(tag, name) local s = entityState(tag, name) if s then return s.x, s.y end return 0, 0 end
W.getEntityScale = function(tag, name) local s = entityState(tag, name) if s then return s.wsx, s.wsy end return 1, 1 end

W.update = function(dt)
    dt = dt or 0
    for _, a in pairs(anims) do
        if a.playing and not a.paused and a.data then
            local act = a.action and a.data.actions[a.action]
            local length = act and act.length or 0
            local t0 = a.time
            a.time = a.time + dt * (a.speed or 1)
            if a.time >= length then
                events(a, t0, length)
                if a.mode == "repeat" and length > 0 then
                    a.time = a.time % length
                    events(a, -1, a.time)
                else
                    a.time = length
                    a.playing = false
                    pose(a)
                    fire(a, "PLAYBACK_END")
                end
            else
                events(a, t0, a.time)
            end
            pose(a)
        end
    end
end

local function offsetOf(f, skin, slot, sprite)
    local key = string.sub(slot, 6)
    for _, name in ipairs({skin, "default"}) do
        local sk = f.skins[name]
        local slotSkin = type(sk) == "table" and sk[key]
        local o = type(slotSkin) == "table" and slotSkin[sprite]
        if type(o) == "table" then return o.x or 0, o.y or 0 end
    end
    return 0, 0
end

-- a slot's sprite rectangle in the animation's space: left, top, right, bottom. The sprite's centre sits at its skin offset from the
-- slot (the comic cutscenes clip each panel to its border slot, whose entity point is shared by all four borders)
W.getEntityWorldBounds = function(tag, name)
    local s = entityState(tag, name)
    if not (s and s.sprite and s.sprite ~= "") then return 0, 0, 0, 0 end
    local a = get(tag)
    local w, h = 0, 0
    if res.getSpriteBounds then w, h = res.getSpriteBounds(s.sprite) w, h = w or 0, h or 0 end
    local ox, oy = offsetOf(a.data, a.skin, name, s.sprite)
    local cr, sr = cos(s.wr), sin(s.wr)
    local ax, ay = ox * s.wsx, oy * s.wsy
    local cx, cy = s.wx + cr * ax - sr * ay, s.wy + sr * ax + cr * ay
    local hw, hh = w * abs(s.wsx) / 2, h * abs(s.wsy) / 2
    local ex, ey = abs(cr) * hw + abs(sr) * hh, abs(sr) * hw + abs(cr) * hh
    return cx - ex, cy - ey, cx + ex, cy + ey
end

W.draw = function(tag, objectAlpha)
    local a = get(tag)
    if not (a and a.data) then return end
    if not a.state then pose(a) end
    local f = a.data
    local alpha = objectAlpha or scriptAlpha()        -- (a cutscene or the scripts fading the animation out; an object draws at its own)
    local slots = {}
    for _, e in ipairs(f.order) do
        local s = a.state[e.name]
        if e.slot and s.sprite and s.sprite ~= "" and s.walpha > 0 then slots[#slots + 1] = e end
    end
    -- a larger zOrder is further back (the comic panels: 10 the picture, 1-4 its borders; Stella: 18 the body, 14-17 the eyes and beak)
    table.sort(slots, function(p, q) local zp, zq = a.state[p.name].z, a.state[q.name].z if zp ~= zq then return zp > zq end return p.index < q.index end)
    local c, n = cos(a.rot), sin(a.rot)
    for _, e in ipairs(slots) do
        local s = a.state[e.name]
        local ox, oy = offsetOf(f, a.skin, e.name, s.sprite)
        -- the sprite's centre in animation space, then on the screen
        local cr, sr = cos(s.wr), sin(s.wr)
        local ax, ay = ox * s.wsx, oy * s.wsy
        local px, py = s.wx + cr * ax - sr * ay, s.wy + sr * ax + cr * ay
        px, py = px * a.sx, py * a.sy
        local X, Y = a.tx + c * px - n * py, a.ty + n * px + c * py
        local ksx, ksy = s.wsx * a.sx, s.wsy * a.sy
        local w, h = 0, 0
        if res.getSpriteBounds then w, h = res.getSpriteBounds(s.sprite) end
        if w and w > 0 and ksx ~= 0 and ksy ~= 0 then
            nativeSetRenderState(0, 0, ksx, ksy, s.wr + a.rot, w / 2, h / 2, s.walpha * alpha)
            res.drawSprite(s.sprite, X / ksx - w / 2, Y / ksy - h / 2, "TOP", "LEFT")   -- (the default puts the pivot there)
        end
    end
    restoreRenderState()
end
W.debugAnimations = function() return anims end      -- (console: the playing animations by tag)
global("AnimationWrapperNative", module("AnimationWrapperNative", W))

-- Align.getPositionAndScale(spec, refW, refH, screenW, screenH): a layout point given on the reference screen (posx, posy, scalex, scaley,
-- alignH LEFT / CENTER / RIGHT, alignV TOP / CENTER / BOTTOM) on the real screen: the reference is scaled to fit and every point keeps its
-- distance from the edge (or the middle) it is aligned to. Returns x, y, scaleX, scaleY
global("Align", module("Align", {
    getPositionAndScale = function(spec, rw, rh, sw, sh)
        spec = spec or {}
        rw, rh, sw, sh = rw or 1024, rh or 768, sw or rw or 1024, sh or rh or 768
        local k = math.min(sw / rw, sh / rh)
        local px, py = spec.posx or 0, spec.posy or 0
        local x, y
        if spec.alignH == "LEFT" then x = px * k elseif spec.alignH == "RIGHT" then x = sw - (rw - px) * k else x = sw / 2 + (px - rw / 2) * k end
        if spec.alignV == "TOP" then y = py * k elseif spec.alignV == "BOTTOM" then y = sh - (rh - py) * k else y = sh / 2 + (py - rh / 2) * k end
        return x, y, (spec.scalex or 1) * k, (spec.scaley or 1) * k
    end,
}))

-- createNonPhysicsObject(name, sprite, x, y, zOrder): an object that is only drawn (menu pieces, decorations): here a small static box that
-- collides with nothing
local drawnOnly = {}      -- name -> sprite of the objects made by createNonPhysicsObject
if env.createNonPhysicsObject == nil then
    env.createNonPhysicsObject = function(name, sprite, x, y, z)
        env.createBox(name, sprite, x, y, 0.1, 0.1, 0, 0, 0, false, false, z or 0)     -- (density 0: static, so decorations stay where the level puts them)
        drawnOnly[name] = sprite
    end
end
-- getIntersectingObjects also finds those objects where their sprite is drawn (the menu's play button, the map's level buttons are
-- tapped through it: BlockPointerEventSubsystem); the exe tested their sprite rectangles
local intersecting = env.getIntersectingObjects
if intersecting then
    env.getIntersectingObjects = function(q)
        local list = intersecting(q) or {}
        if type(q) ~= "table" then return list end
        local world = type(env.objects) == "table" and env.objects.world
        if type(world) ~= "table" then return list end
        local seen = {}
        for _, n in ipairs(list) do seen[n] = true end
        local ps = env.physicsScale or 0.05
        local qx, qy = q.x or 0, q.y or 0
        local l, r, d, u = math.abs(q.left or 0), math.abs(q.right or 0), math.abs(q.down or 0), math.abs(q.up or 0)
        local candidates = {}
        for name, sprite in pairs(drawnOnly) do if world[name] == nil then drawnOnly[name] = nil else candidates[name] = sprite end end
        if env.getObjectDefinition then      -- and the tappable objects (onPress / onTap / onDrag), which collide with nothing
            for name, o in pairs(world) do
                if type(o) == "table" and candidates[name] == nil then
                    local def = env.getObjectDefinition(name)
                    if type(def) == "table" and (def.onPress or def.onTap or def.onDrag) then candidates[name] = o.sprite end
                end
            end
        end
        for name, sprite in pairs(candidates) do
            local o = world[name]
            if not seen[name] and sprite and (not env.isVisible or env.isVisible(name)) then
                local w, h = 0, 0
                if res.getSpriteBounds then w, h = res.getSpriteBounds(o.sprite or sprite) w, h = w or 0, h or 0 end
                local px, py = w / 2, h / 2
                if res.getSpritePivot then local a, b = res.getSpritePivot(o.sprite or sprite) px, py = a or px, b or py end
                local sx, sy = 1, 1
                if env.getScale then local a, b = env.getScale(name) sx, sy = a or 1, b or a or 1 end
                local x, y = o.x or 0, o.y or 0
                local x1, x2 = x - px * sx * ps, x + (w - px) * sx * ps
                local y1, y2 = y - py * sy * ps, y + (h - py) * sy * ps
                if qx + r >= x1 and qx - l <= x2 and qy + u >= y1 and qy - d <= y2 then list[#list + 1] = name end
            end
        end
        return list
    end
end

-- the other natives of Stella's exe, on the engine's own functions where there is one
local function def(name, f) if env[name] == nil then env[name] = f end end
def("changeZOrder", function(name, z) if env.setObjectZOrder then env.setObjectZOrder(name, z) end end)
def("clearParticlesWithTagNative", function(tag) if tag == "MENU" and env.clearMenuParticlesNative then env.clearMenuParticlesNative() end end)
def("createNativeBlockExtension", function(kind, name) return {kind = kind, name = name} end)
def("createTrack", none)                 -- moving tracks (rails): objects stay where they are placed
def("destroyTrack", none)
def("getCurrentTrackAngle", function() return 0 end)
def("getRotation", function(name) return env.getAngle and env.getAngle(name) or 0 end)
def("setAngle", function(name, a) if env.setRotation then env.setRotation(name, a) end end)
def("getSimulationTrajectoryPoints", function() return {} end)
def("isCompoSprite", function(name)
    if res.getSpriteBounds then local w = res.getSpriteBounds(name) if w and w > 0 then return false end end
    if res.getCompoSpriteBounds then local x1, y1, x2, y2 = res.getCompoSpriteBounds(name) return x1 ~= nil and x2 ~= nil and x2 > x1 end
    return false
end)
def("isHorizontallyFlipped", function(name) local o = type(env.objects) == "table" and env.objects.world and env.objects.world[name] return o ~= nil and o.flipped == true end)
def("isObjectAffectedByForce", function() return true end)
def("isSleeping", function(name)
    if not env.getLinearVelocity then return false end
    local vx, vy = env.getLinearVelocity(name)
    return (vx or 0) * (vx or 0) + (vy or 0) * (vy or 0) < 0.0001
end)
def("setGravityScale", function(name, s) if env.setObjectGravityScale then env.setObjectGravityScale(name, s) end end)
def("setPhysicsScale", function(name, sx, sy) if env.setScale then env.setScale(name, sx or 1, sy or sx or 1) end end)
def("unlockPhysicsLock", none)
def("lockPhysicsLock", none)
def("updateThemeParticlesNative", none)
def("enableGameUpdate", none)
-- setFlashAnimation(object [, file, action]): the object is drawn as the Flash animation with its name as the tag instead of its sprite.
-- With a file (a block whose definition has flashAnimation) the animation is loaded here and follows the object; without one a
-- FlashAnimationWrapper of the scripts plays it and places it (attach). removeFlashAnimation(object) draws the sprite again.
local flashObjects = {}
def("setFlashAnimation", function(name, file, action)
    if type(name) ~= "string" then return end
    flashObjects[name] = true
    if type(file) == "string" then
        local a = load(name, file)
        if a.data then
            if not (action and a.data.actions[action]) then action = W.getActions(name)[1] end
            if action then W.start(name, action, "repeat") end
        end
    end
end)
def("removeFlashAnimation", function(name) if name then flashObjects[name] = nil end end)
-- native_setPreDrawFunction(object, f) / native_setPostDrawFunction(object, f): f(block, flipped) is called as the object is drawn, before its sprite
-- (the costume's back layer, the willow's trail) or after it (the pig's pupils, the costume's front layer). DrawCalls.lua sets one function per object
-- that runs all of the object's draw calls; nil takes it away. `block` is the script's own table of the object (objects.world[object]).
local useShader      -- (defined with the shaders below)
local preDraw, postDraw = {}, {}
env.native_setPreDrawFunction = function(name, f) if type(name) == "string" then preDraw[name] = f end end
env.native_setPostDrawFunction = function(name, f) if type(name) == "string" then postDraw[name] = f end end
local function runDrawHook(list, name)
    local f = list[name]
    if type(f) ~= "function" then return end
    local world = type(env.objects) == "table" and env.objects.world
    local block = type(world) == "table" and world[name]
    if type(block) ~= "table" then return end
    if env.isVisible and not env.isVisible(name) then return end
    local flipped = env.isHorizontallyFlipped and env.isHorizontallyFlipped(name) or false
    local ok, err = pcall(f, block, flipped)
    if not ok and list[name] == f then list[name] = nil print("draw function of " .. name .. " removed: " .. tostring(err)) end
    restoreRenderState()
end
env.engine = env.engine or {}
local shadedObjects = {}
env.engine.drawObjectAfter = function(name)
    if shadedObjects[name] then shadedObjects[name] = nil nativeSetShader("none") end
    if postDraw[name] then runDrawHook(postDraw, name) end
end
local previousDrawObject = env.engine.drawObject
env.engine.drawObject = function(name, ...)
    if preDraw[name] then runDrawHook(preDraw, name) end
    local world = type(env.objects) == "table" and env.objects.world
    local block = type(world) == "table" and world[name]
    if type(block) == "table" and block.shader ~= nil then      -- (an object with a Shader object is drawn with it)
        local ok, used = pcall(useShader, block.shader)
        if ok and used then shadedObjects[name] = true end
    end
    if flashObjects[name] then
        local a = anims[name]
        if a and not a.placed and env.physicsToScreenTransform then     -- (an animation the scripts do not place follows its object)
            local o = env.objects and env.objects.world and env.objects.world[name]
            if o then
                a.tx, a.ty = env.physicsToScreenTransform(o.x, o.y)
                local sx, sy = 1, 1
                if env.getScale then sx, sy = env.getScale(name) sx, sy = sx or 1, sy or sx or 1 end
                local k = env.worldScale or 1
                a.sx, a.sy, a.rot = sx * k, sy * k, (env.getAngle and env.getAngle(name)) or 0
            end
        end
        if a then W.draw(name, 1) end
        return false
    end
    if previousDrawObject then return previousDrawObject(name, ...) end
end

-- The game's shaders (data/shaders: 2d-sprite-diffuse-modulate, 2d-sprite-colorize, 2d-sprite-gold, all pixelShaders/pp.ps) as one engine shader. The scripts'
-- Shader objects ({name, params = {{type, name, value}}}) set DIFFUSEC (a vector: the exe reads four numbers from the table, a missing fourth is 0, so
-- {1, 0.25, 0.25} has alpha 0 and is added to the picture by the premultiplied blending), LIGHTNESS, SATURATION and HIGHLIGHT. ALPHA_FACTOR is the alpha
-- of the scripts' render state. u_param0 = DIFFUSEC, u_param1 = LIGHTNESS, SATURATION, HIGHLIGHT, which shader (0 modulate, 1 colorize, 2 gold)
local SHADER_SRC = [[
vec4 shade(vec4 texel, vec2 uv, vec4 tint) {
    vec4 color = texel;
    vec4 diffuse = u_param0;
    float lightness = u_param1.x, saturation = u_param1.y, highlight = u_param1.z;
    float g = (color.r + color.g + color.b) * 0.333;
    if (u_param1.w < 0.5) {
        color *= diffuse;
    } else if (u_param1.w < 1.5) {
        vec4 grayscaleVec = vec4(g, g, g, color.a);
        float l = lightness * color.a;
        color = mix(grayscaleVec, color, saturation) * diffuse;
        color = vec4(color.r + l, color.g + l, color.b + l, color.a);
    } else {
        float h = highlight * g * g;
        g = 1.0 - g;
        g = 1.0 - g * g + lightness * color.a;
        color = vec4(g, g, g, color.a) * diffuse + h;
    }
    return color * tint.a;
}
]]
local shaderMode = {["2d-sprite-diffuse-modulate"] = 0, ["2d-sprite-colorize"] = 1, ["2d-sprite-gold"] = 2}
local shaderReady
-- useShader(shaderObject): the engine's shader is switched on with the object's parameters; false when it is not one of those shaders
function useShader(sh)
    if type(sh) ~= "table" or not nativeSetShader then return false end
    local mode = shaderMode[sh.name]
    if mode == nil then return false end
    if shaderReady == nil then
        local ok = nativeCreateShader and nativeCreateShader("stella-sprite", SHADER_SRC)
        shaderReady = ok and true or false
    end
    if not shaderReady then return false end
    local d, light, sat, hl = {1, 1, 1, 1}, 0, 0, 0
    for _, prm in ipairs(type(sh.params) == "table" and sh.params or {}) do
        local v = prm.value
        if prm.name == "DIFFUSEC" and type(v) == "table" then d = {tonumber(v[1]) or 0, tonumber(v[2]) or 0, tonumber(v[3]) or 0, tonumber(v[4]) or 0}
        elseif prm.name == "LIGHTNESS" then light = tonumber(v) or 0
        elseif prm.name == "SATURATION" then sat = tonumber(v) or 0
        elseif prm.name == "HIGHLIGHT" then hl = tonumber(v) or 0 end
    end
    nativeShaderParam("stella-sprite", 0, d[1], d[2], d[3], d[4])
    nativeShaderParam("stella-sprite", 1, light, sat, hl, mode)
    nativeSetShader("stella-sprite")
    return true
end
-- drawSpriteWithShader(sprite, shader, x, y, scaleX, scaleY, angle) / drawSpriteWithoutShader(sprite, x, y, scaleX, scaleY, angle): the sprite's
-- pivot at (x, y); the shader (the greyed-out look of a disabled button) is left out
local function drawScaled(sprite, x, y, sx, sy, angle, shader)
    if not sprite then return end
    sx, sy = sx or 1, sy or sx or 1
    if sx == 0 or sy == 0 then return end
    local px, py = 0, 0
    if res.getSpritePivot then px, py = res.getSpritePivot(sprite) px, py = px or 0, py or 0 end
    nativeSetRenderState(0, 0, sx, sy, angle or 0, px, py, scriptAlpha())
    local shaded = shader ~= nil and useShader(shader)
    res.drawSprite(sprite, (x or 0) / sx - px, (y or 0) / sy - py, "TOP", "LEFT")
    if shaded then nativeSetShader("none") end
    restoreRenderState()
end
env.drawSpriteWithShader = function(sprite, shader, x, y, sx, sy, angle) drawScaled(sprite, x, y, sx, sy, angle, shader) end
def("drawSpriteWithoutShader", function(sprite, x, y, sx, sy, angle) drawScaled(sprite, x, y, sx, sy, angle) end)
def("deviceManufacturer", "Apple")
def("deviceInfoModel", "iPhone")

-- v3.2: leaving a level (Next after level 3, a level picked on the map) could call GameScene:onExit a second time. The first call
-- removes the scene's listeners and drops the slingshot; the second found neither (an EventManager "nonexistant listener" error for
-- each event) and then stopped on the missing slingshot, so the scene change was tried again every frame and the game hung. A scene
-- that has already left does nothing on a second onExit; onEntry makes it leavable again.
local function guardScene(scene)
    if type(scene) ~= "table" or rawget(scene, "__abGuarded") then return end
    local onExit, onEntry = rawget(scene, "onExit"), rawget(scene, "onEntry")
    if type(onExit) ~= "function" or type(onEntry) ~= "function" then return end
    scene.__abGuarded = true
    local entries = 0
    scene.onEntry = function(self, ...)
        entries = entries + 1
        if env.abGuardMenu then env.abGuardMenu() end      -- (the menu manager is loaded after the first call at start-up, so it is guarded here, when the first scene is entered)
        if os.getenv("AB_SCENEDBG") and entries <= 6 then print("GameScene:onEntry #" .. entries .. " (left before: " .. tostring(rawget(self, "__abLeft")) .. ")\n" .. debug.traceback()) end
        rawset(self, "__abLeft", nil)
        return onEntry(self, ...)
    end
    scene.onExit = function(self, ...)
        if rawget(self, "__abLeft") then return end
        rawset(self, "__abLeft", true)
        return onExit(self, ...)
    end
end
guardScene(rawget(env, "GameScene"))
-- v3.9: the camera keeps the view inside the level's limits, which the level sets when it is loaded. A resolutionChanged before that (the window
-- resized while a menu or the loading screen is up) stopped with "attempt to compare number with nil" in forceScreenLimits, so with no limits yet it leaves the view alone
local function guardCamera(cam)
    if type(cam) ~= "table" or rawget(cam, "__abGuarded") then return end
    local force = rawget(cam, "forceScreenLimits")
    if type(force) ~= "function" then return end
    cam.__abGuarded = true
    cam.forceScreenLimits = function(self, ...)
        local l = self.levelLimits
        if type(l) ~= "table" or l.minScale == nil or l.left == nil or l.right == nil or l.top == nil or l.bottom == nil then return end
        return force(self, ...)
    end
end
-- MenuManager:update calls setRoot(queudRoot) and clears queudRoot only afterwards. While the scene loads (its onEntry loads the level and the theme) another update can
-- run, or onEntry can stop on a script error: the scene was then left and entered again, over and over (hundreds of "setupFrameElements" lines and "same listener twice"
-- for every listener of the scene). The scene is now taken off the queue before it is entered, and an error is printed once and the scene counts as entered.
local function guardMenuManager(mm)
    if type(mm) ~= "table" or rawget(mm, "__abGuarded") then return end
    local setRoot = rawget(mm, "setRoot")
    if type(setRoot) ~= "function" then return end
    mm.__abGuarded = true
    local entering = false
    mm.setRoot = function(self, root, ...)
        if entering then            -- a scene entry that starts while another is still loading is dropped (it is what made the scene load again and again)
            if root ~= nil and rawget(self, "queudRoot") == root then self.queudRoot = nil end
            return
        end
        if root ~= nil and rawget(self, "queudRoot") == root then self.queudRoot = nil end      -- (taken off the queue first: a frame that runs while the scene loads must not enter it again)
        entering = true
        local ok, err = pcall(setRoot, self, root, ...)
        entering = false
        if not ok then print("scene entry stopped (the scene is kept): " .. tostring(err)) end
    end
    local upd = rawget(mm, "update")
    if type(upd) == "function" then
        local inUpdate = false
        mm.update = function(self, ...)
            if inUpdate or entering then return end      -- no nested update while the menu manager is already updating / loading a scene
            inUpdate = true
            local ok, err = pcall(upd, self, ...)
            inUpdate = false
            if not ok then print("menu update: " .. tostring(err)) end
        end
    end
end
-- AB_LISTENERDBG=1: every "same listener twice" prints which event, which listener and the Lua call stack (the first 8), to find who adds a listener again
local function watchListeners(em)
    if type(em) ~= "table" or rawget(em, "__abWatched") or not os.getenv("AB_LISTENERDBG") then return end
    local add = rawget(em, "addEventListener")
    if type(add) ~= "function" then return end
    em.__abWatched = true
    local shown = 0
    em.addEventListener = function(self, id, listener, ...)
        local lists = type(self) == "table" and self.eventListeners
        local list = type(lists) == "table" and lists[id]
        if type(list) == "table" and shown < 8 then
            for _, x in pairs(list) do
                if x == listener then
                    shown = shown + 1
                    local names = {}
                    for k, v in pairs(env.events or {}) do if v == id then names[#names + 1] = tostring(k) end end
                    print("DUPLICATE LISTENER for " .. table.concat(names, "/") .. " (" .. tostring(id) .. "), listener " .. tostring(type(listener) == "table" and (listener.name or listener.objectName) or listener))
                    local dbg = rawget(_G, "debug")
                    print(dbg and dbg.traceback and dbg.traceback() or "(no stack available)")
                    break
                end
            end
        end
        return add(self, id, listener, ...)
    end
end
-- Bird skins come from the Telepod configuration, which is empty here: a skin saved in the save file (chosen or unlocked) has no entry in it, and the level
-- build (BirdAnimation, the extra bird bar of the HUD) indexes that entry. So a skin without an entry counts as not chosen and not unlocked.
local function guardSkins(sw)
    if type(sw) ~= "table" or rawget(sw, "__abSkins") then return end
    local active, unlocked = rawget(sw, "getActiveBirdSkin"), rawget(sw, "isBirdSkinUnlocked")
    if type(active) ~= "function" or type(unlocked) ~= "function" then return end
    sw.__abSkins = true
    local function known(skin) local c = env.g_telepodConfiguration return type(c) == "table" and c[skin] ~= nil end
    sw.getActiveBirdSkin = function(self, ...)
        local r = active(self, ...)
        if r ~= nil and not known(r) then return nil end
        return r
    end
    sw.isBirdSkinUnlocked = function(self, skin, ...)
        if skin ~= nil and not known(skin) then return false end
        return unlocked(self, skin, ...)
    end
end
env.abGuardMenu = function() local m = env.MenuManager guardMenuManager(type(m) == "table" and m.MenuManager or nil) guardSkins(env.SettingsWrapper) end
env.abGuardStellaScene = function() guardScene(rawget(env, "GameScene")) guardCamera(env.Camera) env.abGuardMenu() watchListeners(env.EventManager) end
