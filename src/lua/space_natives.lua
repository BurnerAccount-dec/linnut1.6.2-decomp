-- Angry Birds Space natives that are plain maths or logging; run once into the script environment (the C++ ones are in lua_api.cpp)
local env = ...
function env.performBitwiseOr(...)
    local r = 0
    for _, v in ipairs({...}) do
        local out, bit = 0, 1
        local a, b = r, v
        while a > 0 or b > 0 do
            if a % 2 == 1 or b % 2 == 1 then out = out + bit end
            a = math.floor(a / 2); b = math.floor(b / 2); bit = bit * 2
        end
        r = out
    end
    return r
end
function env.dotProduct(x1, y1, x2, y2) return x1 * x2 + y1 * y2 end
function env.normalizeVector(x, y)
    local l = math.sqrt(x * x + y * y)
    if l == 0 then return {x = 0, y = 0} end
    return {x = x / l, y = y / l}
end
function env.transformCoordinates(angle, x, y)
    local c, s = math.cos(angle), math.sin(angle)
    return {x = x * c - y * s, y = x * s + y * c}
end
function env.loginfo(...) end
function env.logwarning(...) end
function env.logerror(...) end
env.isHDVersion = nil   -- Space asserts that the engine does not define it and then defines it itself
-- The DRM (pc_drm.lua) asks for a registered key and a matching device id; the engine runs the owner's own copy, so the id always matches
env.uniqueDeviceId = "angrybirdsengine"
function env.getDeviceID() return env.uniqueDeviceId end
function env.verifyDeviceID() return true end
function env.registerKey() end
local function secs(t)
    if type(t) == "number" then return t end
    if type(t) ~= "table" or not t.year then return 0 end
    return os.time({year = t.year, month = t.month, day = t.day, hour = t.hour, min = t.min, sec = t.sec})
end
function env.getTimeDifferenceInSeconds(a, b) return secs(a) - secs(b) end
function env.GetDate() return math.floor(os.time() / 3600) end      -- hours since 1970 (the daily eagle compares hours)
function env.checkInstalledAppsOnline() end
function env.checkInstalledAppsOffline() end
function env.exportLuaTableAsJSON() return "[]" end
-- analytics: nothing is sent
for _, n in ipairs({"logFlurryTimerEvent", "logFlurryEvent", "logFlurryEventWithParam", "logFlurryEventWithParams"}) do env[n] = function() end end
function env.resetTimer() end   -- zeroes the frame timer after a long load
-- TODO natives found missing while bringing Space up: stubbed so the scripts keep running, each needs a real implementation
env.setChannelCountLimit = function() end
env.storePersistentData = function() end

-- ---- Angry Birds Space natives that only matter to a device or to the editor: harmless answers (each is also registered in the exe)
local function ifMissing(name, fn) if env[name] == nil then env[name] = fn end end
for _, n in ipairs({"createAudioOutput", "createAudioInput", "startAudioOutput", "stopAudioOutput", "startAudioInput",
                    "stopAudioInput", "setMasterVolume", "setTrackVolume", "openBarnesAndNobleShop", "drawRubberband", "checkMainMenuAd",
                    "iapSetItemDescription", "iapRestoreItems", "native_startURLThread", "removeAllNotifications", "loadBlocksForEditing",
                    "hideExpandableAd", "activateGameOverlayToStore", "setStat", "setStatFloat", "releaseFont",
                    "releaseTextGroupSet", "releaseCompoSpriteSet", "releaseSpriteSheet", "setPath", "captureSprite", "setMenuParticlesScale",
                    "setThemeOffsetY", "ClearSimulationTrajectory", "setSelectedBirdDuringSimulation",
                    "updateBirdTrajectoryTable",
                    }) do ifMissing(n, function() end) end
-- setRevoluteJointSpeed(jointName, speed) (0x465f50): the motor speed of every joint with that name
-- natives of the Star Wars exe that no shipped script calls (0x40d880 / 0x40d8a0 keep the rendering flag, 0x421c50 is the toy-code checksum for a server that is gone)
local renderingDisabled = false
ifMissing("setGameRenderingDisabled", function(b) renderingDisabled = b and true or false end)
ifMissing("isGameRenderingDisabled", function() return renderingDisabled end)
ifMissing("getUnlockRequestChecksum", function() return "" end)
for _, n in ipairs({"toggleZoomSteps", "setPhysicsScale", "setFlashAnimation", "updateThemeParticlesNative", "loadLevelFromAppData",
                    "loadLuaFileFromAppDataToObject", "native_loadTextFileToLuaTable"}) do ifMissing(n, function() end) end
ifMissing("setRevoluteJointSpeed", function(name, speed) env.setJointParameters({name = name, motorSpeed = speed}) end)
-- the drawing natives live in the res table; Space scripts call them as globals
ifMissing("drawTexturedLine2D", function(...) return env.res.drawTexturedLine2D(...) end)
ifMissing("drawCompoSprite", function(...) return env.res.drawCompoSprite(...) end)
-- setDecorationObjects(name) (0x479fb0): the object keeps its block definition's `decorations` (sprite, amount, angleIncrement, scale) and draws them about itself
ifMissing("setDecorationObjects", function(name)
    local o = env.objects.world[name]
    local d = o and env.blockTable.blocks[o.definition] and env.blockTable.blocks[o.definition].decorations
    if d then env.setObjectDecoration(name, d.sprite, d.amount or 0, d.angleIncrement or 0, d.scale or 1) end
end)
ifMissing("getTrackVolume", function() return 1 end)
ifMissing("iapIsAllowed", function() return false end)
ifMissing("getLocale", function() return "en" end)
ifMissing("loadLocale", function() return true end)
ifMissing("useLocale", function() return true end)
ifMissing("getString", function(key) return key end)
ifMissing("stopAllAudio", function() end)
ifMissing("decodeBase64", function(s) return s end)
ifMissing("getJSONHash", function() return "0" end)
ifMissing("GetJSONDiff", function() return "[]" end)
ifMissing("importJSONToLuaTable", function() return {} end)
ifMissing("performBitwiseAnd", function(a, b)
    local out, bit = 0, 1
    while a > 0 and b > 0 do
        if a % 2 == 1 and b % 2 == 1 then out = out + bit end
        a = math.floor(a / 2); b = math.floor(b / 2); bit = bit * 2
    end
    return out
end)
ifMissing("getGravityForceMultiplier", function() return env.g_gravityForceMultiplier or 1 end)
ifMissing("setGravityForceMultiplier", function(v) env.g_gravityForceMultiplier = v end)
ifMissing("getDeltaTimeMultiplier", function() return env.g_deltaTimeMultiplier or 1 end)
ifMissing("setDeltaTimeMultiplier", function(v) env.g_deltaTimeMultiplier = v end)
-- builds that call setCurrentLevelPack(name) keep the pack name in g_currentLevelPack (read back by their own scripts)
ifMissing("setCurrentLevelPack", function(p) env.g_currentLevelPack = p end)
ifMissing("getCurrentLevelPack", function() return env.g_currentLevelPack end)
-- getAimingTime (0x462b80): simulationIterations (a whole number) times objects.currentTimeStep (0.0285714 when the script has not set one): how long the simulated flight lasts
ifMissing("getAimingTime", function()
    local step = env.objects and env.objects.currentTimeStep or 0.028571429
    return math.floor(env.simulationIterations or 0) * step
end)
-- camera: the scripts drive the view with setWorldScale / setTopLeft; the limits and the starting value are remembered for them
ifMissing("setCameraLimits", function(w) env.g_cameraLimitWidth = w end)

-- removeBreakableJointsFromObject (0x4804b0): every breakable joint that has the object at one end is destroyed
ifMissing("removeBreakableJointsFromObject", function(name)
    local joints = env.objects and env.objects.joints
    if not joints then return end
    local doomed = {}
    for jointName, joint in pairs(joints) do
        if type(joint) == "table" and joint.breakable and (joint.end1 == name or joint.end2 == name) then doomed[#doomed + 1] = jointName end
    end
    for _, jointName in ipairs(doomed) do env.destroyJoint(jointName) end
end)
-- addGravitySensor (0x492860): the exe appends the eight values (position, size, flag ...) to a list the gravity visuals walk; the list is kept in env.gravitySensorList
ifMissing("addGravitySensor", function(...)
    env.gravitySensorList = env.gravitySensorList or {}
    env.gravitySensorList[#env.gravitySensorList + 1] = {...}
end)


-- drawString3D(group, text, x, y, z, tilt, turn, depth, alpha): one line of the Star Wars credits crawl. The text lies on a plane tilted back by `tilt`
-- (-45 degrees), `depth` runs along the plane (the line's yPosition: larger is nearer the viewer), and the line shrinks and fades towards the far end.
ifMissing("drawString3D", function(group, text, x, y, z, tilt, turn, depth, alpha)
    local w, h = env.screenWidth, env.screenHeight
    local c, sn = _G.math.cos(tilt), _G.math.sin(-tilt)
    local zz = z - depth * c
    if zz < 1 then return end
    local s = h * 0.55 / zz
    local a = _G.math.max(0, _G.math.min(1, alpha or 1))
    if a <= 0 then return end
    local sy = s * c
    env.setRenderState(0, 0, s, sy, 0, 0, 0, a)
    env.res.drawString(group, text, (w * 0.5 + x * s) / s, (h * 0.5 + (y + depth * sn) * s) / sy, "LEFT", "VCENTER")
    env.setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
end)


-- renderGravityVisualsNative(sensor, screenX, screenY, zoom) (0x415f90): the rings (circle sensors) and bars (box sensors) that the script's gravityVisuals list
-- keeps for a gravitation sensor. A circle entry's sprite is drawn eight times round the sensor (four turns of a quarter, then four more shifted by an eighth),
-- scaled by entry.scale * radius * 3 * zoom * 0.019; a box entry's sprite is drawn along the sensor's length at entry.pos (0..1), turned a quarter past the sensor's angle.
-- (The game's own script passes no zoom, which reads as 0 in the exe, so the retail game draws nothing; a mod that passes the zoom gets the rings.)
-- Every sprite is turned about its own bottom corner (the pivot is the sprite size) and drawn with the pivot anchors (VPIVOT, HPIVOT).
ifMissing("renderGravityVisualsNative", function(sen, sx, sy, zoom)
    zoom = zoom or 0   -- the retail script passes only three arguments, and the exe's number reader gives 0 for the missing fourth: scaled to nothing, so nothing is drawn
    if type(sen) ~= "table" or sen.sensorType == nil or sen.addVisualTimer == nil or not sen.active or sen.definition == nil then return end
    local def = env.blockTable and env.blockTable.blocks and env.blockTable.blocks[sen.definition]
    local vis = sen.gravityVisuals
    if not def or type(vis) ~= "table" then return end
    local pi = _G.math.pi
    if def.type == "circle" then
        local unit = (sen.radius or 0) * 3.0 * zoom * 0.019
        for _, e in _G.ipairs(vis) do
            local s = (e.scale or 1) * unit
            if s ~= 0 then
                local w, h = env.res.getSpriteBounds(e.sprite)
                for pass = 0, 1 do
                    for i = 1, 4 do
                        env.setRenderState(sx / s, sy / s, s, s, (i - 1) * pi * 0.5 + pass * pi * 0.25, w, h)
                        env.res.drawSprite(e.sprite, e.x or 0, e.y or 0, "VPIVOT", "HPIVOT")
                    end
                end
            end
        end
    elseif def.type == "box" and sen.width and sen.height then
        local hw, hh = sen.width * 20 * 0.5, sen.height * 20 * 0.5
        local pw, ph = env.res.getSpriteBounds("THEME_1_GRAVITY_SLICE_BOX_FADED")
        if not pw or pw == 0 then return end
        local u = hw * 2 / pw
        local a = sen.angle or 0
        local cs, sn = _G.math.cos(a), _G.math.sin(a)
        local s = u * zoom
        if s == 0 then return end
        env.setRenderState(sx / s, sy / s, s, s, pi * 0.5 + a, pw, ph)
        for _, e in _G.ipairs(vis) do
            local p = e.pos or 0
            env.res.drawSprite(e.sprite, (p * 2 * hh * sn - hh * sn) / u, (-p * 2 * hh * cs + hh * cs) / u, "VPIVOT", "HPIVOT")
        end
    end
end)

-- TODO (Space bring-up): placeholders that keep the scripts running; each needs the exe's behaviour
for _, n in ipairs({"drawForeground", "renderMaskedImageNative",
                    "linkSensor",
                    "drawSelectedTexturizedObject", "loadPreviousLevel",
                   
                    "setFontPlaceholder", "getFontPlaceholder"}) do ifMissing(n, function() end) end
for _, n in ipairs({"getFontHeight", "getFontMaxAscending", "getFontMaxDescending", "getFontLeading", "getFontTracking", "getStringWidth"}) do
    ifMissing(n, function() return 0 end)
end
ifMissing("useFont", function() end)
-- setScale(name, sx, sy): the object's sprite is drawn scaled (setObjectParameter 5 sets the scale, 6 turns scaled drawing on); planets rely on it
ifMissing("setScale", function(name, sx, sy)
    if env.setObjectParameter then env.setObjectParameter(name, 17, sx or 1); env.setObjectParameter(name, 18, sy or sx or 1) end
end)
ifMissing("setVisible", function(name, visible)
    if env.setObjectAlpha then env.setObjectAlpha(name, visible and 1 or 0) end
end)

-- Star Wars: the resource natives sit in a ResourceManager table as native_<name>; each one is the res.<name> function of the earlier games
if env.ResourceManager == nil then
    local RM = setmetatable({}, {__index = function(t, key)
        local base = type(key) == "string" and key:match("^native_(.+)$")
        local f = base and env.res and env.res[base]
        if f then return f end
        if base then return function() end end   -- natives that only matter to a device: harmless no-ops
    end})
    env.ResourceManager = RM
    _G.ResourceManager = RM   -- scripts reach it as _G.ResourceManager
end

-- Star Wars natives (the exe registers them as native_<name>; the scripts call them bare or through gamelua.)
ifMissing("native_setAlpha", function(a) if env.setAlpha then env.setAlpha(a) end end)
-- the three particle layers: behind the blocks, in front of the birds, notifications over the menus
ifMissing("native_drawBackgroundParticles", function() env.drawParticlesFiltered(1) end)
ifMissing("native_drawForegroundParticles", function() env.drawParticlesFiltered(2) end)
ifMissing("native_drawNotificationParticles", function() env.drawParticlesFiltered(3) end)
-- setMass(name, density): the density of every fixture of the body
ifMissing("native_setMass", function(name, density) env.setObjectDensity(name, density) end)
for _, n in ipairs({"native_drawSimulationTrajectory", "native_refreshThemeSystem",
                    "native_resetThemeSystem", "native_onLevelInitComplete", "native_initPaymentProvider", "native_startURLThread", "native_startQrScanner",
                    "native_stopQrScanner", "native_setQrRecognizedCallback", "native_iapRestorePurchases", "native_setThemeLayerOffset", "native_setThemeLayerScale",
                    "native_setZOrderRange",
                    "native_setAdditionalBirdGravity", "native_setWaterColor",
                    "native_resizeRadius"}) do ifMissing(n, function() end) end
ifMissing("native_setBirdWaterDrag", function(v) env.setBirdWaterDrag(v) end)
ifMissing("native_setObjectWaterDrag", function(v) env.setObjectWaterDrag(v) end)
ifMissing("native_isPaymentInitialized", function() return false end)
ifMissing("native_isCameraSupported", function() return false end)
ifMissing("native_hasFrontCamera", function() return false end)
ifMissing("native_getDeviceOrientation", function() return 0 end)
ifMissing("native_getLevelLimits", function() return 0, 0, 0, 0 end)
-- the setters of single flags on the object record (0x4290d0, 0x428290, 0x428050, 0x427fe0, 0x427f70, 0x427e90, 0x427f00, 0x4280c0): the object record
-- keeps them, 200 and up are the engine's own setObjectParameter ids
local function flagNative(nativeName, id)
    ifMissing(nativeName, function(name, on) env.setObjectParameter(name, id, (on == true or on == 1) and 1 or 0) end)
end
flagNative("native_setBlockCollisionEnabled", 200)
flagNative("native_setIgnoresScore", 201)
flagNative("native_setKeepOrientation", 202)
flagNative("native_setAffectedByForceMultipliers", 203)
flagNative("native_setIsWater", 204)
flagNative("native_setControlled", 206)
ifMissing("native_setWaterDensity", function(name, density) env.setObjectParameter(name, 205, density or 0) end)
ifMissing("native_setTimeSinceCollision", function(name, t) env.setObjectParameter(name, 207, t or 0) end)

-- res.createCompositeAudio(name, {clip names}): a sound made of other sounds that play one after the other
-- (Star Wars: the title theme, then the menu music). Without it startInitialLoading failed in createAudioAssets and restarted every frame.
if env.res and env.res.createCompositeAudio == nil and env.res.playAudio then
    local res, composites, running = env.res, {}, {}
    local playAudio, stopAudio, stopAll = res.playAudio, res.stopAudio, res.stopAllAudio
    res.createCompositeAudio = function(name, clips) composites[name] = clips end
    res.playAudio = function(name, ...)
        local clips = composites[name]
        if not clips then return playAudio(name, ...) end
        running[name] = {index = 1, args = {...}}
        return playAudio(clips[1], ...)
    end
    res.stopAudio = function(name, ...)
        local clips = composites[name]
        if clips then running[name] = nil; for _, c in ipairs(clips) do stopAudio(c, ...) end return end
        return stopAudio(name, ...)
    end
    if stopAll then res.stopAllAudio = function(...) running = {} return stopAll(...) end end
    local isPlaying = res.isAudioPlaying
    if isPlaying then
        res.isAudioPlaying = function(name, ...)
            if composites[name] then return running[name] ~= nil end
            return isPlaying(name, ...)
        end
    end
    -- the next clip of every running composite starts when the one before it has ended
    local update = env.update
    env.update = function(...)
        for name, run in pairs(running) do
            local clips = composites[name]
            if not isPlaying(clips[run.index]) then
                run.index = run.index + 1
                if clips[run.index] then playAudio(clips[run.index], unpack(run.args)) else running[name] = nil end
            end
        end
        return update(...)
    end
end

-- social features are not available on a PC build
for _, n in ipairs({"isTwitterSupported", "isFacebookSupported", "isGameCenterSupported"}) do if env[n] == nil then env[n] = function() return false end end end

-- ---- Star Wars cutscenes: the "game::Animation" files (encrypted JSON) of the menus and the power-up tutorials
-- cutsceneLoad(tag, file), cutsceneStart(tag, action, mode "once" / "repeat"), cutsceneSetTranslation / Scale / Rotation(tag, ...), updateCutscene(dt),
-- drawCutscene(tag), cutsceneClose(tag). A clip is a list of targets named <parent>_<parent layer>_<name>_L<layer>; each target animates a translation,
-- scale, rotation and the sprite shown. Layer 0 is on top (Flash), and the children of a target are drawn over it.
if env.readDataFile then
    local cuts = {}
    local function parseJson(text)
        local pos = 1
        local function skip() pos = text:find("[^ \t\r\n]", pos) or #text + 1 end
        local value
        local function str()
            local out, i = {}, pos + 1
            while true do
                local c = text:sub(i, i)
                if c == '"' then pos = i + 1 return table.concat(out) end
                if c == "\\" then
                    local n = text:sub(i + 1, i + 1)
                    out[#out + 1] = ({n = "\n", t = "\t", r = "\r"})[n] or n
                    i = i + 2
                else out[#out + 1] = c i = i + 1 end
            end
        end
        function value()
            skip()
            local c = text:sub(pos, pos)
            if c == "{" then
                local t, order = {}, {}
                pos = pos + 1 skip()
                if text:sub(pos, pos) == "}" then pos = pos + 1 t.__order = order return t end
                while true do
                    skip()
                    local k = str() skip()
                    pos = pos + 1   -- the colon
                    t[k] = value() order[#order + 1] = k skip()
                    local d = text:sub(pos, pos) pos = pos + 1
                    if d == "}" then break end
                end
                t.__order = order
                return t
            elseif c == "[" then
                local t = {}
                pos = pos + 1 skip()
                if text:sub(pos, pos) == "]" then pos = pos + 1 return t end
                while true do
                    t[#t + 1] = value() skip()
                    local d = text:sub(pos, pos) pos = pos + 1
                    if d == "]" then break end
                end
                return t
            elseif c == '"' then return str()
            elseif text:sub(pos, pos + 3) == "true" then pos = pos + 4 return true
            elseif text:sub(pos, pos + 4) == "false" then pos = pos + 5 return false
            elseif text:sub(pos, pos + 3) == "null" then pos = pos + 4 return nil
            end
            local s, e = text:find("^-?[%d%.eE+-]+", pos)
            pos = e + 1
            return tonumber(text:sub(s, e))
        end
        return value()
    end
    -- the value of a track at a time: linear between keyframes, the first / last value outside them, the latest keyframe for sprites.
    -- Time only moves forward while a clip plays, so each track remembers the keyframe it was at and the search starts there (a clip of
    -- hundreds of keyframes per track used to be walked from the start for every target every frame). Returns the value, or two numbers
    -- (x, y) for a pair, without making a table.
    local function sample(track, time)
        local k = track.keyframes
        local n = #k
        if n == 0 then return nil end
        local i = track.cur or 2
        if i < 2 or i > n or time < k[i - 1][1] then i = 2 end
        if time <= k[1][1] then track.cur = 2 local v = k[1][2] if type(v) == "table" then return v[1], v[2] end return v end
        while i <= n and time >= k[i][1] do i = i + 1 end
        track.cur = i <= n and i or n
        if i > n then local v = k[n][2] if type(v) == "table" then return v[1], v[2] end return v end
        local a, b = k[i - 1], k[i]
        if track.type == "DiscreteSprite" then return a[2] end
        local f = (time - a[1]) / (b[1] - a[1])
        if type(a[2]) == "table" then return a[2][1] + (b[2][1] - a[2][1]) * f, a[2][2] + (b[2][2] - a[2][2]) * f end
        return a[2] + (b[2] - a[2]) * f
    end
    env.cutsceneLoad = function(tag, file)
        local text
        local base = file:match("([^/\\]+)$") or file
        for _, path in ipairs({file, file .. ".json", "animations/" .. file, "animations/" .. file .. ".json", "animations/Tutorial/" .. base, "animations/Tutorial/" .. base .. ".json"}) do
            text = env.readDataFile(path)
            if text then break end
        end
        local c = {tag = tag, time = 0, playing = false, x = 0, y = 0, sx = 1, sy = 1, angle = 0, mode = "once", nodes = {}, duration = 0}
        cuts[tag] = c
        if not text then env.print("cutsceneLoad: cannot read " .. file .. "\n") return end
        local ok, doc = pcall(parseJson, text)
        local data = ok and doc and doc.comps and doc.comps[1] and doc.comps[1].data
        c.actions = data and data.actions or {}
    end
    local function buildNodes(c, action)
        local clip = c.actions[action] and c.actions[action].clips and c.actions[action].clips[""]
        c.nodes, c.duration = {}, 0
        if not clip then return end
        local list = {}
        for i, key in ipairs(clip.targets.__order) do
            local layer = key:match("_L(%d+)$")
            local n = {key = key, tracks = clip.targets[key], children = {}}
            n.layer = tonumber(layer)
            list[#list + 1] = n
            for _, track in pairs(n.tracks) do
                if type(track) == "table" and track.keyframes then
                    local last = track.keyframes[#track.keyframes]
                    if last and last[1] > c.duration then c.duration = last[1] end
                end
            end
        end
        -- the parent is the target whose "<name>_<layer>_" the key starts with (the longest one); the rest hang from the first target
        for _, n in ipairs(list) do
            local best
            for _, p in ipairs(list) do
                if p ~= n then
                    local pname = p.key:match("^(.*)_L%d+$")
                    -- the key of a child starts with its parent's name, the parent's layer and an underscore; the name is a tail of the parent's own key
                    for s in pname:gmatch("()[^_]") do
                        local tail = pname:sub(s) .. "_" .. p.layer .. "_"
                        if n.key:sub(1, #tail) == tail and (not best or #tail > best.len) then best = {node = p, len = #tail} end
                    end
                end
            end
            n.parent = best and best.node or (n ~= list[1] and list[1] or nil)
            if n.parent then n.parent.children[#n.parent.children + 1] = n end
        end
        c.nodes = list
        c.root = list[1]
    end
    env.cutsceneStart = function(tag, action, mode)
        local c = cuts[tag]
        if not c or not c.actions then return end
        if not action or not c.actions[action] then action = next(c.actions) end
        buildNodes(c, action)
        c.time, c.playing, c.mode, c.finished = 0, true, mode or "once", false
    end
    env.cutsceneClose = function(tag) cuts[tag] = nil end
    env.cutsceneSetTranslation = function(tag, x, y) local c = cuts[tag] if c then c.x, c.y = x, y end end
    env.cutsceneSetScale = function(tag, sx, sy) local c = cuts[tag] if c then c.sx, c.sy = sx, sy or sx end end
    env.cutsceneSetRotation = function(tag, a) local c = cuts[tag] if c then c.angle = a end end
    env.updateCutscene = function(dt)
        for _, c in pairs(cuts) do
            if c.playing and not c.finished then
                c.time = c.time + dt
                if c.time > c.duration then
                    if c.mode == "repeat" and c.duration > 0 then c.time = c.time % c.duration else c.time, c.finished = c.duration, true end   -- a clip played once stays on its last picture and is not stepped again
                end
            end
        end
    end
    -- cutsceneStop(tag) (0x411500), cutsceneStopAll (0x40d350), cutsceneCloseAll (0x40d340), cutsceneSeek(tag, time) (0x411580)
    env.cutsceneStop = function(tag) local c = cuts[tag] if c then c.playing = false end end
    env.cutsceneStopAll = function() for _, c in pairs(cuts) do c.playing = false end end
    env.cutsceneCloseAll = function() for tag in pairs(cuts) do cuts[tag] = nil end end
    env.cutsceneSeek = function(tag, time) local c = cuts[tag] if c then c.time, c.finished = time or 0, false for _, n in ipairs(c.nodes) do for _, tr in pairs(n.tracks) do if type(tr) == "table" then tr.cur = nil end end end end end
    local function drawNode(c, n, px, py, psx, psy, pang)
        local t = n.tracks
        local pos1, pos2, sc1, sc2, rot = 0, 0, 1, 1, 0
        if t.translation then pos1, pos2 = sample(t.translation, c.time) if not pos1 then pos1, pos2 = 0, 0 end end
        if t.scale then sc1, sc2 = sample(t.scale, c.time) if not sc1 then sc1, sc2 = 1, 1 end end
        if t.rotation then rot = sample(t.rotation, c.time) or 0 end
        local ca, sa = math.cos(pang), math.sin(pang)
        local x = px + (pos1 * psx) * ca - (pos2 * psy) * sa
        local y = py + (pos1 * psx) * sa + (pos2 * psy) * ca
        local sx, sy, ang = psx * sc1, psy * (sc2 or sc1), pang + rot
        local sprite = t.sprite and sample(t.sprite, c.time)
        if sprite and sx ~= 0 and sy ~= 0 then
            env.setRenderState(0, 0, sx, sy, ang, 0, 0, 1)
            env.res.drawSprite(sprite, x / sx, y / sy)
        end
        for i = #n.children, 1, -1 do drawNode(c, n.children[i], x, y, sx, sy, ang) end
    end
    env.drawCutscene = function(tag)
        local c = cuts[tag]
        if not c or not c.root or not c.playing then return end
        drawNode(c, c.root, c.x, c.y, c.sx, c.sy, c.angle)
        env.setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
    end
end

-- Star Wars natives of a device or of the metro layout: the PC build answers the way a plain full-screen window does
ifMissing("getFontScale", function() return 1 end)
ifMissing("getUIScale", function() return 1 end)
ifMissing("getWindowState", function() return "FULL" end)
ifMissing("clearParticlesWithTagNative", function() env.clearParticles() end)
for _, n in ipairs({"tryUnsnap", "sendTweet", "setAccelerometerSensitivity", "setNotificationsEnabled", "getLeaderboardScoresAroundPlayer",
                    "getLeaderboardScoresForFriends"}) do ifMissing(n, function() end) end
if env.res then
    if env.res.getClipRect == nil then env.res.getClipRect = function() return 0, 0, env.screenWidth, env.screenHeight end end
    if env.res.createSystemFont == nil then env.res.createSystemFont = function() end end
end
