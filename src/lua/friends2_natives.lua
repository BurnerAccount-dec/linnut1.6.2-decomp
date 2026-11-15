-- Angry Birds Friends 2.0.0 (Android): the phone's native services, answered on this computer.
--
-- Friends 2.0.0 is built on Rovio's newer framework: the game reaches its server through native modules
-- (service, social, user, tournament, shop, game, gamerServices ...) that the PC engine does not have. The game's own
-- TutorialMode.lua and PracticeMode.lua replace user, tournament, shop and game with local versions while those modes
-- run, and set them back to nil afterwards; so these modules sit behind the globals (in the environment's fallback),
-- never in them, exactly as the phone's natives did.
--
-- A call is answered the way a server answers: callbacks run one frame later, with nil for "no error".
-- Run by the engine inside the game's environment when scripts/TutorialMode.lua exists (script_host.cpp).

local env = ...

local queue, flushing = {}, false
local function flush()
    local jobs = queue
    queue = {}
    for _, job in ipairs(jobs) do
        local ok, err = xpcall(function() return job.fn(unpack(job.args, 1, job.n)) end, function(m) return debug and debug.traceback and debug.traceback(m) or m end)
        if not ok then print("friends 2.0.0: " .. tostring(err)) end
    end
end
local function later(fn, ...)
    if type(fn) ~= "function" then return end
    local engine = env.engine or _G.engine
    if not (engine and engine.on) then fn(...) return end
    if not flushing then flushing = true engine.on("update", flush) end
    queue[#queue + 1] = {fn = fn, args = {...}, n = select("#", ...)}
end

-- a module whose missing functions do nothing (and say so once), so a call the game makes that is not answered here
-- shows in the console instead of stopping the game
local reported = {}
local function module(name, fields)
    return setmetatable(fields, {__index = function(t, k)
        if type(k) ~= "string" then return nil end
        if not reported[name .. "." .. k] then reported[name .. "." .. k] = true print("friends 2.0.0: " .. name .. "." .. k .. " is not there") end
        return function() end
    end})
end

-- ---------------------------------------------------------------------------------------------------------------------
-- The online part: a guest session on a stand-in server, so "Play Tournament" works on this computer.
--
-- The phone game got the Weekly Tournament's levels, the scores, the coins and the shop from Rovio's server, which is
-- gone. Here the tournament's levels are the level files in data/levels/tournament (Level<code>.lua, the game's own
-- level format; the apk ships none). With more than six there, six are played each week, the week ending on Monday
-- at midnight. Their star scores come from a starLimits.lua in that folder (Level<code> = {silverScore = ...,
-- goldScore = ...}), else from the level file's own scores (Gold / Silver, as Friends 1.0.0's levels have), else from the
-- game's starTable, else 40000 / 60000. The score lists hold the player and the
-- game's two tutorial bots: Red scores each level's three-star score (goldScore) and Chuck its two-star score
-- (silverScore). Coins ("credits"), bought power-ups and slingshots, and the week's scores are kept in settings.lua
-- (settings.abServer2). Prices and prizes are made up: the real ones came from the server.
-- ---------------------------------------------------------------------------------------------------------------------
local floor, max = math.floor, math.max
local WEEK = 7 * 24 * 60 * 60
local EPOCH = os.time({year = 2013, month = 4, day = 29, hour = 0})   -- a Monday at midnight: weeks run Monday to Monday
local LEVELS_PER_WEEK = 6
local PODIUM_COINS = {30, 20, 10}
local STAR_COINS = {2, 4, 8}
local WELCOME_COINS = 100
local DEFAULT_LIMITS = {silverScore = 40000, goldScore = 60000}

-- the shop: power-ups (id = the item the game counts), the pack, and the slingshots (kept for good)
local POWERUP_PRICES = {{price = 30, totalQuantity = 3, freeQuantity = 0, referencePrice = 30, salesPrice = ""}, {price = 90, totalQuantity = 11, freeQuantity = 1, referencePrice = 90, salesPrice = ""}, {price = 200, totalQuantity = 30, freeQuantity = 5, referencePrice = 200, salesPrice = ""}}
local CATALOG = {
    {id = "POWER_UP", name = "Power-ups", items = {
        {id = "superseed", type = "CONSUMABLE", currency = "IVC", prices = POWERUP_PRICES},
        {id = "kingsling", type = "CONSUMABLE", currency = "IVC", prices = POWERUP_PRICES},
        {id = "slingscope", type = "CONSUMABLE", currency = "IVC", prices = POWERUP_PRICES},
        {id = "birdquake", type = "CONSUMABLE", currency = "IVC", prices = POWERUP_PRICES},
        {id = "extrabird", type = "CONSUMABLE", currency = "IVC", prices = {{price = 30, totalQuantity = 1, freeQuantity = 0, referencePrice = 30, salesPrice = ""}, {price = 130, totalQuantity = 5, freeQuantity = 0, referencePrice = 130, salesPrice = ""}}},
        {id = "PowerupBundle", type = "CONSUMABLE", currency = "IVC", prices = {{price = 100, totalQuantity = 3, freeQuantity = 0, referencePrice = 100, salesPrice = ""}, {price = 300, totalQuantity = 10, freeQuantity = 1, referencePrice = 300, salesPrice = ""}}}}},
    {id = "SLINGSHOT", name = "Slingshots", items = {
        {id = "GoldenSling", type = "DURABLE", currency = "IVC", prices = {{price = 500, totalQuantity = 1, freeQuantity = 0, referencePrice = 500, salesPrice = ""}}},
        {id = "WoodChipper", type = "DURABLE", currency = "IVC", prices = {{price = 300, totalQuantity = 1, freeQuantity = 0, referencePrice = 300, salesPrice = ""}}},
        {id = "StoneCutter", type = "DURABLE", currency = "IVC", prices = {{price = 300, totalQuantity = 1, freeQuantity = 0, referencePrice = 300, salesPrice = ""}}},
        {id = "GlassBreaker", type = "DURABLE", currency = "IVC", prices = {{price = 300, totalQuantity = 1, freeQuantity = 0, referencePrice = 300, salesPrice = ""}}}}},
}
local BUNDLE = {PowerupBundle = {"superseed", "kingsling", "slingscope", "birdquake"}}
-- the daily wheel's eight pieces, in the game's order (WheelPopup.lua): 5, 10 and 50 coins, then one of each power-up
local WHEEL = {{"credits", 5}, {"credits", 10}, {"credits", 50}, {"superseed", 1}, {"slingscope", 1}, {"birdquake", 1}, {"extrabird", 1}, {"kingsling", 1}}
local function today(offset) return tonumber(os.date("%Y%m%d", os.time() + (offset or 0))) end

local session = "Uninitialized"
-- the phone points the game at the session's levels (episodes.lua: 1 tournament, 2 tutorial, 3 practice) when it opens one
local function episodes(kind) local f = env["setup" .. kind .. "Episodes"] if type(f) == "function" then f() end end
local player = {id = "0", firstName = "You", lastName = "", name = "You", nickname = "", profileImageURL = ""}

-- the saved state, in the game's own settings table
local function state()
    local settings = env.settings
    if type(settings) ~= "table" then return {items = {}, scores = {}, trophies = {0, 0, 0}} end
    local s = settings.abServer2
    if type(s) ~= "table" then s = {} settings.abServer2 = s end
    s.items = s.items or {}
    s.scores = s.scores or {}
    s.trophies = s.trophies or {0, 0, 0}
    return s
end
local function save()
    if env.saveLuaFileWrapper then pcall(env.saveLuaFileWrapper, "settings.lua", "settings", true) end
end
local function amount(name) return state().items[name] or 0 end
-- a changed amount is told to the game as the phone did (EID_INVENTORY_ITEM_UPDATED: the coin wallet and the shop's counts follow it)
local function give(name, n)
    local s = state()
    local from = s.items[name] or 0
    s.items[name] = max(0, from + n)
    local ev = env.events and env.events.EID_INVENTORY_ITEM_UPDATED
    if ev and env.eventManager and s.items[name] ~= from then
        pcall(env.eventManager.notify, env.eventManager, {id = ev, itemId = name, fromAmount = from, toAmount = s.items[name]})
    end
end

-- the tournament's levels: the files in data/levels/tournament
-- (or, while that folder has none, Friends 1.0.0's tournament levels: the engine copies them to data/levels/friends_1.0.0)
local folderLevels, limits
local levelFolder = "/levels/tournament"
local function dataRoot() return (string.gsub(tostring(env.scriptPath or ""), "/scripts$", "")) end
local function allLevels()
    if folderLevels then return folderLevels end
    folderLevels, limits = {}, {}
    for _, sub in ipairs({"/levels/tournament", "/levels/friends_1.0.0"}) do
        for _, name in ipairs(env.abListFolder and env.abListFolder(dataRoot() .. sub) or {}) do
            local code = string.match(name, "^Level(.+)%.lua$")
            if code and code ~= "" then folderLevels[#folderLevels + 1] = code end
        end
        if #folderLevels > 0 then levelFolder = sub break end
    end
    local folder = dataRoot() .. levelFolder
    table.sort(folderLevels, function(a, b)
        local na, nb = tonumber(string.match(a, "(%d+)$")), tonumber(string.match(b, "(%d+)$"))
        if na and nb and na ~= nb then return na < nb end
        return a < b
    end)
    local ok, t = pcall(env.abNativeLoadLuaTable, folder .. "/starLimits.lua")
    if ok and type(t) == "table" then limits = t end
    if #folderLevels == 0 then print("friends 2.0.0: no tournament levels in " .. dataRoot() .. "/levels/tournament (Level<code>.lua files)") end
    return folderLevels
end
local function weekNumber() return floor((os.time() - EPOCH) / WEEK) end
local function weekCodes(week)
    local all = allLevels()
    if #all <= LEVELS_PER_WEEK then return all end
    local list, weeks = {}, math.ceil(#all / LEVELS_PER_WEEK)
    local first = (week % weeks) * LEVELS_PER_WEEK
    for i = 1, LEVELS_PER_WEEK do list[i] = all[(first + i - 1) % #all + 1] end
    return list
end
local levelScores = {}
local function starLimits(code)                                           -- starLimits.lua in the folder, else the level file's own
    local name = "Level" .. code                                         -- scores (Gold / Silver), else the game's starTable, else 40000 / 60000
    local t = limits and limits[name]
    if type(t) ~= "table" then
        if levelScores[code] == nil then
            levelScores[code] = false
            local ok, level = pcall(env.abNativeLoadLuaTable, dataRoot() .. levelFolder .. "/" .. name .. ".lua")
            local sc = ok and type(level) == "table" and level.scores
            if type(sc) == "table" and tonumber(sc.Gold) and tonumber(sc.Silver) then levelScores[code] = {silverScore = tonumber(sc.Silver), goldScore = tonumber(sc.Gold)} end
        end
        t = levelScores[code] or nil
    end
    if type(t) ~= "table" then t = env.starTable and rawget(env.starTable, name) end
    if type(t) ~= "table" then t = DEFAULT_LIMITS end
    return {silverScore = t.silverScore or DEFAULT_LIMITS.silverScore, goldScore = t.goldScore or DEFAULT_LIMITS.goldScore}
end

-- score lists: the player and the two bots, best first; a list entry has the fields the game's own bots have
local function entry(id, name, points, bot)
    return {id = id, name = name, firstName = name, lastName = "", nickname = name, points = points, rank = 0, isBot = bot, isOwnPlayer = not bot,
            gift = false, tournamentPrize = 0, leagueStars = 0, leagueTier = "", profileImageURL = "", profileImage = ""}
end
local function scoreList(own, red, chuck)
    local list = {entry("-1", "Red", red, true), entry("-2", "Chuck", chuck, true), entry(player.id, player.name, own, false)}
    table.sort(list, function(a, b) if a.points ~= b.points then return a.points > b.points end return a.isOwnPlayer end)
    local ownRank = 1
    for i, e in ipairs(list) do
        e.rank = i
        e.tournamentPrize = PODIUM_COINS[i] or 0
        if e.isOwnPlayer then ownRank = i end
    end
    return list, ownRank
end
local function weekList(week)
    local own, red, chuck = 0, 0, 0
    for _, code in ipairs(weekCodes(week)) do
        local l = starLimits(code)
        own = own + (state().scores[code] or 0)
        red = red + l.goldScore
        chuck = chuck + l.silverScore
    end
    return scoreList(own, red, chuck)
end

-- the league: twenty players a week, the player and nineteen bots of the league the player's rating puts them in. A bot's
-- week total is a share of the week's three-star scores (the same every time for that bot and week, a little higher in a
-- higher league). The top five move the rating up, the bottom five down; first, second and third get the league's prize.
-- The player joins with their first tournament level.
local LEAGUES = {
    {id = "GLASS", ratingThreshold = 0, reward = 20, leagueStars = 0}, {id = "WOOD", ratingThreshold = 500, reward = 30, leagueStars = 0},
    {id = "STONE", ratingThreshold = 1000, reward = 40, leagueStars = 0}, {id = "BRONZE", ratingThreshold = 1500, reward = 60, leagueStars = 0},
    {id = "SILVER", ratingThreshold = 2000, reward = 80, leagueStars = 0}, {id = "GOLD", ratingThreshold = 2500, reward = 100, leagueStars = 0},
    {id = "DIAMOND", ratingThreshold = 3000, reward = 150, leagueStars = 0},
}
local LEAGUE_NAMES = {GLASS = "Glass", WOOD = "Wood", STONE = "Stone", BRONZE = "Bronze", SILVER = "Silver", GOLD = "Gold", DIAMOND = "Diamond"}
local BOTS = {"Terence", "Bomb", "Matilda", "Hal", "Bubbles", "Stella", "Jay", "Jake", "Jim", "Silver", "Willow", "Poppy", "Luca", "Dahlia",
              "Gale", "Foreman", "Corporal", "Leonard", "Ross"}
local function leagueFor(rating)
    local found = LEAGUES[1]
    for _, l in ipairs(LEAGUES) do if rating >= l.ratingThreshold then found = l end end
    local out = {}
    for k, v in pairs(found) do out[k] = v end
    out.name = LEAGUE_NAMES[found.id]
    return out
end
local function share(week, i)                                            -- a fixed number between 0 and 1 for a week and a bot
    local x = math.sin(week * 12.9898 + i * 78.233) * 43758.5453
    return x - floor(x)
end
local function leagueStandings(week, rating, own, final)
    -- the bots play through the week: their totals grow with the time gone (all of it once the week is over)
    local gone = final and 1 or math.min(1, 0.1 + 1.1 * (os.time() - (EPOCH + week * WEEK)) / WEEK)
    local tier = 1
    for i, l in ipairs(LEAGUES) do if rating >= l.ratingThreshold then tier = i end end
    local gold = 0
    for _, code in ipairs(weekCodes(week)) do gold = gold + starLimits(code).goldScore end
    local list = {}
    for i, name in ipairs(BOTS) do
        local points = floor(gold * gone * (0.35 + 0.08 * tier + 0.6 * share(week, i)) / 10) * 10
        list[#list + 1] = {id = "bot" .. i, nickname = name, firstName = name, points = points, isOwnPlayer = false, isFreeSlot = false,
                           profileImage = "FRIEND_LIST_FRIEND_NO_IMAGE", profileImageURL = "", leagueStars = 0}
    end
    local s = state()
    list[#list + 1] = {id = player.id, nickname = s.nickname or player.name, firstName = player.name, points = own, isOwnPlayer = true, isFreeSlot = false,
                       profileImage = s.profileImage or "FRIEND_LIST_FRIEND_NO_IMAGE", profileImageURL = "", leagueStars = 0}
    table.sort(list, function(a, b) if a.points ~= b.points then return a.points > b.points end return a.isOwnPlayer end)
    local reward, ownRank = leagueFor(rating).reward, #list
    for i, e in ipairs(list) do
        e.rank = i
        e.leaguePrize = i == 1 and reward or i == 2 and floor(reward / 2) or i == 3 and floor(reward / 4) or 0
        e.promotion = (i <= 5 and tier < #LEAGUES) and 1 or (i > #list - 5 and tier > 1) and -1 or 0
        if e.isOwnPlayer then ownRank = i end
    end
    return list, ownRank
end
local function weekOwnTotal()
    local own = 0
    for _, code in ipairs(weekCodes(state().week or weekNumber())) do own = own + (state().scores[code] or 0) end
    return own
end
local function inLeague() local s = state() return s.leagueJoined == true or next(s.scores) ~= nil end

-- a new week: last week's standings become the results to claim (coins for the podium), the scores start again
local function rollover()
    local s, week = state(), weekNumber()
    if s.week == nil then s.week = week save() return end
    if s.week >= week then return end
    local played = false
    for _ in pairs(s.scores) do played = true end
    if played then
        local players, rank = weekList(s.week)
        s.lastResult = {players = players, rank = rank, prizes = PODIUM_COINS}
        s.lastClaimed = false
        local rating = s.rating or 0
        local standings, leagueRank = leagueStandings(s.week, rating, weekOwnTotal(), true)
        local before = leagueFor(rating)
        s.rating = max(0, rating + (leagueRank <= 5 and 250 or leagueRank > #standings - 5 and -250 or 0))
        standings[leagueRank].leagueRatingChange = s.rating - rating
        standings[leagueRank].leagueRating = s.rating
        local nextLeague
        for _, l in ipairs(LEAGUES) do if not nextLeague and l.ratingThreshold > s.rating then nextLeague = leagueFor(l.ratingThreshold) end end
        -- the results page: the league before and after the week, the next one up (its progress bar), and the standings
        s.lastLeague = {standings = standings, rank = leagueRank, prize = standings[leagueRank].leaguePrize, previousLeague = before,
                        currentLeague = leagueFor(s.rating), nextLeague = nextLeague}
        s.leagueJoined = true
    end
    s.week = week
    s.scores = {}
    folderLevels = nil
    save()
end
local function results(claimable)
    local s = state()
    local r = s.lastResult
    if not r then return {players = {}, prizes = {}, goldTrophies = s.trophies[1], silverTrophies = s.trophies[2], bronzeTrophies = s.trophies[3]} end
    return {players = r.players, prizes = claimable and r.prizes or {}, goldTrophies = s.trophies[1], silverTrophies = s.trophies[2], bronzeTrophies = s.trophies[3]}
end
local function online() return session == "Guest" end

local service = module("service", {
    loadTournament = function(progress, done) rollover() if type(progress) == "function" then progress(0, 1) end later(done) end,
    testNetwork = function(cb) later(cb) end,                          -- cb(error): nil, the network is there
    getDurationSinceNow = function(t) return math.max(0, (tonumber(t) or 0) - os.time()) end,
    getTimeFromNow = function(seconds) return os.time() + (tonumber(seconds) or 0) end,
    getTournamentBrand = function() return {path = "", name = ""} end,
    getCampaignId = function() return "" end,
    getAppDomain = function() return "" end,
    getItemServerName = function(name) return name end,
    getVideoRewardAmount = function() return 0 end,
    getAnalyticsSessionId = function() return "" end,
    scheduleLocalNotification = function() end,
    removeLocalNotification = function() end,
    refreshAnalyticsSession = function() end,
    tryAppraterActivation = function() end,
    loadImage = function() end,
    toggleZoom = function() end,
})

local social = module("social", {
    getSessionType = function() return session end,
    isOnlineSession = function() return online() end,
    hasToken = function() return false end,                           -- no Facebook: "Play Tournament" logs in as a guest
    hasPermission = function() return false end,
    resolveGuestIdentity = function(cb) later(cb) end,
    resolveFacebookIdentity = function(cb) later(cb, {message = "Facebook is not available."}) end,
    openGuestSession = function(cb)
        session = "Guest"
        episodes("Tournament")
        local e = type(env.g_episodes) == "table" and env.g_episodes[1]
        if type(e) == "table" then allLevels() e.folder_name = levelFolder end
        rollover()
        local s = state()
        if not s.welcome then s.welcome = true give("credits", WELCOME_COINS) save() end
        later(cb)
    end,
    openFacebookSession = function(cb) later(cb, {message = "Facebook is not available."}) end,
    openTutorialSession = function(cb) session = "Tutorial" episodes("Tutorial") later(cb) end,
    openPracticeSession = function(cb) session = "Practice" episodes("Practice") later(cb) end,
    closeSession = function() session = "Uninitialized" episodes("Tournament") end,
    logoutSession = function(cb) session = "Uninitialized" episodes("Tournament") later(cb) end,
    loginServer = function(cb) later(cb) end,
    getPendingRequests = function(cb) later(cb, nil, {}) end,
    getInvitableFriends = function(_, cb) later(cb, {}) end,
    enterTournament = function(cb) later(cb) end,
    leaveTournament = function(cb) later(cb) end,
    claimGifts = function(_, cb) later(cb, nil, {}) end,
    isUserGiftRequestExcluded = function() return true end,
    isUserInviteRequestExcluded = function() return true end,
    isUserChallengeRequestExcluded = function() return true end,
    loadProfilePicture = function() end,
})

local user = module("user", {
    getInfo = function() return player end,
    refreshInventory = function(cb) later(cb) end,
    getItem = function(name)                                             -- what the player owns (settingsWrapper:getItem asks here first)
        if not online() then return nil end
        local n = amount(name)
        if n <= 0 then return nil end
        return {id = name, quantity = n}
    end,
    hasBoughtSomething = function() return false end,
    hasClaimableBundle = function() return false end,
    claimBundle = function(_, cb) later(cb, {message = "Nothing to claim."}) end,
    isSubscriptionActive = function() return false end,
    getLeagueProfile = function(cb)                                      -- cb(error, {nickname, image, imageURL, useSocialName, rating, currentLeague, nextLeague})
        local s = state()
        local rating = s.rating or 0
        local current, nextLeague = leagueFor(rating), nil
        for _, l in ipairs(LEAGUES) do if not nextLeague and l.ratingThreshold > rating then nextLeague = leagueFor(l.ratingThreshold) end end
        later(cb, nil, {nickname = s.nickname or player.name, image = s.profileImage or "", imageURL = "", useSocialName = s.nickname == nil,
                        rating = rating, currentLeague = current, nextLeague = nextLeague or current})
    end,
    saveLeagueProfile = function(nickname, image, _, cb)
        local s = state()
        if type(nickname) == "string" and nickname ~= "" then s.nickname = nickname end
        if type(image) == "string" then s.profileImage = image end
        save()
        later(cb)
    end,
})

local tournament = module("tournament", {
    getLevels = function()
        rollover()
        local list = {}
        for i, code in ipairs(weekCodes(state().week or weekNumber())) do
            list[i] = {level = i, code = code, scoreLimits = starLimits(code), unlockTime = "0", locked = false}
        end
        return {levels = list, episode = 1, online = true}
    end,
    getTournamentId = function() return "WEEK" .. tostring(state().week or weekNumber()) end,
    getTimeLeft = function() rollover() return EPOCH + (weekNumber() + 1) * WEEK - os.time() end,
    getTournamentInfoText = function()                                  -- (the banner's flag: none, the line under the title: the time left)
        local left = max(0, EPOCH + (weekNumber() + 1) * WEEK - os.time())
        local d, h = floor(left / 86400), floor(left % 86400 / 3600)
        return nil, d > 0 and ("Ends in " .. d .. "d " .. h .. "h") or ("Ends in " .. h .. "h " .. floor(left % 3600 / 60) .. "m")
    end,
    getTournamentScores = function(cb) rollover() later(cb, nil, (weekList(state().week))) end,
    getPlayerScores = function(cb)
        local list = {}
        for _, code in ipairs(weekCodes(state().week or weekNumber())) do
            local points = state().scores[code]
            if points then
                local l = starLimits(code)
                list[#list + 1] = {level = code, points = points, stars = 0, rank = points >= l.goldScore and 1 or points >= l.silverScore and 2 or 3}
            end
        end
        later(cb, nil, list)
    end,
    getLevelScores = function(_, cb)
        local code = string.match(tostring(env.levelName), "^Level(.+)$")
        local l = starLimits(code or "")
        later(cb, nil, (scoreList(code and state().scores[code] or 0, l.goldScore, l.silverScore)))
    end,
    getLeagueLevelStandings = function(_, cb) later(cb, nil, nil, {}) end,   -- no league: (error, league info, standings)
    clearStandingsCache = function() end,
    checkDynamicPopups = function(cb) later(cb, {}) end,
    markDynamicPopupSeen = function() end,
    checkUnconcludedTournament = function(cb) later(cb, {}) end,
    getUnconcludedResults = function(cb)
        local s = state()
        if s.lastResult and not s.lastClaimed then later(cb, nil, results(true), s.lastLeague or {standings = {}}) else later(cb, nil, {players = {}, prizes = {}}, {standings = {}}) end
    end,
    getPreviousResults = function(cb) later(cb, nil, results(false), state().lastLeague or {standings = {}}) end,   -- (error, tournament, league)
    claimTournamentRewards = function(cb)
        local s = state()
        local coins = 0
        if s.lastResult and not s.lastClaimed then
            coins = PODIUM_COINS[s.lastResult.rank] or 0
            if s.lastResult.rank <= 3 then s.trophies[s.lastResult.rank] = s.trophies[s.lastResult.rank] + 1 end
            give("credits", coins)
            s.lastClaimed = true
            local leagueCoins = s.lastLeague and s.lastLeague.prize or 0
            if leagueCoins > 0 then give("credits", leagueCoins) end
            save()
            later(cb, nil, {{itemId = "credits", receivedQuantity = coins, totalQuantity = amount("credits") - leagueCoins}},
                  leagueCoins > 0 and {{itemId = "credits", receivedQuantity = leagueCoins, totalQuantity = amount("credits")}} or {})   -- (error, tournament rewards, league rewards)
            return
        end
        later(cb, nil, {{itemId = "credits", receivedQuantity = 0, totalQuantity = amount("credits")}}, {})
    end,
    getLeagueStandings = function(cb)                                    -- (error, the player's league, this week's standings); none before the first level
        if not inLeague() then later(cb, nil, nil, {}) return end
        local rating = state().rating or 0
        later(cb, nil, leagueFor(rating), (leagueStandings(state().week or weekNumber(), rating, weekOwnTotal())))
    end,
    getLeagueInfo = function()
        local list = {}
        for i, l in ipairs(LEAGUES) do list[i] = leagueFor(l.ratingThreshold) end
        return list
    end,
    -- the daily wheel: one spin a day; cb(error, day, the eight pieces, {index = the prize, 0 = none today}, remove a piece)
    -- after each spin the player takes one piece off for the next day's spin; a missed day (or two pieces left) puts them all back
    checkWheel = function(cb)
        local s = state()
        local d = s.wheel or {}
        s.wheel = d
        if d.last ~= today() and d.last ~= today(-86400) then d.removed = {} end
        d.removed = d.removed or {}
        local pieces, left = {}, {}
        for i = 1, #WHEEL do
            if not d.removed[i] then pieces[#pieces + 1] = {numericId = i, id = WHEEL[i][1], quantity = WHEEL[i][2]} left[#left + 1] = i end
        end
        local day = d.last == today() and (d.day or 1) or ((d.last == today(-86400)) and ((d.day or 0) % 7 + 1) or 1)
        if d.last == today() then later(cb, nil, day, pieces, {index = 0}, d.removePending == true) return end
        if d.pending == nil or d.pendingDay ~= today() or d.removed[d.pending] then d.pending, d.pendingDay = left[math.random(#left)], today() save() end
        later(cb, nil, day, pieces, {index = d.pending}, false)
    end,
    claimWheelReward = function(cb)
        local s = state()
        local d = s.wheel or {}
        if d.last ~= today() and d.pending then
            local prize = WHEEL[d.pending]
            give(prize[1], prize[2])
            d.day = (d.last == today(-86400)) and ((d.day or 0) % 7 + 1) or 1
            d.last, d.pending = today(), nil
            d.removePending = true
            s.wheel = d
            save()
        end
        later(cb)
    end,
    removeWheelItem = function(id, cb)
        local s = state()
        local d = s.wheel or {}
        d.removed = d.removed or {}
        local n = 0
        for i = 1, #WHEEL do if not d.removed[i] then n = n + 1 end end
        if n <= 2 then d.removed = {} elseif tonumber(id) then d.removed[tonumber(id)] = true end
        d.removePending = false
        s.wheel = d
        save()
        later(cb)
    end,
})

local shop = module("shop", {
    getItemCatalog = function(cb) later(cb, nil, {shops = CATALOG}) end,
    getCoinCatalog = function(cb) later(cb, {title = "Shop", message = "Coins cannot be bought here."}) end,
    getLevelCatalog = function(cb) later(cb, nil, {}) end,
    isLevelBought = function() return false end,
    setLevelBought = function() end,
    clearCatalog = function() end,
    isPaymentInitializing = function() return false end,
    inShop = function() end,
    shopOpenedFrom = function() end,
    getRandomVideoReward = function(cb) later(cb, {message = "No videos here."}) end,
    buyIAPProduct = function(_, cb) later(cb, {title = "Shop", message = "Nothing can be bought for money here."}) end,
    redeemCode = function(_, cb) later(cb, {title = "Shop", message = "Codes cannot be redeemed here."}) end,
    redeemVoucher = function(_, cb) later(cb, {title = "Shop", message = "Vouchers cannot be redeemed here."}) end,
    buyVirtualProduct = function(code, quantity, cb)                   -- pay with coins: cb(error, {{itemId, totalQuantity}})
        local price
        for _, page in ipairs(CATALOG) do
            for _, item in ipairs(page.items) do
                if item.id == code then for _, p in ipairs(item.prices) do if p.totalQuantity == quantity then price = p end end end
            end
        end
        if not price then later(cb, {title = "Shop", message = "That is not for sale."}) return end
        if amount("credits") < price.price then later(cb, {title = "Shop", message = "Not enough coins."}) return end
        give("credits", -price.price)
        local got = {}
        for _, name in ipairs(BUNDLE[code] or {code}) do
            give(name, quantity)                                  -- totalQuantity counts the free ones too
            got[#got + 1] = {itemId = name, totalQuantity = amount(name)}
        end
        save()
        later(cb, nil, got)
    end,
})

local game = module("game", {
    useItem = function(name, cb)                                         -- a power-up used up; with none bought it is free, as everywhere here
        if amount(name) > 0 then give(name, -1) save() end
        later(cb)
    end,
    submitScore = function(level, score, _, _, _, cb)                    -- the week's best score of a tournament level
        local code = string.match(tostring(level), "^Level(.+)$") or tostring(level)
        for _, c in ipairs(weekCodes(state().week or weekNumber())) do
            if c == code then
                local s = state()
                -- coins for each star that is new this week: 2 for the first, 4 for the second, 8 for the third (the level end shows them)
                if env.getStarCount then
                    local before = (s.scores[c] or 0) > 0 and env.getStarCount("Level" .. c, s.scores[c]) or 0
                    local now = env.getStarCount("Level" .. c, tonumber(score) or 0) or 0
                    for star = before + 1, now do give("credits", STAR_COINS[star] or 0) end
                end
                s.scores[c] = max(s.scores[c] or 0, tonumber(score) or 0)
                save()
            end
        end
        later(cb)
    end,
    sendBrag = function(_, cb) later(cb) end,
})

local natives = {
    service = service,
    social = social,
    user = user,
    tournament = tournament,
    shop = shop,
    game = game,
    gamerServices = module("gamerServices", {isSupported = function() return false end, isPlayerAuthenticated = function() return false end}),
    nativeAnalytics = module("nativeAnalytics", {logEvent = function() end}),
    skin = module("skin", {enableSkin = function() end, getImageName = function(name) return name end, isLoading = function() return false end, setSkinUpdateCallback = function() end}),
    -- the level's theme pictures: the engine's own theme layers (blockTable.themes), drawn where the game asks for them;
    -- createThemeSprite here is (name, sprite, x, y, speedX, scaleX, scaleY, angle, layer)
    LevelBackgrounds = module("LevelBackgrounds", {
        -- a 2.0 theme names its layers (backgroundLayers / foregroundLayers: sprite, parallax, scale, looping); they are
        -- given to the engine in the older order ({sheet, sprite, speed, scale, looping}) that Friends 1.0 used
        setTheme = function(theme)
            local t = env.blockTable and env.blockTable.themes and env.blockTable.themes[theme]
            if type(t) == "table" and t.bgLayers == nil then
                local function layers(list)
                    local out = {}
                    for i, l in ipairs(list or {}) do out[i] = {"", l.sprite, l.parallax or 0, l.scale or 1, l.looping ~= false} end
                    return out
                end
                t.bgLayers, t.fgLayers = layers(t.backgroundLayers), layers(t.foregroundLayers)
            end
            env.setTheme(theme)
        end,
        drawBackground = function() env.drawBackgroundNative() end,
        drawForeground = function() env.drawForegroundNative() end,
        createThemeSprite = function(name, sprite, x, y, speedX, scaleX, scaleY, angle, layer)
            env.createThemeSprite(name, sprite, x, y, scaleX or 1, scaleY or 1, angle or 0, speedX or 0, layer or 0)
        end,
        setThemeRectColour = function() end,
    }),
    DebugConsoleNative = module("DebugConsoleNative", {getInput = function() return "" end, activate = function() end, deactivate = function() end}),
}

-- the picture groups (native.ResourceManager): Assets.lua names each group's files (a sprite sheet .dat = 0, a set of
-- composite sprites = 1) with setGroup, then acquires and releases groups by name; a group is loaded on its first acquire
-- and freed when the last holder lets go
do
    local groups, holders = {}, {}
    local function each(name, sheet, compo)
        local g = groups[name]
        if not g then return end
        for _, f in ipairs(g.files) do
            local file = g.path .. "/" .. tostring(f[1])
            local fn = (f[2] == 0 and sheet) or (f[2] == 1 and compo)
            if fn then local ok, err = pcall(fn, file) if not ok then print("friends 2.0.0: " .. file .. ": " .. tostring(err)) end end
        end
    end
    local manager = {requiredTextureMemory = 0, usedTextureMemory = 0}
    manager.setMemoryLimit = function() end
    manager.setGroup = function(name, path, files) groups[name] = {path = path, files = files or {}} end
    manager.acquireGroup = function(name)
        holders[name] = (holders[name] or 0) + 1
        if holders[name] == 1 then each(name, res.createSpriteSheet, res.createCompoSpriteSet) end
    end
    manager.releaseGroup = function(name)
        if not holders[name] then return end
        holders[name] = holders[name] - 1
        if holders[name] <= 0 then holders[name] = nil each(name, res.releaseSpriteSheet, res.releaseCompoSpriteSet) end
    end
    manager.isGroupLoaded = function(name) return holders[name] ~= nil end
    if type(env.native) == "table" then env.native.ResourceManager = module("ResourceManager", manager) end
    -- the phone knew every group's sprites from the start (only the pictures waited for acquireGroup): a frame made before its group
    -- is taken (the game HUD's slingshot menu measures SLINGSHOT_TAB_LEFT / _BOTTOM_LEFT when it is built) gets the sizes from the files
    local peeked = {}
    local function peek(sprite)
        local known = peeked[sprite]
        if known then if known[1] then return known[1], known[2], known[3], known[4] end return end
        peeked[sprite] = {}
        for _, g in pairs(groups) do
            for _, f in ipairs(g.files) do
                if f[2] == 0 and env.res.abPeekSprite then
                    local w, h, px, py = env.res.abPeekSprite(g.path .. "/" .. tostring(f[1]), sprite)
                    if w then peeked[sprite] = {w, h, px, py} return w, h, px, py end
                end
            end
        end
    end
    local wrapped = false
    local function wrap()   -- done when the game names its first group: the game's res table is there by then
        local r = rawget(env, "res")
        if wrapped or type(r) ~= "table" or type(r.getSpriteBounds) ~= "function" then return end
        wrapped = true
        local bounds, pivot = r.getSpriteBounds, r.getSpritePivot
        r.getSpriteBounds = function(a, b)
            local w, h = bounds(a, b)
            if w == 0 and h == 0 then local pw, ph = peek(b or a) if pw then return pw, ph end end
            return w, h
        end
        r.getSpritePivot = function(a, b)
            local x, y = pivot(a, b)
            if x == 0 and y == 0 then local w, h, px, py = peek(b or a) if w then return px, py end end
            return x, y
        end
    end
    local setGroup = manager.setGroup
    manager.setGroup = function(...) wrap() return setGroup(...) end
end

-- the phone tells the game its screen size once it is known (g_updatedScreenWidth/Height); update() then sets
-- g_assetSizeX/Y (updateAssetSizes), which every menu scales by
env.g_updatedScreenWidth, env.g_updatedScreenHeight = env.screenWidth, env.screenHeight

-- a resized window: the phone's screen never changed size, so a page keeps the size it was built at (the login page / main
-- menu sets w, h = screenWidth, screenHeight once). The pages made at an earlier window size take the new one before the
-- game lays them out again (MenuManager:update does that when screenWidth changes, setRoot when a page is shown)
do
    local old = {}
    local function fit(frame, seen)
        if type(frame) ~= "table" or seen[frame] then return end
        seen[frame] = true
        local w, h = rawget(frame, "w"), rawget(frame, "h")
        if type(w) == "number" and type(h) == "number" and old[w .. "x" .. h] and (w ~= env.screenWidth or h ~= env.screenHeight) then
            frame.w, frame.h = env.screenWidth, env.screenHeight
        end
        local children = rawget(frame, "children")
        if type(children) == "table" then for _, c in pairs(children) do fit(c, seen) end end
    end
    env.abWindowResized = function(ow, oh)
        old[ow .. "x" .. oh] = true
        local m = env.menuManager
        if type(m) ~= "table" then return end
        if type(m.menus) == "table" then local seen = {} for _, f in pairs(m.menus) do fit(f, seen) end end
        if rawget(m, "setRoot") == nil and type(m.setRoot) == "function" then
            local setRoot = m.setRoot
            m.setRoot = function(self, frame, ...) fit(frame, {}) return setRoot(self, frame, ...) end
        end
    end
end

-- loadTableFromFile(file, name): a saved file (highscores.lua) read again into the global `name` (at a tournament rollover)
if env.loadTableFromFile == nil then
    env.loadTableFromFile = function(file, name)
        local ok, t = pcall(env.loadPersistentFileWrapper, file)
        if ok and type(t) == "table" then env[name] = t end
    end
end

-- text in several fonts (MultiFontText): the bitmap fonts here are taken to have every letter the text needs
if env.doesFontHaveRequiredGlyphs == nil then env.doesFontHaveRequiredGlyphs = function() return true end end
if env.splitMultiFontText == nil then env.splitMultiFontText = function(text, font) return {{text = text or "", font = font}} end end

-- its createCircle has no collision flag: (name, sprite, x, y, radius, density, friction, restitution, controllable, zOrder, 0)
env.abCircleNoCollision = true

-- a first start has no highscores.lua: the phone gives the game an empty table
if env.highscores == nil then env.highscores = {} end

-- the phone build (the engine's debug keys turn the cheats on later, with the game's DebugConsole.lua)
if rawget(env, "releaseBuild") == nil then env.releaseBuild = true end

-- behind the globals: the environment's (and the real global table's) fallback finds these when the name is not a global
local function behind(t)
    local mt = getmetatable(t)
    if not mt then mt = {} setmetatable(t, mt) end
    local old = mt.__index
    mt.__index = function(tbl, k)
        local v = natives[k]
        if v ~= nil then return v end
        if type(old) == "function" then return old(tbl, k) end
        if type(old) == "table" then return old[k] end
    end
end
behind(env)
if _G ~= env then behind(_G) end
