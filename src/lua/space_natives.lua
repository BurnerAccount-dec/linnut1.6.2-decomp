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
env.savePersistentLuaFile = function() end
env.storePersistentData = function() end
