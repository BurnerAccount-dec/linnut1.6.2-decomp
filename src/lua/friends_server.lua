-- Angry Birds Friends: a stand-in for the game's server, so the online part plays on this computer.
--
-- The phone game talked to Rovio's and Facebook's servers for the login, the Weekly Tournament, the coins, the
-- power-up shop and the daily rewards (the native `facebook` table). None of that exists any more. This file answers
-- those calls locally: "Log in with Facebook" logs in a local player, the tournament runs on the levels the game ships
-- (levels/pack57, six a week, the week ending on Monday at midnight), and the score lists hold the player and the two
-- bots of the offline game: Red Bird scores each level's three-star score (goldScore) and the Yellow Bird its two-star
-- score (silverScore), and their tournament totals are the sums over the week's levels. Coins, power-ups bought with them,
-- trophies and the daily reward streak are kept in settings.lua (settings.abServer). Prices and rewards are made up: the
-- real ones came from the server.
--
-- Run by the engine inside the game's environment after its offline `facebook` stand-in is made (script_host.cpp).

local env = ...
local fb = _G.facebook
if type(fb) ~= "table" or not rawget(fb, "abOffline") then return end

local floor, max, min = math.floor, math.max, math.min

local CODES = {1, 2, 5, 6, 7, 8, 15, 16, 20, 24, 26, 27, 41, 56, 57, 58, 60, 61, 126, 128, 130, 131, 132, 133, 135, 136, 137, 139, 140, 155}
local LEVELS_PER_WEEK = 6
local WEEK = 7 * 24 * 60 * 60
local EPOCH = os.time({year = 2013, month = 4, day = 29, hour = 0})     -- a Monday at midnight: weeks run Monday to Monday
local LOGIN_GIFT = 200
local PODIUM_COINS = {30, 20, 10}
local STAR_COINS = {2, 4, 8}

-- the power-up shop (buy code -> the item names it gives) and its prices: tq = how many, p = coins, fq = how many free
local ITEMS = {BirdFood = {"superseed"}, ExtraSpeed = {"kingsling"}, LaserSight = {"slingscope"}, Earthquake = {"birdquake"},
               ExtraBird = {"extrabird"}, PowerupBundle = {"superseed", "kingsling", "slingscope", "birdquake"}}
local PRICES = {
    BirdFood = {{tq = 3, p = 30, fq = 0}, {tq = 10, p = 90, fq = 1}, {tq = 25, p = 200, fq = 5}},
    ExtraSpeed = {{tq = 3, p = 30, fq = 0}, {tq = 10, p = 90, fq = 1}, {tq = 25, p = 200, fq = 5}},
    LaserSight = {{tq = 3, p = 30, fq = 0}, {tq = 10, p = 90, fq = 1}, {tq = 25, p = 200, fq = 5}},
    Earthquake = {{tq = 3, p = 30, fq = 0}, {tq = 10, p = 90, fq = 1}, {tq = 25, p = 200, fq = 5}},
    ExtraBird = {{tq = 1, p = 30, fq = 0}, {tq = 5, p = 130, fq = 1}},
    PowerupBundle = {{tq = 3, p = 100, fq = 0}, {tq = 10, p = 300, fq = 2}},
}
local DAILY = {{day = 1, itemId = "BirdFood", quantity = 1}, {day = 2, itemId = "ExtraSpeed", quantity = 1}, {day = 3, itemId = "LaserSight", quantity = 1},
               {day = 4, itemId = "Earthquake", quantity = 1}, {day = 5, itemId = "VirtualCurrency", quantity = 20}}

local mode = "offline"
local playerUpdateCallback, timeoutCallback
local countdown                                      -- the tutorial tournament's last seconds: {ends = time, done = function}

-- a server answers later, never inside the call: the menus count on that (an answer given at once opened the results
-- window in the middle of the frame that made it, before it was set up). Answers wait for the next frame.
local queue, flushing = {}, false
local function flush()
    local jobs = queue
    queue = {}
    for _, job in ipairs(jobs) do
        local ok, err = pcall(job.fn, unpack(job.args, 1, job.n))
        if not ok then print("friends server: " .. tostring(err)) end
    end
end
local function later(fn, ...)
    if type(fn) ~= "function" then return end
    local engine = env.engine or _G.engine
    if not (engine and engine.on) then fn(...) return end
    if not flushing then flushing = true engine.on("update", flush) end
    queue[#queue + 1] = {fn = fn, args = {...}, n = select("#", ...)}
end

-- the saved state, in the game's own settings table
local function state()
    local settings = env.settings
    if type(settings) ~= "table" then return {coins = 0, scores = {}, trophies = {0, 0, 0}, daily = {}} end
    local s = settings.abServer
    if type(s) ~= "table" then s = {} settings.abServer = s end
    s.coins = s.coins or 0
    s.scores = s.scores or {}
    s.trophies = s.trophies or {0, 0, 0}
    s.daily = s.daily or {}
    return s
end
local function save()
    if env.saveLuaFileWrapper then pcall(env.saveLuaFileWrapper, "settings.lua", "settings", true) end
end
local function notify(name)
    local ev = env.friendsEvents and env.friendsEvents[name]
    if ev and env.eventManager then pcall(env.eventManager.notify, env.eventManager, {id = ev}) end
end

local function weekNumber() return floor((os.time() - EPOCH) / WEEK) end
local function weekCodes(week)
    local list, first = {}, (week % (#CODES / LEVELS_PER_WEEK)) * LEVELS_PER_WEEK
    for i = 1, LEVELS_PER_WEEK do list[i] = "2000-" .. CODES[first + i] end
    return list
end

-- (4.2) the coins: Friends keeps its own, or, when the player turns "share coins" on (the coin window, F10), uses the wallet that the Social account shares
local function shared() return state().shareCoins == true and type(env.getCoins) == "function" end
local function coinCount() if shared() then return env.getCoins() end return state().coins end
local function addCoins(n)
    if shared() then env.addCoins(n) else local s = state() s.coins = max(0, s.coins + n) end
    notify("EID_CREDIT_COUNT_UPDATED")
end
-- what the coin window asks for: the option itself and the two ways of moving coins across
env.abFriendsCoins = {
    shared = shared,
    setShared = function(on)
        local s = state()
        if on and not shared() then env.addCoins(s.coins) s.coins = 0 end          -- turning it on brings the Friends coins to the shared wallet
        s.shareCoins = on and true or false
        save() notify("EID_CREDIT_COUNT_UPDATED")
    end,
    toSocial = function() local s = state() local n = s.coins s.coins = 0 if n > 0 then env.addCoins(n) end save() notify("EID_CREDIT_COUNT_UPDATED") return n end,
    toFriends = function()
        local s = state()
        local n = type(env.getCoins) == "function" and env.getCoins() or 0
        if n > 0 then env.addCoins(-n) s.coins = s.coins + n end
        save() notify("EID_CREDIT_COUNT_UPDATED") return n
    end,
    friendsCoins = function() return state().coins end,
}
local function addItem(name, n)
    local wrapper = env.settingsWrapper
    if not (wrapper and wrapper.getItem) then return 0 end
    local item = wrapper:getItem(name)
    item.totalCount = (item.totalCount or 0) + n
    return item.totalCount - (item.usedCount or 0)
end
local function giveReward(itemId, quantity)
    if itemId == "VirtualCurrency" then addCoins(quantity) return end
    for _, name in ipairs(ITEMS[itemId] or {}) do addItem(name, quantity) end
end

-- the score lists: the player against the two bots, best first
local function scoreList(own, red, yellow)
    local list = {{id = "-1", name = "Red Bird", points = red, isBot = true}, {id = "-2", name = "Yellow Bird", points = yellow, isBot = true},
                  {id = "0", name = "Player", points = own, isOwnPlayer = true}}
    table.sort(list, function(a, b) if a.points ~= b.points then return a.points > b.points end return a.isOwnPlayer == true end)
    local ownRank = 1
    for i, e in ipairs(list) do
        e.rank = i e.stars = 0 e.invite = false e.gift = false
        e.isBot = e.isBot or false e.isOwnPlayer = e.isOwnPlayer or false
        e.firstName = e.name e.lastName = ""
        if e.isOwnPlayer then ownRank = i end
    end
    return list, ownRank
end
local function weekTotal()
    local total = 0
    for _, code in ipairs(weekCodes(state().week or weekNumber())) do total = total + (state().scores[code] or 0) end
    return total
end
local function botTotals(levels)       -- the bots' totals over a list of level names (Red Bird: three-star scores, Yellow Bird: two-star scores)
    local red, yellow = 0, 0
    for _, level in ipairs(levels) do local gold, silver = fb.starScores(level) red = red + gold yellow = yellow + silver end
    return red, yellow
end
local function tournamentList()
    local levels = {}
    for i, code in ipairs(weekCodes(state().week or weekNumber())) do levels[i] = "Level" .. code end
    return scoreList(weekTotal(), botTotals(levels))
end

-- a new week: the last one's standings become the results (trophies and coins for the podium), the scores start again
local function rollover()
    local s, week = state(), weekNumber()
    if s.week == nil then s.week = week return end
    if s.week >= week then return end
    if weekTotal() > 0 then
        local players, rank = tournamentList()
        local reward = PODIUM_COINS[rank] or 0
        if rank <= 3 then s.trophies[rank] = s.trophies[rank] + 1 end
        local result = {valid = true, ownRank = rank, ownPoints = weekTotal(), players = players,
                        goldTrophies = s.trophies[1], silverTrophies = s.trophies[2], bronzeTrophies = s.trophies[3],
                        prizeCounts = {s.trophies[1], s.trophies[2], s.trophies[3], 0, 0},
                        rewardItemId = reward > 0 and "VirtualCurrency" or "", rewardQuantity = reward}
        if reward > 0 then addCoins(reward) end
        s.lastResult = result
        s.lastResultSeen = false
    end
    s.week = week
    s.scores = {}
    save()
    later(timeoutCallback)
end

-- a cleared tournament level counts for this week's standings
local hooked = false
local listener = {name = "abFriendsServer"}
function listener:eventTriggered(event)
    if not (env.events and event.id == env.events.EID_LEVEL_ENDED and event.levelComplete and mode == "online") then return end
    local code = string.match(tostring(event.level), "Level([%w%-]+)$")
    for _, c in ipairs(weekCodes(state().week or weekNumber())) do
        if c == code then
            local s = state()
            -- coins for the stars that are new this week: 2 for the first, 4 for the second, 8 for the third (the level end screen shows them)
            if env.getStarCount then
                local before = (s.scores[c] or 0) > 0 and env.getStarCount(event.level, s.scores[c]) or 0
                local now = env.getStarCount(event.level, event.score or 0) or 0
                for star = before + 1, now do addCoins(STAR_COINS[star] or 0) end
            end
            s.scores[c] = max(s.scores[c] or 0, event.score or 0)
            save()
            later(playerUpdateCallback)
        end
    end
end
local function hook()
    if hooked or not (env.eventManager and env.events and env.events.EID_LEVEL_ENDED) then return end
    hooked = true
    env.eventManager:addEventListener(env.events.EID_LEVEL_ENDED, listener)
end

-- the session
fb.testNetwork = function() return true end
fb.hasToken = function() return state().login == true end
fb.isFacebookSessionOpen = function() return mode == "online" end
fb.isOnlineMode = function() hook() return mode == "online" end
fb.isUnknownMode = function() return false end
local function logIn(cb)
    local s = state()
    s.login = true
    mode = "online"
    rollover()
    save()
    later(cb, true)
end
-- (4.2) "Log in with Facebook" looks for the player's Social account (the name in socialLevels.player) on the Social server. No network at all is the game's own network
-- error; a network without the account (or without the server) gets its own window, which lets the player go on as a local player.
fb.openFacebookSession = function(cb)
    local found = env.abFindSocialAccount and env.abFindSocialAccount()
    if found == nil or found == "found" or found == "unchecked" then logIn(cb) return end
    if found == "nonetwork" then later(cb, false, "offline") return end
    local name = env.abSocialName and env.abSocialName()
    local lines = found == "nosocial"
        and {"No Angry Birds Social account", name and ("named " .. name .. " was found.") or "is set for this player.", "Make one on the Social server,", "or play as a local player."}
        or found == "oldserver" and {"The Social server that is running is", "an older copy without Friends logins.", "Start social-server\\start.bat in the", "engine's folder, or play as a local player."}
        or {"The Social server does not answer,", "so no Social account was found.", "Start it, or play as a local player."}
    env.abNotice(lines, {{"cancel", "ESCAPE"}, {"LOCAL PLAYER", "LOCAL"}}, function(i)
        if i == 2 then logIn(cb) else later(cb, false, "cancelled") end
    end)
end
fb.logoutFromFacebook = function(cb)
    state().login = false
    mode = "offline"
    save()
    later(cb)
end
fb.leaveCurrentGameSession = function() mode = "offline" end
fb.startOfflineSession = function(cb) mode = "offline" if type(cb) == "function" then cb() end end
fb.getStoredUser = function() return {id = "0", firstName = "Player", lastName = ""} end
fb.getOwnUserInfo = function() return {id = "0", name = "Player", first_name = "Player"} end
fb.refreshTournament = function(cb) rollover() later(cb, true) end
fb.initTournament = fb.refreshTournament
fb.useDynamicLevelLoading = function() return mode == "online" end   -- the main menu reads the week's star scores from the level files

-- the tournament
local offlineLevels = fb.getTournamentLevels
fb.getTournamentLevels = function()
    hook()
    local ids = env.g_episodeIds
    if mode ~= "online" or not (type(ids) == "table" and ids[1] == 1) then return offlineLevels() end
    rollover()
    local list = {}
    for i, code in ipairs(weekCodes(state().week)) do
        list[i] = {code = code, level = i, locked = false, new = false, points = state().scores[code] or 0, rank = 0}
        local hs = env.highscores
        if type(hs) == "table" and type(rawget(hs, "Level" .. code)) ~= "table" then rawset(hs, "Level" .. code, {score = 0, eagleScore = 0}) end
    end
    return {episode = 1, online = true, levels = list}
end
fb.getTournamentTime = function()
    local left
    if countdown then
        left = countdown.ends - os.time()
        if left <= 0 then local done = countdown.done countdown = nil if done then done() end left = 0 end
    else
        rollover()
        left = EPOCH + (weekNumber() + 1) * WEEK - os.time()
    end
    return {secondsLeft = left, minutesLeft = floor(left / 60), hoursLeft = floor(left / 3600), daysLeft = floor(left / 86400)}
end
fb.getTournamentScoreCount = function() return 3 end
fb.getTournamentScores = function()
    if mode == "online" then rollover() return (tournamentList()) end
    local own, levels = 0, {}
    for i, l in ipairs(offlineLevels().levels) do
        local e = type(env.highscores) == "table" and rawget(env.highscores, "Level" .. l.code)
        own = own + ((type(e) == "table" and e.score) or 0)
        levels[i] = "Level" .. l.code
    end
    return (scoreList(own, botTotals(levels)))
end
local offlineLevelScores = fb.getLevelScores
fb.getLevelScores = function(cb)
    hook()
    local code = string.match(tostring(fb.currentLevel), "Level([%w%-]+)$")
    if mode == "online" and code and state().scores and type(cb) == "function" then
        for _, c in ipairs(weekCodes(state().week or weekNumber())) do
            if c == code then
                local list, rank = scoreList(state().scores[c] or 0, fb.starScores("Level" .. c))
                cb(true, fb.currentLevel, list, rank)
                return
            end
        end
    end
    return offlineLevelScores(cb)
end
fb.setTournamentPlayerUpdateCallback = function(cb) playerUpdateCallback = cb end
fb.removeTournamentPlayerUpdateCallback = function() playerUpdateCallback = nil end
fb.setTournamentTimeoutCallback = function(cb) timeoutCallback = cb end
fb.removeTournamentTimeoutCallback = function() timeoutCallback = nil end
fb.hasLastTournamentResult = function() local s = state() return s.lastResult ~= nil and not s.lastResultSeen end
fb.getLastTournamentResult = function() local s = state() s.lastResultSeen = true save() return s.lastResult end
fb.hasPreviousWeeksTournamentResult = function() return state().lastResult ~= nil end
fb.getPreviousWeeksTournamentResult = function() return state().lastResult or {valid = false} end
fb.postTournamentResults = function(rank, points, cb) later(cb, true) end

-- coins, the power-up shop and the welcome gift
fb.getCreditCount = function() return coinCount() end
fb.checkCreditCountChange = function() return 0 end
fb.hasBoughtSomething = function() return false end
-- (4.3) the coin shop: with a network connection it works like the coin window's fake ad, each of the six packages is paid with an ad
local COIN_PACKS = {{1700, 400}, {780, 170}, {360, 80}, {170, 30}, {80, 10}, {30, 0}}
local function netUp() return type(env.isNetworkConnected) ~= "function" or env.isNetworkConnected() end
fb.isCoinShopReady = function() return netUp() and type(env.watchAd) == "function" end
fb.getCreditShopListing = function(cb)
    if not netUp() then later(cb, false) return end
    local list = {}
    for i, c in ipairs(COIN_PACKS) do list[i] = {id = "ab.coins." .. i, price = "Free (ad)", topUpValue = c[1] + c[2]} end
    later(cb, true, list)
end
fb.buyCredits = function(id, cb)                           -- cb(1: bought, product id) | cb(0, message)
    local n = tonumber(tostring(id):match("(%d+)$"))
    local c = n and COIN_PACKS[n]
    if not c then later(cb, 0, "That is not for sale.") return end
    if not netUp() or type(env.watchAd) ~= "function" then later(cb, 0, "No internet connection.") return end
    env.watchAd(function(reward)
        if (reward or 0) <= 0 then later(cb, 0, "Cancelled.") return end
        if type(env.addCoins) == "function" then env.addCoins(-reward) end   -- the ad's reward goes to the Social wallet, the package to the Friends coins
        addCoins(c[1] + c[2])
        save()
        notify("EID_CREDIT_COUNT_UPDATED")
        later(cb, 1, id)
    end)
end
fb.hasClaimableBundle = function(name) return name == "MobileLoginPack" and not state().loginGift end
fb.claimBundle = function(name, cb)
    local ok = name == "MobileLoginPack" and not state().loginGift
    if ok then state().loginGift = true addCoins(LOGIN_GIFT) save() end
    later(cb, name, ok)
end
fb.getShopListing = function(cb)
    local bundles = {}
    for id, prices in pairs(PRICES) do bundles[#bundles + 1] = {id = id, hidden = false, prices = prices} end
    later(cb, {categories = {{name = "commodities", bundles = bundles}}})
end
fb.buyWithCredits = function(code, quantity, cb)
    local price
    for _, p in ipairs(PRICES[code] or {}) do if p.tq == quantity then price = p end end
    if not price then later(cb, false, nil, "That is not for sale.") return end
    if coinCount() < price.p then later(cb, false, nil, "Not enough coins.") return end
    addCoins(-price.p)
    local items = {}
    for _, name in ipairs(ITEMS[code]) do items[name] = addItem(name, price.tq + (price.fq or 0)) end
    save()
    later(cb, true, items)
end
fb.validatePowerup = function(name, cb)     -- a bought power-up is used up; with none bought the power-up is free, as before
    local wrapper = env.settingsWrapper
    if wrapper and wrapper.getItem then
        local item = wrapper:getItem(name)
        if (item.totalCount or 0) - (item.usedCount or 0) > 0 then item.usedCount = (item.usedCount or 0) + 1 save() end
    end
    if type(cb) == "function" then cb(true) end
end

-- the daily reward: one a day, a five-day streak that starts again when a day is missed
local function today(offset) return tonumber(os.date("%Y%m%d", os.time() + (offset or 0))) end
local function streakDay()
    local d = state().daily
    if d.last == today(-86400) then return (d.day or 0) % #DAILY + 1 end
    return 1
end
fb.hasDailyReward = function() return mode == "online" and state().daily.last ~= today() end
fb.getDailyRewardInfo = function() return {schedule = DAILY, dayInSchedule = streakDay(), rewardGivenNow = true} end
fb.flushDailyReward = function()
    local s = state()
    if s.daily.last == today() then return end
    local day = streakDay()
    giveReward(DAILY[day].itemId, DAILY[day].quantity)
    s.daily = {last = today(), day = day}
    save()
end
fb.refreshDailyRewardInfo = function(cb) later(cb, true) end

-- the tutorial tournament's countdown (the main menu shows it before the tutorial ends)
local tutorial = _G.tutorial
if type(tutorial) == "table" and rawget(tutorial, "startCountDown") then
    local finish = tutorial.startCountDown
    tutorial.startCountDown = function(seconds, done)
        countdown = {ends = os.time() + (tonumber(seconds) or 0), done = function()
            -- the tutorial tournament's results ("Tutorial Tournament - Congratulations!" on the main menu): the four tutorial levels against the bots
            local own = 0
            for i = 1, 4 do
                local e = type(env.highscores) == "table" and rawget(env.highscores, "LevelTutorial" .. i)
                own = own + ((type(e) == "table" and e.score) or 0)
            end
            local s = state()
            local players, rank = scoreList(own, botTotals({"LevelTutorial1", "LevelTutorial2", "LevelTutorial3", "LevelTutorial4"}))
            s.lastResult = {valid = true, ownRank = rank, ownPoints = own, players = players, goldTrophies = s.trophies[1], silverTrophies = s.trophies[2],
                            bronzeTrophies = s.trophies[3], prizeCounts = {s.trophies[1], s.trophies[2], s.trophies[3], 0, 0}, rewardItemId = "", rewardQuantity = 0}
            s.lastResultSeen = false
            finish(0, done)
        end}
    end
end
