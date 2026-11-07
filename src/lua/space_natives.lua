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
ifMissing("setStartingCameraValue", function(v) env.g_startingCameraValue = v end)

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

-- TODO (Space bring-up): placeholders that keep the scripts running; each needs the exe's behaviour
for _, n in ipairs({"populateAimingAid",
                    "createLineShape",
                    "drawForeground", "renderMaskedImageNative",
                    "linkSensor",
                    "drawSelectedTexturizedObject", "loadPreviousLevel",
                   
                    "setFontPlaceholder", "getFontPlaceholder", "renderGravityVisualsNative"}) do ifMissing(n, function() end) end
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
                    "native_setZOrderRange", "native_setKeepOrientation", "native_setControlled", "native_setIgnoresScore", "native_setTimeSinceCollision",
                    "native_setBlockCollisionEnabled", "native_setAdditionalBirdGravity", "native_setBirdWaterDrag", "native_setObjectWaterDrag", "native_setWaterColor",
                    "native_resizeRadius", "native_setAffectedByForceMultipliers"}) do ifMissing(n, function() end) end
ifMissing("native_isPaymentInitialized", function() return false end)
ifMissing("native_isCameraSupported", function() return false end)
ifMissing("native_hasFrontCamera", function() return false end)
ifMissing("native_getDeviceOrientation", function() return 0 end)
ifMissing("native_getLevelLimits", function() return 0, 0, 0, 0 end)
-- native_setIsWater / native_setWaterDensity(name, ...) are used as wrappers around createObject: they hand the object's name back
ifMissing("native_setIsWater", function(name) return name end)
ifMissing("native_setWaterDensity", function(name) return name end)

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
    -- the value of a track at a time: linear between keyframes, the first / last value outside them, the latest keyframe for sprites
    local function sample(track, time)
        local k = track.keyframes
        if #k == 0 then return nil end
        if time <= k[1][1] then return k[1][2] end
        for i = 2, #k do
            if time < k[i][1] then
                if track.type == "DiscreteSprite" then return k[i - 1][2] end
                local a, b = k[i - 1], k[i]
                local f = (time - a[1]) / (b[1] - a[1])
                if type(a[2]) == "table" then return {a[2][1] + (b[2][1] - a[2][1]) * f, a[2][2] + (b[2][2] - a[2][2]) * f} end
                return a[2] + (b[2] - a[2]) * f
            end
        end
        return k[#k][2]
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
        c.time, c.playing, c.mode = 0, true, mode or "once"
    end
    env.cutsceneClose = function(tag) cuts[tag] = nil end
    env.cutsceneSetTranslation = function(tag, x, y) local c = cuts[tag] if c then c.x, c.y = x, y end end
    env.cutsceneSetScale = function(tag, sx, sy) local c = cuts[tag] if c then c.sx, c.sy = sx, sy or sx end end
    env.cutsceneSetRotation = function(tag, a) local c = cuts[tag] if c then c.angle = a end end
    env.updateCutscene = function(dt)
        for _, c in pairs(cuts) do
            if c.playing then
                c.time = c.time + dt
                if c.time > c.duration then
                    if c.mode == "repeat" and c.duration > 0 then c.time = c.time % c.duration else c.time = c.duration end
                end
            end
        end
    end
    local function drawNode(c, n, px, py, psx, psy, pang)
        local t = n.tracks
        local pos = t.translation and sample(t.translation, c.time) or {0, 0}
        local sc = t.scale and sample(t.scale, c.time) or {1, 1}
        local rot = t.rotation and sample(t.rotation, c.time) or 0
        local ca, sa = math.cos(pang), math.sin(pang)
        local x = px + (pos[1] * psx) * ca - (pos[2] * psy) * sa
        local y = py + (pos[1] * psx) * sa + (pos[2] * psy) * ca
        local sx, sy, ang = psx * sc[1], psy * sc[2], pang + rot
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
