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
for _, n in ipairs({"createAudioOutput", "createAudioInput", "createCompositeAudio", "startAudioOutput", "stopAudioOutput", "startAudioInput",
                    "stopAudioInput", "setMasterVolume", "setTrackVolume", "openBarnesAndNobleShop", "drawRubberband", "checkMainMenuAd",
                    "iapSetItemDescription", "iapRestoreItems", "native_startURLThread", "removeAllNotifications", "loadBlocksForEditing",
                    "saveLevelForReload", "hideExpandableAd", "activateGameOverlayToStore", "setStat", "setStatFloat", "releaseFont",
                    "releaseTextGroupSet", "releaseCompoSpriteSet", "releaseSpriteSheet", "setPath", "captureSprite", "setMenuParticlesScale",
                    "setThemeOffsetY", "resetMouseWheelScale", "ClearSimulationTrajectory", "setSelectedBirdDuringSimulation",
                    "updateBirdTrajectoryTable"}) do ifMissing(n, function() end) end
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
ifMissing("getAimingTime", function() return 0 end)
-- camera: the scripts drive the view with setWorldScale / setTopLeft; the limits and the starting value are remembered for them
ifMissing("setCameraLimits", function(w) env.g_cameraLimitWidth = w end)
ifMissing("setStartingCameraValue", function(v) env.g_startingCameraValue = v end)

-- TODO (Space bring-up): placeholders that keep the scripts running; each needs the exe's behaviour
for _, n in ipairs({"populateAimingAid",
                    "createLineShape",
                    "drawForeground", "renderMaskedImageNative", "drawString3D",
                    "addGravitySensor", "linkSensor", "removeBreakableJointsFromObject",
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
