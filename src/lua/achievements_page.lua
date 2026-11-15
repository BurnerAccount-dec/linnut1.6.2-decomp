-- achievements_page.lua: Game Center achievements without Game Center (engine side, run once with the game's environment).
--
-- The phone exes read the achievement list of the device (scripts/ipad/achievements.lua, iphone/, gamecenter/iphone/ ...), reported
-- unlockAchievement(id, name) to Game Center (or Chillingo's Crystal in Angry Birds HD 1.4.2) and Game Center sent back each
-- achievement's title and "achieved" text, which the game's own pop-up (AchievementPopup / BannerPopup) shows. Here:
--   * the list is loaded when the scripts left it empty;
--   * every unlock is kept in settings.abAchievements (name = time) and shown with the game's own pop-up when it has one, otherwise
--     with a banner of the same make (title strip, icon, text);
--   * each achievement gets a short text saying how to get it, taken from the conditions the scripts check (the limits are read from
--     the running game where it keeps them: achievementLimits, the achievement processor, the episode list);
--   * F7, the game's Game Center button (showAchievements) and the Crystal button of Angry Birds HD 1.4.2 open a page listing them all.
local env = ...
local _G = _G
local math, string, table, pairs, ipairs, type, tostring, next, rawget, pcall = _G.math, _G.string, _G.table, _G.pairs, _G.ipairs, _G.type, _G.tostring, _G.next, _G.rawget, _G.pcall

if env.abAchPage then return end

-- v3.2: the Android data (Google Play's list is the iPhone's, or scripts/achievements.lua) and the Seasons 4.x PC data (its Xbox Live list) too
local phone = env.iOS or env.deviceModel == "iphone" or env.deviceModel == "ipad" or env.deviceModel == "android" or (env.deviceModel == "windows" and env.commonScriptPath ~= nil)
if phone and type(env.loadLuaFile) == "function" and env.fileExists and env.scriptPath and (type(env.achievements) ~= "table" or next(env.achievements) == nil) then
    local dm = tostring(env.deviceModel or "iphone")
    for _, f in ipairs({dm .. "/achievements.lua", "gamecenter/" .. dm .. "/achievements.lua", "ipad/achievements.lua", "iphone/achievements.lua", "gamecenter/iphone/achievements.lua", "gamecenter/ipad/achievements.lua", "xboxlive/achievements.lua", "achievements.lua"}) do
        local path = env.scriptPath .. "/" .. f
        if env.fileExists(path) then
            pcall(env.loadLuaFile, path, "")
            if type(env.achievements) == "table" and next(env.achievements) then break end
            if type(env.g_achievements) == "table" and next(env.g_achievements) then break end
        end
    end
    -- Angry Birds 2.0.0 (Android) keeps its list as g_achievements (name = {icon = ...})
    if (type(env.achievements) ~= "table" or next(env.achievements) == nil) and type(env.g_achievements) == "table" and next(env.g_achievements) then
        env.achievements = env.g_achievements
    end
end
if type(env.achievements) ~= "table" or next(env.achievements) == nil then return end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- how to get each one. {P:field|default} is a limit of the achievement processor (Classic / Seasons), {L:name|default} one of
-- achievementLimits (Space / Star Wars), {H:...} the same in hours (play time is counted in seconds)
local texts = {
    -- Classic (and the Classic list Friends 1.0.0 ships)
    ["Split it!"] = "Unlock the Blue Birds.",
    ["Speed is the Essence"] = "Unlock the Yellow Bird.",
    ["Boom Boom!"] = "Unlock the Black Bird.",
    ["Mother of all Bombs"] = "Unlock the White Bird.",
    ["Return to Sender"] = "Unlock the Boomerang Bird.",
    ["Seeing Red"] = "Unlock the Big Brother Bird.",
    ["Aquiline Benefactor"] = "Get the Mighty Eagle.",
    ["Backward Compatibility"] = "Shoot 10 birds backwards.",
    ["Space Invader"] = "Shoot a bird (not the red or black one) up into space.",
    ["Wilhelm Tell"] = "Hit a flying bird with another bird.",
    ["Bull's Eye"] = "Score a direct hit on a pig.",
    ["Cakemonger"] = "Collect the secret cake.",
    ["Woodpecker"] = "Smash {P:woodPeckerLimit|5000} wooden blocks.",
    ["Icepicker"] = "Smash {P:icePickerLimit|5000} ice blocks.",
    ["Ice Picker"] = "Smash {P:icePickerLimit|1000} ice blocks.",
    ["Stonecutter"] = "Smash {P:stoneCutterLimit|5000} stone blocks.",
    ["Stone Cutter"] = "Smash {P:stoneCutterLimit|1000} stone blocks.",
    ["Pig Popper"] = "Pop {P:pigPopperLimit|1000} pigs.",
    ["Block Smasher"] = "Smash {P:blockSmasherLimit|50000} blocks.",
    ["Smash Maniac"] = "Smash {P:smashManiacLimit|500000} blocks.",
    ["Ultimate Bejeweler"] = "Smash {P:bejewelerLimit|100} jewels.",
    ["Hard as a Rock"] = "Smash {P:hardAsRockLimit|100} stalactites.",
    ["Egg Hunter"] = "Find {P:eggHunterLimit|10} golden eggs.",
    ["Egg Cracker"] = "Earn {P:eggCrackerLimit|10} stars in golden egg levels.",
    ["Angry Birds Fan"] = "Play for {H:P:ABFanLimit|18000} hours.",
    ["True Angry Birds Fan"] = "Play for {H:P:trueABFanLimit|54000} hours.",
    ["Angry Birds Addict"] = "Play for {H:P:ABAddictLimit|108000} hours.",
    ["Star Collector"] = "Earn {P:starCollectorLimit|750} stars.",
    ["Star Gatherer"] = "Earn {P:starGathererLimit|1500} stars.",
    ["Bird Slinger"] = "Launch {P:birdSlingerLimit|5000} birds.",
    ["Feather Picker"] = "Earn {P:featherPickerLimit|50} Mighty Eagle feathers.",
    ["Feather Collector"] = "Earn {P:featherCollectorLimit|100} Mighty Eagle feathers.",
    ["Feather Gatherer"] = "Earn {P:featherGathererLimit|200} Mighty Eagle feathers.",
    ["Ready For More"] = "Complete every level of the free version.",
    -- the world-clear achievements of Classic's iPad list (Angry Birds HD): each is named after the boss pig of its world
    ["Herr Helmet"] = "Complete every level of world 1 (Poached Eggs 1-1 to 1-21).",
    ["Mr Moustache"] = "Complete every level of world 2 (Poached Eggs 2-1 to 2-21).",
    ["Defeat of The King"] = "Complete every level of world 3 (Poached Eggs 3-1 to 3-21) and beat King Pig.",
    ["The Imposter"] = "Complete every level of world 4 (Mighty Hoax).",
    ["The Mysterious Escape"] = "Complete every level of world 5 (Mighty Hoax).",
    ["Hovering Helmet"] = "Complete every level of world 6 (Danger Above).",
    ["Mounting Moustache"] = "Complete every level of world 7 (Danger Above).",
    ["Green Baron"] = "Complete every level of world 8 (Danger Above).",
    ["Hardhat Hidalgo"] = "Complete every level of world 9 (The Big Setup).",
    ["Mason Moustache"] = "Complete every level of world 10 (The Big Setup).",
    ["Royal Ringleader"] = "Complete every level of world 11 (The Big Setup).",
    -- Seasons
    ["Pumpkin Smasher"] = "Smash {P:pumpkinSmasherLimit|300} pumpkins.",
    ["Unwrapper"] = "Unwrap {P:unwrapperLimit|300} presents.",
    ["The Grinch"] = "Smash {P:grinchLimit|1000} presents.",
    ["Snowplower"] = "Smash {P:snowPlowerLimit|1500} snow blocks.",
    ["Chocolate Lover"] = "Smash {P:chocolateLoverLimit|300} chocolate boxes.",
    ["Cloud Buster"] = "Pop {P:cloudBusterLimit|1500} clouds.",
    ["Pennywise"] = "Smash {P:pennywiseLimit|300} piles of coins.",
    ["The Egg Man"] = "Smash {P:eggManLimit|300} Easter eggs.",
    ["Bunny Hunter"] = "Smash {P:bunnyHunterLimit|300} bunny ears.",
    ["Oops a Daisy"] = "Smash {P:oopsADaisyLimit|300} flowers.",
    ["Picnic Pooper"] = "Smash {P:picnicPooperLimit|300} picnic items.",
    ["Shut the Lights!"] = "Smash {P:shutTheLightsLimit|300} lanterns.",
    ["Bunny Hopper"] = "Smash {P:bunnyHopperLimit|300} bunny statues.",
    ["Leprechaun's Secret"] = "Smash {P:leprechaunsSecretLimit|300} pots of gold.",
    ["Lovely Bones"] = "Smash {P:lovelyBonesLimit|300} bones.",
    ["Cookie Crumbler"] = "Smash {P:cookieCrumblerLimit|300} cookies.",
    ["Black is the New Black"] = "Smash {P:blackIsTheNewBlackLimit|300} black pumpkins.",
    ["All Around the Globe"] = "Watch the tutorial of the globe-flying bird.",
    ["Loop-de-loop-de-loop"] = "Send a bird through portals {P:birdThroughPortalLimit|3} times in one shot.",
    -- Space
    ["Fowl Delivery"] = "Get the Space Eagle.",
    ["Cosmic Feather"] = "Earn a Space Eagle feather.",
    ["Where No Birds Have Gone Before"] = "Unlock an extra episode.",
    ["Vegetables of Vengeance"] = "Beat the boss of the first episode.",
    ["Handy Man"] = "Beat the boss of the second episode.",
    ["Birds of Courage"] = "Beat the last level of Danger Zone.",
    ["Cosmic Discovery"] = "Unlock an Eggsteroid level.",
    ["Eggsteroid Hunter"] = "Unlock {L:Eggsteroid Hunter|5} Eggsteroid levels.",
    ["Name Popper"] = "Pop every name bubble in the credits.",
    ["Centripetal Force"] = "Keep a bird inside two gravity fields at once for half a second.",
    ["Gravitational Escape"] = "Fly {L:Gravitational Escape|100} birds out of gravity fields they started outside.",
    ["Space Woodpecker"] = "Smash {L:Space Woodpecker|5000} wooden blocks.",
    ["Space Stonecutter"] = "Smash {L:Space Stonecutter|5000} stone blocks.",
    ["Space Icepicker"] = "Smash {L:Space Icepicker|5000} ice blocks.",
    ["Space Pig Popper"] = "Pop {L:Space Pig Popper|5000} pigs.",
    ["Space Block Smasher"] = "Smash {L:Space Block Smasher|50000} blocks.",
    ["Space Smash Maniac"] = "Smash {L:Space Smash Maniac|500000} blocks.",
    ["Bubble Popper"] = "Pop {L:Bubble Popper|1000} bubbles in the main menu.",
    ["Menu Popper"] = "Pop the bubbles in the main menu.",
    ["Space Bird Fan"] = "Play for {H:L:Space Bird Fan|18000} hours.",
    ["True Space Bird Fan"] = "Play for {H:L:True Space Bird Fan|54000} hours.",
    ["Space Bird Addict"] = "Play for {H:L:Space Bird Addict|108000} hours.",
    ["Starman"] = "Earn {L:Starman|250} stars.",
    ["Bird Launcher"] = "Launch {L:Bird Launcher|5000} birds.",
    ["Pigsicle"] = "Freeze {L:Pigsicle|500} pigs with the Ice Bird.",
    ["Three Hog Night"] = "Freeze {L:Three Hog Night|3} pigs with one Ice Bird.",
    ["Big Spook"] = "Push {L:Big Spook|6} objects with one shout of the big red bird.",
    ["Banging Heads"] = "Pop two pigs by knocking them into each other.",
    ["Professional Level"] = "Earn three stars on a level.",
    ["Absolute Zero"] = "Freeze the boss pig.",
    -- Star Wars
    ["Mighty Launch"] = "Call the Mighty Falcon.",
    ["Falcon Escape"] = "Get a full Mighty Falcon score on a level.",
    ["Hyper Space"] = "Get a full Mighty Falcon score {L:Hyper Space|50} times.",
    ["That's No Moon"] = "Beat the last level of Death Star.",
    ["Disturbing Misobedience"] = "Beat the Trench Run boss level.",
    ["Courage Of Jedi"] = "Beat the last level of Path of the Jedi.",
    ["Dangerous It Will Be"] = "Unlock Path of the Jedi.",
    ["Probe Explorer"] = "Unlock a droid level.",
    ["Probe Gatherer"] = "Unlock {L:Probe Gatherer|5} droid levels.",
    ["Jedi Initiate"] = "Destroy {L:Jedi Initiate|50} blocks with a lightsaber.",
    ["Jedi Knight"] = "Destroy {L:Jedi Knight|500} blocks with a lightsaber.",
    ["Jedi Master"] = "Destroy {L:Jedi Master|2000} blocks with a lightsaber.",
    ["Force Learner"] = "Throw {L:Force Learner|2000} blocks with the Force.",
    ["Ultimate Laser Shooter"] = "Destroy {L:Ultimate Laser Shooter|1000} blocks with blaster shots.",
    ["Skilled Pilot"] = "Destroy {L:Skilled Pilot|500} blocks with the Rebel pilots.",
    ["Magnificent Grumble"] = "Destroy {L:Magnificent Grumble|300} blocks with Chewbacca.",
    ["Prominent Jedi"] = "Deflect {L:Prominent Jedi|3} blaster shots with one lightsaber swing.",
    ["Target Shooter"] = "Hit {L:Target Shooter|3} pigs with one blaster shot.",
    ["Star Bird Fan"] = "Play for {H:L:Star Bird Fan|18000} hours.",
    ["True Star Bird Fan"] = "Play for {H:L:True Star Bird Fan|54000} hours.",
    ["Star Bird Addict"] = "Play for {H:L:Star Bird Addict|108000} hours.",
    ["Fly Bird Fly"] = "Launch {L:Fly Bird Fly|5000} birds.",
    ["There's No-one Here"] = "Make a stormtrooper pig shoot another one.",
    ["Imperial Entanglement"] = "Pop a stormtrooper pig with a falling TIE fighter wing.",
    ["Walking Carpet"] = "Let Chewbacca take {L:Walking Carpet|6} shots from one turret.",
    ["Simple Pigs and Nonsense"] = "Pop {L:Simple Pigs and Nonsense|300} stormtrooper pigs.",
    ["The Birds Will Be With You"] = "Clear {L:The Birds Will Be With You|10} levels with one bird.",
    ["Lack of Faith"] = "Restart levels {L:Lack of Faith|100} times.",
    ["Pork side of the Force"] = "Fail {L:Pork side of the Force|10} levels with one pig left.",
    ["Popcorn Popper"] = "Pop {L:Popcorn Popper|300} popcorn seeds.",
}

local function trim(s) return (string.gsub(s, "^%s+", "")):gsub("%s+$", "") end
local function number(n)
    local s = tostring(math.floor(n + 0.5))
    while true do local k s, k = string.gsub(s, "^(%d+)(%d%d%d)", "%1,%2") if k == 0 then return s end end
end

-- the achievement processor of Classic / Seasons (any table of the game that keeps birdSlingerLimit)
local processor
local function findProcessor()
    if processor ~= nil then return processor end
    processor = false
    for _, v in pairs(env) do
        if type(v) == "table" and rawget(v, "birdSlingerLimit") ~= nil then processor = v break end
    end
    return processor
end

-- what the game's data says: worlds (pages) that give an achievement when cleared, levels and episodes that give one
local data
local function scan()
    if data then return data end
    data = {clear = {}, level = {}, stars = {}, score = {}, episodeLimit = {}}
    local seen, skip = {}, {objects = true, _G = true, package = true, settings = true, highscores = true, blockTable = true, loadedObjects = true}
    local function walk(t, key, depth)
        if seen[t] or depth > 4 then return end
        seen[t] = true
        local clear, level = rawget(t, "clear_achievement"), rawget(t, "levelAchievement")
        if type(clear) == "string" then data.clear[trim(clear)] = t end
        if type(level) == "string" then data.level[trim(level)] = key end
        local star, score = rawget(t, "starAchievement"), rawget(t, "scoreAchievement")
        if type(star) == "string" then data.stars[trim(star)] = key end
        if type(score) == "string" then data.score[trim(score)] = key end
        for k, v in next, t do
            if type(v) == "table" and not (depth == 0 and skip[k]) then walk(v, k, depth + 1) end
        end
    end
    walk(env, nil, 0)
    -- the episode list (Classic, Seasons): each world's episode, and each episode's score target by number and by name
    data.episodeOf, data.limitByName, data.clearCount = {}, {}, {}
    if type(env.g_episodes) == "table" then
        for i, ep in ipairs(env.g_episodes) do
            if type(ep) == "table" then
                data.episodeLimit[i] = rawget(ep, "score_achievement_limit")
                if type(rawget(ep, "name")) == "string" then data.limitByName[ep.name] = rawget(ep, "score_achievement_limit") end
                if type(rawget(ep, "pages")) == "table" then
                    for _, pg in ipairs(ep.pages) do
                        if type(pg) == "table" and type(rawget(pg, "clear_achievement")) == "string" and pg.clear_achievement ~= "" then
                            data.episodeOf[pg] = ep
                            data.clearCount[ep] = (data.clearCount[ep] or 0) + 1
                        end
                    end
                end
            end
        end
    end
    return data
end

local function limit(spec)
    local hours = false
    if string.sub(spec, 1, 2) == "H:" then hours = true spec = string.sub(spec, 3) end
    local kind, rest = string.match(spec, "^(%a):(.*)$")
    local name, default = string.match(rest or "", "^(.-)|(.*)$")
    local v
    if kind == "P" then
        local p = findProcessor()
        if p then v = rawget(p, name) end
    elseif kind == "L" then
        local l = type(env.currentAchievementLimits) == "table" and env.currentAchievementLimits or env.achievementLimits
        if type(l) == "table" then v = l[name] end
    end
    if type(v) ~= "number" then v = _G.tonumber(default) or 0 end
    if hours then return number(v / 3600) end
    return number(v)
end

local function describe(name)
    local n = trim(name)
    if type(env.abAchievementTexts) == "table" and type(env.abAchievementTexts[n]) == "string" then return env.abAchievementTexts[n] end      -- 3.6: a script's own text
    local t = texts[n]
    if t then return (string.gsub(t, "{([^}]+)}", limit)) end
    local d = scan()
    local page = d.clear[n]
    if page then
        -- Seasons names its episodes in English and numbers its levels page by page; Classic numbers them by world (1-1, 2-1 ...)
        local ep = d.episodeOf[page]
        local epName = ep and rawget(ep, "name")
        if type(epName) == "string" and not string.find(epName, "^TEXT_") then
            if (d.clearCount[ep] or 0) > 1 and rawget(page, "display_number") then return "Complete every level of " .. epName .. ", page " .. tostring(page.display_number) .. "." end
            return "Complete every level of " .. epName .. "."
        end
        local w = rawget(page, "world_number")
        if w then return "Complete every level of world " .. tostring(w) .. "." end
        return "Complete every level of its world."
    end
    local ep = string.match(n, "^(.-) %- Total Destruction$") or string.match(n, "^(.-) %- War for the Stars$")
    if ep then return "Earn three stars on every level of " .. ep .. "." end
    ep = string.match(n, "^(.-) %- Score Addict$") or string.match(n, "^(.-) %- Power of the Force$")
    if ep then
        local l = type(env.currentAchievementLimits) == "table" and env.currentAchievementLimits[n] or (type(env.achievementLimits) == "table" and env.achievementLimits[n])
        local i = _G.tonumber(string.match(ep, "^Episode (%d+)$") or "")
        if type(l) ~= "number" and i then l = d.episodeLimit[i] end
        if type(l) ~= "number" then l = d.limitByName[ep] end
        -- Angry Birds HD keeps no limits in its scripts (Game Center checked them): these are the ones it showed
        if type(l) ~= "number" and i and string.sub(n, -12) == "Score Addict" then l = ({4000000, 3300000, 4800000, 3900000})[i] end
        if type(l) == "number" then return "Score " .. number(l) .. " points in total in " .. ep .. "." end
        return "Reach the target score of " .. ep .. "."
    end
    if d.level[n] then return "Beat level " .. tostring(d.level[n]) .. "." end
    return ""
end
env.abAchievementText = describe

-- ---------------------------------------------------------------------------------------------------------------------------------
-- unlocked or not (the engine's own record, and the games' own where they keep one)
local function unlocked(name)
    local s = env.settings
    if type(s) == "table" then
        if type(s.abAchievements) == "table" and s.abAchievements[name] then return s.abAchievements[name] end
        if type(s.achievements) == "table" and s.achievements[name] then return true end
    end
    local sw = env.SettingsWrapper
    if type(sw) == "table" and type(sw.hasAchievement) == "function" then
        local ok, has = pcall(sw.hasAchievement, sw, name)
        if ok and has then return true end
    end
    return nil
end

local function iconOf(name)
    local a = env.achievements[name]
    local icon = type(a) == "table" and a.icon or nil
    if not icon and type(env.g_achievements) == "table" and type(env.g_achievements[name]) == "table" then icon = env.g_achievements[name].icon end
    local popup = env.achievementPopUpPage      -- Angry Birds HD 1.4.2 keeps them with its pop-up page
    if not icon and type(popup) == "table" and type(rawget(popup, "icons")) == "table" then icon = popup.icons[name] end
    return icon
end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- the unlock pop-up: the game's own (BannerPopup in Space / Star Wars, AchievementPopup in Classic 2.x / 3.x and Seasons), or a banner
-- drawn the same way where the game has none (Angry Birds HD 1.4.2 showed Crystal's)
local queue = {}
local current
local clock = 0   -- seconds of play, counted from the frames (os.clock is processor time: it hardly moves while the game waits for the screen)
if type(env.achievementsEnabled) == "function" and env.gameCenterEnabled ~= true then env.achievementsEnabled = function() return env.achievements ~= nil end end
env.unlockAchievement = function(id, name)
    name = name or tostring(id)
    for _, q in ipairs(queue) do if q.name == name then return end end
    if type(env.settings) == "table" and type(env.settings.abAchievements) == "table" and env.settings.abAchievements[name] then return end   -- reported before: Game Center shows nothing again
    table.insert(queue, {id = id, name = name, icon = iconOf(name), text = describe(name)})
    if type(env.settings) == "table" then
        if type(env.settings.abAchievements) ~= "table" then env.settings.abAchievements = {} end
        if env.settings.abAchievements[name] == nil then env.settings.abAchievements[name] = _G.os.time() end
    end
    _G.print("Game Center: achievement unlocked: " .. name)
end

local function gamePopup(b)
    local frame = env.notificationsFrame
    if type(frame) ~= "table" or type(frame.addChild) ~= "function" then return false end
    local title = trim(b.name)
    if type(env.BannerPopup) == "table" and type(env.BannerPopup.new) == "function" then
        return pcall(function()
            local box = env.BannerPopup:new(title, b.text, b.icon or env.achievementPlaceholderIcon or "")
            box.name = "achievement"
            frame:addChild(box)
            box:onEntry()
            box:layout()
        end)
    end
    if type(env.showAchievementPopUp) == "function" and type(env.achievementPopUpPage) == "table" then
        -- Classic 1.x / 2.0.0 and HD 1.4.2: the pop-up is a menu page that shows gameCenter.achievements[showPopUpID] when Game Center is on
        local id = b.id or b.name
        if type(env.gameCenter) ~= "table" then env.gameCenter = {} end
        if type(env.gameCenter.achievements) ~= "table" then env.gameCenter.achievements = {} end
        env.gameCenter.achievements[id] = {title = title, achievedText = b.text ~= "" and b.text or "Achievement unlocked", icon = b.icon or "", completed = true}
        env.gameCenter.achievements.showPopUpID = id
        local on = env.gameCenterEnabled
        env.gameCenterEnabled = true
        local ok = pcall(env.showAchievementPopUp)
        env.gameCenterEnabled = on
        return ok
    end
    if type(env.AchievementPopup) == "table" and type(env.AchievementPopup.new) == "function" and b.icon then
        -- it reads gameCenter.achievements[id] (title, achievedText, icon) while it lays itself out
        local id = b.id or b.name
        local gc = env.gameCenter
        local made = gc == nil
        if made then env.gameCenter = {achievements = {}} end
        if type(env.gameCenter.achievements) ~= "table" then env.gameCenter.achievements = {} end
        local old = env.gameCenter.achievements[id]
        env.gameCenter.achievements[id] = {title = title, achievedText = b.text, icon = b.icon, completed = true}
        local ok = pcall(function()
            local box = env.AchievementPopup:new(id)
            box.name = "achievement"
            if type(frame.addTimedChild) == "function" then frame:addTimedChild(box, 2.7) else frame:addChild(box) end
            box:onEntry()
            box:layout()
        end)
        env.gameCenter.achievements[id] = old
        if made then env.gameCenter = nil end
        return ok
    end
    return false
end

local function nextPopup()
    local now = clock
    if current and now - current.since < 3.4 then return end
    current = nil
    local b = table.remove(queue, 1)
    if not b then return end
    b.since = now
    b.own = not gamePopup(b)
    current = b
end

local function text(str, tx, ty, size, va, ha, alpha)
    local res = _G.res
    local fh = (res.getFontHeight and res.getFontHeight()) or size
    local k = size / math.max(fh, 1)
    env.setRenderState(0, 0, k, k, 0, 0, 0, alpha or 1)
    res.drawString("", str, tx / k, ty / k, va, ha)
    env.setRenderState()
end
local function font(headline)
    local f = headline and ((env.getFontHeadline and env.getFontHeadline()) or env.FONT_HEADLINE) or nil
    f = f or (env.getFontBasic and env.getFontBasic()) or env.FONT_BASIC or "FONT_BASIC"
    if not pcall(_G.res.useFont, f) then pcall(_G.res.useFont, "FONT_BASIC") end
end
local function icon(name, x, y, size, alpha)
    local res = _G.res
    if not (name and res.getSpriteBounds) then return false end
    local iw, ih = res.getSpriteBounds(name)
    if not (iw and iw > 0) then return false end
    local k = size / math.max(iw, ih)
    env.setRenderState(0, 0, 1, 1, 0, 0, 0, alpha or 1)
    res.drawSprite(name, x + (size - iw * k) / 2, y + (size - ih * k) / 2, "TOP", "LEFT", iw * k, ih * k)
    env.setRenderState()
    return true
end

-- a stamp for an achievement whose icon the game does not have (Angry Birds HD 1.4.2 ships none: Crystal sent them): a gold badge
-- with the world number, E and the episode number, or the initials of the name
local worldOf = {["Herr Helmet"] = 1, ["Mr Moustache"] = 2, ["Defeat of The King"] = 3, ["The Imposter"] = 4, ["The Mysterious Escape"] = 5,
    ["Hovering Helmet"] = 6, ["Mounting Moustache"] = 7, ["Green Baron"] = 8, ["Hardhat Hidalgo"] = 9, ["Mason Moustache"] = 10, ["Royal Ringleader"] = 11}
local function stamp(name, x, y, size)
    local n = trim(tostring(name or ""))
    local label = worldOf[n] and tostring(worldOf[n]) or nil
    if not label then local e = string.match(n, "^Episode (%d+)") if e then label = "E" .. e end end
    if not label then
        label = ""
        for w in string.gmatch(n, "%a+") do if #label < 2 then label = label .. string.upper(string.sub(w, 1, 1)) end end
    end
    env.drawRect(0.55, 0.36, 0.05, 1, x, y, x + size, y + size, false)
    env.drawRect(0.98, 0.78, 0.2, 1, x + size * 0.07, y + size * 0.07, x + size * 0.93, y + size * 0.93, false)
    env.drawRect(1, 0.9, 0.45, 1, x + size * 0.07, y + size * 0.07, x + size * 0.93, y + size * 0.4, false)
    font(true)
    text(label, x + size / 2, y + size / 2, size * (#label > 1 and 0.5 or 0.62), "VCENTER", "HCENTER")
    return true
end

-- the first of these sprites the game has (0 x 0 when it has none, or its sheet is not loaded)
local function sprite(names)
    local res = _G.res
    if not res.getSpriteBounds then return nil end
    for _, n in ipairs(names) do
        local ok, w, h = pcall(res.getSpriteBounds, n)
        if ok and w and w > 0 and h and h > 0 then return n, w, h end
    end
end
local function useFont(names)
    for _, f in ipairs(names) do if f and pcall(_G.res.useFont, f) then return end end
end

-- a pop-up for a made-up achievement, to see how they look: nothing is recorded
env.abTestAchievement = function()
    local any
    for name in pairs(env.achievements) do if type(name) == "string" and iconOf(name) then any = name break end end
    table.insert(queue, {id = "abTest", name = "Test Achievement", icon = any and iconOf(any), text = "Not a real one: this only shows the pop-up."})
end

env.abDrawBanner = function()
    local b = current
    if not (b and b.own) then return end
    local t = clock - b.since
    if t > 3.4 then return end
    local sw, sh = env.screenWidth or 1024, env.screenHeight or 768
    local h = math.max(64, sh * 0.15)
    local w = math.min(sw * 0.62, h * 4.6)
    local slide = math.min(t / 0.3, 1, (3.4 - t) / 0.3)
    local x, y = sw - w - sh * 0.02, sh - h * slide - sh * 0.02 * slide   -- from the bottom right corner, as the phone's pop-up
    local L, lw, lh = sprite({"ACHIEVEMENT_BG_LEFT"})
    local M = sprite({"ACHIEVEMENT_BG_MIDDLE"})
    local R, rw = sprite({"ACHIEVEMENT_BG_RIGHT"})
    if L and M and R then
        -- the game's own pop-up pieces (Classic's ACHIEVEMENT_BG_LEFT / MIDDLE / RIGHT): title along the top, icon, text below it
        local k = h / lh
        local res = _G.res
        env.setRenderState()
        res.drawSprite(L, x, y, "TOP", "LEFT", lw * k, h)
        res.drawSprite(M, x + lw * k, y, "TOP", "LEFT", math.max(0, w - (lw + rw) * k), h)
        res.drawSprite(R, x + w - rw * k, y, "TOP", "LEFT", rw * k, h)
        useFont({"FONT_GAMECENTER_BASIC", env.getFontBasic and env.getFontBasic(), env.FONT_BASIC, "FONT_BASIC"})
        text(trim(b.name), x + w / 2, y + h * 0.1, h * 0.26, "TOP", "HCENTER")
        local tx = x + w * 0.08
        if icon(b.icon, tx, y + h * 0.38, h * 0.48) or stamp(b.name, tx, y + h * 0.38, h * 0.48) then tx = tx + h * 0.58 end
        useFont({"FONT_GAMECENTER_BASIC", env.getFontBasic and env.getFontBasic(), env.FONT_BASIC, "FONT_BASIC"})
        local str = b.text ~= "" and b.text or "Achievement unlocked"
        local size = h * 0.22
        if res.getStringWidth then
            local fh = (res.getFontHeight and res.getFontHeight()) or size
            local width = res.getStringWidth(str) * size / math.max(fh, 1)
            if width > x + w - tx - h * 0.15 then size = size * (x + w - tx - h * 0.15) / width end
        end
        text(str, tx, y + h * 0.66, size, "VCENTER", "LEFT")
        return
    end
    local head = h * 0.36
    env.drawRect(0.85, 0.82, 0.3, 1, x - 3, y - 3, x + w + 3, y + h + 3, false)
    env.drawRect(0.05, 0.3, 0.33, 0.97, x, y, x + w, y + h, false)
    env.drawRect(0.09, 0.42, 0.45, 1, x, y, x + w, y + head, false)
    env.drawRect(0.6, 0.85, 0.85, 0.6, x, y + head - 1, x + w, y + head + 1, false)
    font(true)
    text(string.upper(trim(b.name)), x + w / 2, y + head * 0.52, head * 0.62, "VCENTER", "HCENTER")
    local tx = x + h * 0.15
    if icon(b.icon, tx, y + head + (h - head - h * 0.52) / 2, h * 0.52) or stamp(b.name, tx, y + head + (h - head - h * 0.52) / 2, h * 0.52) then tx = tx + h * 0.62 end
    font(false)
    local str = b.text ~= "" and b.text or "Achievement unlocked"
    local size = (h - head) * 0.34
    local res = _G.res
    if res.getStringWidth then
        local fh = (res.getFontHeight and res.getFontHeight()) or size
        local width = res.getStringWidth(str) * size / math.max(fh, 1)
        if width > x + w - tx - h * 0.12 then size = size * (x + w - tx - h * 0.12) / width end
    end
    text(str, tx, y + head + (h - head) * 0.5, size, "VCENTER", "LEFT")
end

-- ---------------------------------------------------------------------------------------------------------------------------------
-- the page: a scrolling list like the Windows Phone port's, with a back arrow, "n / m achieved" and a row per achievement. It scrolls
-- smoothly (a drag carries on and slows down, the wheel and the keys glide) and gives the game no input while it is open
local page = {open = false, scroll = 0, target = 0, items = {}}
env.abAchPage = page

local function list()
    local l = {}
    for name in pairs(env.achievements) do
        if type(name) == "string" then l[#l + 1] = {name = name, title = trim(name), icon = iconOf(name), text = describe(name)} end
    end
    table.sort(l, function(a, b)
        local ua, ub = unlocked(a.name) and 1 or 0, unlocked(b.name) and 1 or 0
        if ua ~= ub then return ua > ub end
        return a.title < b.title
    end)
    return l
end
local function geometry()
    local sw, sh = env.screenWidth or 1024, env.screenHeight or 768
    local head = math.max(56, sh * 0.16)
    local row = math.max(48, sh * 0.135)
    local w = math.min(sw, sh * 1.6)
    local x = (sw - w) / 2
    return sw, sh, x, w, head, row
end
local function most()
    local sw, sh, x, w, head, row = geometry()
    return math.max(0, #page.items * row - (sh - head))
end

page.show = function()
    page.open = true
    page.scroll, page.target, page.vel, page.drag = 0, 0, 0, nil
    page.items = list()
end
page.close = function()
    if not page.open then return end
    page.open = false
    if type(env.crystalUiDeactivated) == "function" then pcall(env.crystalUiDeactivated) end   -- HD 1.4.2: the player closed Crystal
end
env.showAchievements = page.show
-- Angry Birds HD 1.4.2: the gem button (startCrystal) opened Chillingo's Crystal, whose main page was the achievement list
env.activateCrystalUI = page.show
env.activateCrystalUIAtProfile = page.show
env.deactivateCrystalUI = function() page.open = false end
env.isCrystalUIShowing = function() return page.open end
env.userEnabledCrystal = function() return true end

env.abAchievementInput = function()
    clock = clock + math.min(env.abFrameDt or 1 / 60, 0.1)
    nextPopup()
    local kp, kr, kh = env.keyPressed, env.keyReleased, env.keyHold
    if not page.open then
        if type(kp) == "table" and kp.F7 then kp.F7 = nil page.show() else return end
    end
    local sw, sh, x, w, head, row = geometry()
    local dt = math.min(env.abFrameDt or 1 / 60, 0.1)
    local c = env.cursor or {}
    local cx, cy = c.x or 0, c.y or 0
    if type(kp) == "table" then
        if kp.ESCAPE or kp.F7 or kp.BACKSPACE then page.close() end
        if kp.UP then page.target = page.target - row end
        if kp.DOWN then page.target = page.target + row end
        if kp.PAGEUP then page.target = page.target - (sh - head) end
        if kp.PAGEDOWN then page.target = page.target + (sh - head) end
        if kp.HOME then page.target = 0 end
        if kp.END then page.target = most() end
        if kp.T then page.close() env.abTestAchievement() end
        if kp.LBUTTON then
            if cy < head and cx < x + head * 1.2 then page.close()
            elseif cy < head and cx > x + w - head * 2.2 then page.close() env.abTestAchievement()   -- the "test pop-up" button
            else page.drag = {y = cy, scroll = page.scroll, last = cy, moved = 0} page.vel = 0 end
        end
    end
    if c.wheelTriggered and c.wheel and c.wheel ~= 0 then page.target = page.target - c.wheel / math.abs(c.wheel) * row * 1.5 end
    if page.drag and type(kh) == "table" and kh.LBUTTON then
        local d = page.drag
        page.scroll = d.scroll - (cy - d.y)
        if page.scroll < 0 then page.scroll = page.scroll * 0.5 end                  -- pulled past an end: it gives a little
        if page.scroll > most() then page.scroll = most() + (page.scroll - most()) * 0.5 end
        page.vel = (page.vel or 0) * 0.6 + (-(cy - d.last) / math.max(dt, 1 / 240)) * 0.4
        d.last = cy
        page.target = page.scroll
    elseif page.drag then
        page.drag = nil
        page.target = page.scroll + (page.vel or 0) * 0.32                           -- the flick carries on and slows down
        page.vel = 0
    end
    page.target = math.max(0, math.min(page.target, most()))
    if not page.drag then page.scroll = page.scroll + (page.target - page.scroll) * math.min(1, dt * 9) end
    -- the game sees no key, click or touch while the page is open (or on the frame it closes)
    for _, t in ipairs({kp, kr, kh, env.touches}) do if type(t) == "table" then for k in pairs(t) do t[k] = nil end end end
    if c.wheelTriggered ~= nil then c.wheelTriggered = false c.wheel = 0 end
    if env.touchcount then env.touchcount = 0 end
end

env.abDrawAchievements = function()
    if not page.open then return end
    local sw, sh, x, w, head, row = geometry()
    local got = 0
    for _, it in ipairs(page.items) do if unlocked(it.name) then got = got + 1 end end
    env.drawRect(0.03, 0.2, 0.24, 1, 0, 0, sw, sh, false)
    -- rows
    local first = math.max(1, math.floor(page.scroll / row) + 1)
    for i = first, #page.items do
        local it = page.items[i]
        local ry = head + (i - 1) * row - page.scroll
        if ry > sh then break end
        local when = unlocked(it.name)
        local shade = i % 2 == 0 and 0.08 or 0
        env.drawRect(0.1 + shade, 0.52 + shade, 0.58 + shade, 1, x, ry, x + w, ry + row - 2, false)
        local size = row * 0.7
        local ix = x + row * 0.25
        if not (when and (icon(it.icon, ix, ry + (row - size) / 2, size) or stamp(it.name, ix, ry + (row - size) / 2, size))) then
            font(true)
            text("?", ix + size / 2, ry + row / 2, size * 0.8, "VCENTER", "HCENTER", 0.45)
        end
        local tx = ix + size + row * 0.3
        font(true)
        text(it.title, tx, ry + row * 0.36, row * 0.3, "VCENTER", "LEFT")
        font(false)
        if it.text ~= "" then text(it.text, tx, ry + row * 0.72, row * 0.2, "VCENTER", "LEFT", 0.85) end
        local status = "Locked"
        if type(when) == "number" then status = _G.os.date("%Y-%m-%d", when) elseif when then status = "Unlocked" end
        text(status, x + w - row * 0.3, ry + row * 0.5, row * 0.2, "VCENTER", "RIGHT", when and 1 or 0.6)
    end
    -- scroll bar
    local m = most()
    if m > 0 then
        local track = sh - head
        local bar = math.max(row * 0.5, track * track / (track + m))
        local by = head + (track - bar) * math.max(0, math.min(1, page.scroll / m))
        env.drawRect(1, 1, 1, 0.75, x + w - 7, by + 4, x + w - 3, by + bar - 4, false)
    end
    -- header with the back arrow
    env.drawRect(0.02, 0.27, 0.32, 1, 0, 0, sw, head, false)
    env.drawRect(0.5, 0.8, 0.82, 0.5, 0, head - 2, sw, head, false)
    local b = head * 0.72
    local bx, by = x + head * 0.18, (head - b) / 2
    -- the "test pop-up" button at the right
    local tw = head * 1.9
    env.drawRect(0.55, 0.38, 0.05, 1, x + w - tw - head * 0.18 - 2, by + b * 0.15 - 2, x + w - head * 0.18 + 2, by + b * 0.85 + 2, false)
    env.drawRect(0.99, 0.8, 0.22, 1, x + w - tw - head * 0.18, by + b * 0.15, x + w - head * 0.18, by + b * 0.85, false)
    font(true)
    text("TEST POP-UP", x + w - head * 0.18 - tw / 2, head / 2, b * 0.3, "VCENTER", "HCENTER")
    -- the game's own back button (Classic: LS_BACK_BUTTON in BUTTONS_SHEET_1), or one drawn like it
    local back, bw, bh = sprite({"LS_BACK_BUTTON", "BTN_BACK", "BUTTON_BACK", "BACK_BUTTON"})
    if back then
        -- at the size the game's own menus draw it (its picture has a wide clear margin, so it is not fitted to the bar)
        local k = math.min(sh / 768, head * 1.25 / bh)
        env.setRenderState(0, 0, k, k, 0, 0, 0, 1)
        _G.res.drawSprite(back, x / k, (head * 0.92 - bh * k) / k, "TOP", "LEFT")   -- its button sits at the bottom of the picture
        env.setRenderState()
        b = 0
    end
    if b > 0 then
    env.drawRect(0.55, 0.38, 0.05, 1, bx - 2, by - 2, bx + b + 2, by + b + 2, false)
    env.drawRect(0.99, 0.8, 0.22, 1, bx, by, bx + b, by + b, false)
    -- the arrow: a bar and a head made of thin slices (the 2D drawing has no triangles)
    local cxm, cym, l = bx + b / 2, by + b / 2, b * 0.3
    local t = b * 0.11
    env.drawRect(1, 1, 1, 1, cxm - l * 0.35, cym - t / 2, cxm + l, cym + t / 2, false)
    local steps = 24
    for i = 0, steps - 1 do
        local f0, f1 = i / steps, (i + 1) / steps
        local half = l * 0.75 * f1
        env.drawRect(1, 1, 1, 1, cxm - l + l * 0.85 * f0, cym - half, cxm - l + l * 0.85 * f1, cym + half, false)
    end
    end
    font(true)
    text("ACHIEVEMENTS", sw / 2, head * 0.36, head * 0.3, "VCENTER", "HCENTER")
    font(false)
    text(got .. " / " .. #page.items .. " achieved", sw / 2, head * 0.72, head * 0.2, "VCENTER", "HCENTER")
    env.setRenderState()
end
