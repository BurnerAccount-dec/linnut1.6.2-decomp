-- Coins, the fake ad and the language list, for every game. Run by the engine inside the game's environment (script_host.cpp), after achievements_page.lua.
--
--   * A wallet of coins (wallet.lua in the user data, saved at once, so nothing is lost when the game or the Social server is closed). When the Social stand-in
--     server (social-server/) answers, the wallet is shared with it: the coins are kept there too, and once a Social account is made with the name in
--     socialLevels.player (extras/extra_scripts.lua), the coins earned before the account existed join the account. Offline, the wallet just works on its own.
--   * The fake ad: no video, a few seconds of a placeholder screen, then coins. F10 opens the coin window (watch an ad, sync now); a script calls watchAd(callback).
--   * buyWithCoins(price [, callback]) is the in-app purchase: it takes the price from the wallet when there is enough and answers true / false.
--   * The language list (F9, or showLanguages() from a script): every language the game's text files carry. The choice is saved (language.txt).
-- Banner and interstitial ad calls of the phone games (showAd, hideAd, showAdvertisement, requestAd) stay silent: an ad only plays when the player asks for one.

local env = ...
if env.abCoins then return end

local floor, max, min = math.floor, math.max, math.min
local select = select
-- the numbers a script can change (abCoinsConfig.adReward = 25, ...): coins for one ad, how long the placeholder screen stays, the seconds between two syncs with the server,
-- and the wait after one that failed
local cfg = {adReward = 10, adSeconds = 3, syncEvery = 30, retryAfter = 120}
env.abCoinsConfig = cfg
local function hook(name, ...)          -- onCoinsChanged(balance, change), onAdWatched(reward), onLanguageChanged(code): global functions of the game's scripts, if it has them
    local f = env[name]
    if type(f) == "function" then pcall(f, ...) end
end

local function load(file, name)
    local ok, t = pcall(function() return env.loadPersistentFileWrapper and env.loadPersistentFileWrapper(file) end)
    if ok and type(t) == "table" and type(t[name]) == "table" then return t[name] end
    return {}
end
local wallet = load("wallet.lua", "abWallet")
if not wallet.id then
    local id = ""
    math.randomseed(os.time() + floor(os.clock() * 1000))
    for _ = 1, 16 do id = id .. string.format("%x", math.random(0, 15)) end
    wallet.id, wallet.coins, wallet.pending = id, 0, 0
end
wallet.coins, wallet.pending = wallet.coins or 0, wallet.pending or 0
local function save()
    env.abWallet = wallet
    if env.savePersistentLuaFile then pcall(env.savePersistentLuaFile, "wallet.lua", "abWallet") end
end

-- ---------------------------------------------------------------------------------------------------------------- the Social server
local state = {status = "never", clock = 0, nextSync = 5}     -- status: never / online / offline
local function urlEncode(s) return (string.gsub(tostring(s), "[^%w%-_%.]", function(c) return string.format("%%%02X", string.byte(c)) end)) end
-- (4.4) a game without socialLevels (Friends from an .apk / .ipa has no extra_scripts.lua) reads socialPlayer / socialServer from config.lua beside the exe
local engineCfg
local function engineConfig()
    if engineCfg == nil then
        engineCfg = {}
        local f = loadfile and loadfile("config.lua")
        if f then
            local t = {}
            setfenv(f, t)
            if pcall(f) then engineCfg = t end
        end
    end
    return engineCfg
end
local function social()
    local s = type(env.socialLevels) == "table" and env.socialLevels
    if not s then
        local c = engineConfig()
        s = {server = type(c.socialServer) == "string" and c.socialServer or nil, player = type(c.socialPlayer) == "string" and c.socialPlayer or nil}
    end
    local name = s.player
    if name == "Player" or name == "" then name = nil end        -- the default name is nobody's account
    return s.server or "http://localhost", name
end
local function syncQuery()
    local server, player = social()
    return server, "/classic/wallet?id=" .. wallet.id .. "&player=" .. urlEncode(player or "") .. "&delta=" .. floor(wallet.pending) .. "&coins=" .. floor(wallet.coins), floor(wallet.pending)
end
-- what the server said (or why it did not): true when the wallet was synced. sent is the change that was sent with the question; coins earned since stay pending
local function syncDone(server, body, why, sent)
    state.why = nil
    local coins, account
    if body then coins, account = string.match(body, "^ok\t(%d+)\t([^\n]*)") end
    if not coins then
        state.why = body and "It runs an older version: restart it from social-server/" or (server .. ": " .. tostring(why))
        state.status, state.nextSync = "offline", state.clock + cfg.retryAfter
        return false
    end
    wallet.pending = wallet.pending - sent
    wallet.coins = tonumber(coins) + wallet.pending
    wallet.account = account ~= "" and account or nil
    state.status, state.nextSync = "online", state.clock + cfg.syncEvery
    save()
    return true
end
local asked                                -- the question that is on its way (4.1.2.1: the regular check never waits for the answer, a server that does not answer used to hold the game still for seconds)
local function sync()                      -- asked for by the player (watching an ad, S in the coin window): waits for the answer, a second and a half at most
    asked = nil
    if type(env.httpGet) ~= "function" then return end
    local server, query, sent = syncQuery()
    local body, why = env.httpGet(server .. query, 1.5)
    if not body and string.find(server, "//localhost", 1, true) then         -- "localhost" may lead to an address the server does not listen on
        body, why = env.httpGet((string.gsub(server, "//localhost", "//127.0.0.1", 1)) .. query, 1.5)
    end
    return syncDone(server, body, why, sent)
end
local function syncInBackground()          -- the regular check: asks on its own thread and looks at the answer in the frames that follow
    if type(env.httpGetAsync) ~= "function" then return sync() end
    if asked == nil then
        local server, query, sent = syncQuery()
        asked = {server = server, query = query, sent = sent, id = env.httpGetAsync(server .. query, 1.5)}
        state.nextSync = state.clock + 1e9                     -- not again before this one is answered
        return
    end
    local function pack(...) return select("#", ...), ... end
    local n, body, why = pack(env.httpPoll(asked.id))
    if n == 0 then return end                                  -- still on its way
    if not body and not asked.second and string.find(asked.server, "//localhost", 1, true) then
        asked.second = true
        asked.id = env.httpGetAsync((string.gsub(asked.server, "//localhost", "//127.0.0.1", 1)) .. asked.query, 1.5)
        return
    end
    local a = asked
    asked = nil
    syncDone(a.server, body, why, a.sent)
end

-- (4.2) Friends' login: is there a Social account? "found", "nosocial" (the server answers, no such account), "noserver", "nonetwork" (no connection at all)
-- or "unchecked" (no name is set in socialLevels.player: nothing to look for)
env.abSocialName = function() local _, player = social() return player end
env.abFindSocialAccount = function()
    if env.isNetworkConnected and not env.isNetworkConnected() then return "nonetwork" end
    local server, player = social()
    if type(env.httpGet) ~= "function" then return "unchecked" end
    player = player or wallet.account
    -- (4.4) no name set (Friends has no socialLevels): the wallet's own sync asks the server, so a running server is found, and an account the
    -- wallet is already shared with counts; the server answering without an account is "nosocial" (the game said "unchecked" and never asked)
    if not player then
        if not sync() then return "noserver" end
        return wallet.account and "found" or "nosocial"
    end
    local url = "/classic/account?player=" .. urlEncode(player)
    local body = env.httpGet(server .. url, 1.5)
    if not body and string.find(server, "//localhost", 1, true) then body = env.httpGet((string.gsub(server, "//localhost", "//127.0.0.1", 1)) .. url, 1.5) end
    if not body then return "noserver" end
    local what = string.match(body, "^ok\t(%a+)")
    if what == "found" then return "found" elseif what == "none" then return "nosocial" end
    return "noserver"
end
-- a window with a message and one or two buttons: callback(1) for the first (Esc), callback(2) for the second
local notice
env.abNotice = function(lines, buttons, callback)
    notice = {lines = lines, buttons = buttons, callback = callback}
    env.abPage.mode = "notice"
end

-- ---------------------------------------------------------------------------------------------------------------- the script functions
env.getCoins = function() return wallet.coins end
env.addCoins = function(n)
    n = floor(tonumber(n) or 0)
    wallet.coins = max(0, wallet.coins + n)
    wallet.pending = wallet.pending + n
    save()
    if n ~= 0 then hook("onCoinsChanged", wallet.coins, n) end
    return wallet.coins
end
env.spendCoins = function(n)
    n = floor(tonumber(n) or 0)
    if n < 0 or wallet.coins < n then return false end
    env.addCoins(-n)
    return true
end
-- the in-app purchase: the game's "You are about to buy ..." window, paid from the wallet. callback(true) after BUY with enough coins, callback(false) otherwise.
-- spendCoins(price) does the same without the window.
env.buyWithCoins = function(price, callback, item)
    env.abBuy = {price = floor(tonumber(price) or 0), callback = callback, item = item}
    env.abPage.mode = "buy"
end
env.abCoins = wallet

-- ---------------------------------------------------------------------------------------------------------------- the windows
local page = {mode = nil}           -- "coins", "ad", "languages" or "buy"
env.abPage = page
local ad = {left = 0, done = nil, note = ""}
local names = {en = "English", fr = "French", it = "Italian", de = "German", es = "Spanish", zh_TW = "Chinese (Traditional)", zh_CN = "Chinese (Simplified)", ja = "Japanese",
               pt = "Portuguese", ru = "Russian", ko = "Korean", nl = "Dutch", sv = "Swedish", pl = "Polish", tr = "Turkish", fi = "Finnish", da = "Danish", nb = "Norwegian"}
local function languageName(code)
    return names[string.sub(code, 1, 5)] or names[string.sub(code, 1, 2)] or code
end
local languages = {}
local selected = 1
local rects = {}
local drawBuy

local function openLanguages()
    languages = _G.res.getLocales and _G.res.getLocales() or {}
    selected = 1
    local now = _G.res.getLocale and _G.res.getLocale() or ""
    for i, code in ipairs(languages) do if code == now then selected = i end end
    page.mode = "languages"
end
env.showLanguages = openLanguages
env.showCoins = function() page.mode = "coins" end
-- An ad plays only while the computer is on a network (isNetworkConnected: it has an address that reaches beyond itself, Wi-Fi or cable), checked when the ad is asked for
-- (a second at most). The Social stand-in server is not needed: the coins wait in the wallet until it answers.
env.isSocialServerReachable = function() return sync() and true or false end
env.isOnline = function() return (env.isNetworkConnected == nil or env.isNetworkConnected()) and env.isSocialServerReachable() end
local networkAt, networkIs = -1e9, false
env.getWalletInfo = function()
    if state.clock - networkAt > 10 then                       -- trying a connection takes a moment: not on every call
        networkIs, networkAt = env.isNetworkConnected and env.isNetworkConnected() or false, state.clock
    end
    return {coins = wallet.coins, pending = wallet.pending, id = wallet.id, account = wallet.account, server = state.status, network = networkIs}
end
env.watchAd = function(callback)
    local from = page.mode
    local message
    if env.isNetworkConnected and not env.isNetworkConnected() then message = "network" end     -- (4.1.2.1) an ad needs the internet only: the coins wait in the wallet until the Social server is there to keep them
    if message then
        page.back, page.mode, page.problem = from, "offline", message
        if type(callback) == "function" then pcall(callback, 0) end
        return false
    end
    ad.left, ad.done, ad.from = cfg.adSeconds, callback, page.mode
    page.mode = "ad"
    return true
end
local function useLanguage(i)
    local code = languages[i]
    if code then selected = i pcall(_G.res.useLocale, code) hook("onLanguageChanged", code) end
end

local function font(headline)
    local f = headline and ((env.getFontHeadline and env.getFontHeadline()) or env.FONT_HEADLINE) or nil
    f = f or (env.getFontBasic and env.getFontBasic()) or env.FONT_BASIC or "FONT_BASIC"
    if not pcall(_G.res.useFont, f) then pcall(_G.res.useFont, "FONT_BASIC") end
end
local function text(str, tx, ty, size, va, ha, alpha)
    local res = _G.res
    local fh = (res.getFontHeight and res.getFontHeight()) or size
    local k = size / max(fh, 1)
    env.setRenderState(0, 0, k, k, 0, 0, 0, alpha or 1)
    res.drawString("", str, tx / k, ty / k, va, ha)
    env.setRenderState()
end
-- the windows share the look of the pop-ups of extras/extra_scripts.lua: round corners, the green of the pause menu with its pale yellow rim
local function round(x1, y1, x2, y2, radius, r, g, b, a)
    if env.drawRoundRect then env.drawRoundRect(x1, y1, x2, y2, radius, r, g, b, a or 1) else env.drawRect(r, g, b, a or 1, x1, y1, x2, y2, false) end
end
local function box(x, y, w, h, fill, rim, radius)
    radius = radius or min(w, h) * 0.08
    round(x - 4, y - 4, x + w + 4, y + h + 4, radius + 4, 0, 0, 0, 0.22)
    round(x - 2, y - 2, x + w + 2, y + h + 2, radius + 2, rim[1], rim[2], rim[3], rim[4] or 1)
    round(x, y, x + w, y + h, radius, fill[1], fill[2], fill[3], fill[4] or 1)
end
local GREEN, YELLOW = {0.13, 0.42, 0.36}, {0.97, 0.95, 0.59}
local function button(label, x, y, w, h, key, color)
    color = color or {0.95, 0.62, 0.12}
    local radius = h * 0.3
    round(x - 2, y - 2, x + w + 2, y + h + 2, radius + 2, 1, 1, 1, 0.85)
    round(x, y, x + w, y + h, radius, color[1], color[2], color[3], 1)
    round(x + 2, y + 2, x + w - 2, y + h / 2, radius - 1, 1, 1, 1, 0.16)         -- the glossy upper half of the game's buttons
    font(true)
    text(label, x + w / 2, y + h / 2, h * 0.5, "VCENTER", "HCENTER")
    rects[#rects + 1] = {x = x, y = y, w = w, h = h, key = key}
end

local function panel()
    local sw, sh = env.screenWidth or 1024, env.screenHeight or 768
    local w, h = min(sw * 0.8, sh * 1.1), sh * 0.7
    return sw, sh, (sw - w) / 2, (sh - h) / 2, w, h
end


-- the purchase window, after the game's own "You are about to buy the Mighty Eagle feature" (the phone versions): the golden egg's turning shine behind a pig with a
-- dollar sign, four lines of text, and cancel / BUY. Not enough coins: the same window says so and offers the coin window instead.
local shine = {angle = 0, tried = false}
local function shineSprite()
    local name = "GOLDEN_EGG_STAR_EFFECT"
    local res = _G.res
    if not (res.getSpriteBounds and res.getSpritePivot) then return nil end
    local w = res.getSpriteBounds(name)
    if not (w and w > 0) and not shine.tried then
        shine.tried = true
        if type(env.loadGoldenEggStarSprites) == "function" then pcall(env.loadGoldenEggStarSprites) end       -- the sheet of the golden egg pop-up, which the menus unload
        w = res.getSpriteBounds(name)
    end
    return (w and w > 0) and name or nil
end
local function drawDialog(sw, sh, lines, buttons)
    local w, h = min(sw * 0.7, sh * 0.95), sh * 0.66
    local x, y = (sw - w) / 2, (sh - h) / 2
    box(x, y, w, h, {0.13, 0.15, 0.17, 0.96}, {0.62, 0.65, 0.68}, 22)
    -- the shine and the piggy bank of Angry Birds Social
    local cx, cy, size = sw / 2, y + h * 0.2, h * 0.26
    shine.angle = shine.angle + 1.6 * min(env.abFrameDt or 1 / 60, 0.1)
    local sprite = shineSprite()
    if sprite then
        local sw0, sh0 = _G.res.getSpriteBounds(sprite)
        local k = (size * 3.2) / max(sw0, sh0, 1)
        local px, py = _G.res.getSpritePivot("", sprite)
        _G.res.setClipRect(x, y, w, h)                                       -- the rays stay inside the window
        env.setRenderState(0, 0, k, k, shine.angle, px, py, 1)
        _G.res.drawSprite(sprite, cx / k, cy / k)
        env.setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
        _G.res.setClipRect(0, 0, sw, sh)
    end
    if shine.pig == nil then shine.pig = env.loadPiggyBank and env.loadPiggyBank("abPiggyBank") or false end
    if shine.pig then
        local ph = size * 1.2
        env.setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
        _G.res.drawSprite("abPiggyBank", cx - ph * 1.12 / 2, cy - ph / 2, "TOP", "LEFT", ph * 1.12, ph)
    end
    font(false)
    for i, l in ipairs(lines) do text(l, sw / 2, y + h * (0.43 + (i - 1) * 0.1), h * 0.065, "VCENTER", "HCENTER") end
    local bw, bh = w * 0.38, h * 0.13
    if #buttons == 1 then
        button(buttons[1][1], sw / 2 - bw / 2, y + h * 0.82, bw, bh, buttons[1][2], buttons[1][3])
    else
        button(buttons[1][1], x + w * 0.08, y + h * 0.82, bw, bh, buttons[1][2], buttons[1][3])
        button(buttons[2][1], x + w * 0.54, y + h * 0.82, bw, bh, buttons[2][2], buttons[2][3])
    end
end
local GREY = {0.45, 0.47, 0.5}
drawBuy = function(sw, sh)
    local b = env.abBuy or {price = 0}
    local enough = wallet.coins >= b.price
    if enough then
        drawDialog(sw, sh, {"You are about to buy", "the " .. (b.item or "this item") .. " feature.", "This will cost " .. b.price .. (b.price == 1 and " coin." or " coins."), "The cost will be taken from your coins."},
                   {{"cancel", "ESCAPE", GREY}, {"BUY", "BUY"}})
    else
        drawDialog(sw, sh, {"You do not have enough coins.", "This costs " .. b.price .. (b.price == 1 and " coin" or " coins") .. ", you have " .. wallet.coins .. ".", "Watch an ad in the coin window", "to earn some."},
                   {{"cancel", "ESCAPE", GREY}, {"COINS", "COINS"}})
    end
end

local function drawWindow()
    if not page.mode then return end
    rects = {}
    local sw, sh, x, y, w, h = panel()
    if page.mode == "ad" then
        env.drawRect(0.02, 0.02, 0.05, 1, 0, 0, sw, sh, false)
        font(true)
        text("ADVERTISEMENT", sw / 2, sh * 0.38, sh * 0.07, "VCENTER", "HCENTER")
        font(false)
        text("Not a real ad: no video is played.", sw / 2, sh * 0.47, sh * 0.035, "VCENTER", "HCENTER", 0.8)
        local bw = sw * 0.4
        local p = 1 - max(0, ad.left) / cfg.adSeconds
        env.drawRect(0.25, 0.25, 0.3, 1, sw / 2 - bw / 2, sh * 0.56, sw / 2 + bw / 2, sh * 0.6, false)
        env.drawRect(0.95, 0.62, 0.12, 1, sw / 2 - bw / 2, sh * 0.56, sw / 2 - bw / 2 + bw * p, sh * 0.6, false)
        text(string.format("+%d coins in %d", cfg.adReward, math.ceil(max(0, ad.left))), sw / 2, sh * 0.66, sh * 0.04, "VCENTER", "HCENTER")
        return
    end
    env.drawRect(0, 0, 0, 0.55, 0, 0, sw, sh, false)
    if page.mode == "buy" then
        drawBuy(sw, sh)
        return
    elseif page.mode == "notice" and notice then
        drawDialog(sw, sh, notice.lines, {{notice.buttons[1][1], notice.buttons[1][2], GREY}, notice.buttons[2] and {notice.buttons[2][1], notice.buttons[2][2]} or nil})
        return
    elseif page.mode == "offline" then
        if page.problem == "server" then
            drawDialog(sw, sh, {"The Social server does not answer.", state.why or "Start it and try again."}, {{"OK", "ESCAPE"}})
        else
            drawDialog(sw, sh, {"You must be connected", "to the internet to do that!"}, {{"OK", "ESCAPE"}})
        end
        return
    end
    box(x, y, w, h, GREEN, YELLOW, 24)
    font(true)
    if page.mode == "coins" then
        text("COINS", sw / 2, y + h * 0.1, h * 0.09, "VCENTER", "HCENTER")
        text(tostring(wallet.coins), sw / 2, y + h * 0.27, h * 0.14, "VCENTER", "HCENTER")
        font(false)
        local line
        local _, player = social()
        if state.status == "online" then
            line = wallet.account and ("Shared with the Social account " .. wallet.account) or (player and ("Server found; no Social account named " .. player .. " yet: the coins wait for it") or "Server found; set socialLevels.player to link a Social account")
        elseif state.status == "offline" then line = "No Social server answers: the coins are kept on this computer"
        else line = "Not connected to the Social server yet" end
        text(line, sw / 2, y + h * 0.43, h * 0.045, "VCENTER", "HCENTER", 0.9)
        local bw, bh = w * 0.7, h * 0.12
        local fc = env.abFriendsCoins
        if fc then      -- (4.2) Angry Birds Friends: its coins are its own unless the player shares them with the Social account
            bh = h * 0.085
            text("Friends coins on their own: " .. fc.friendsCoins(), sw / 2, y + h * 0.485, h * 0.036, "VCENTER", "HCENTER", 0.85)
            button("WATCH AN AD   +" .. cfg.adReward, sw / 2 - bw / 2, y + h * 0.52, bw, bh, "A")
            button("SHARE COINS WITH FRIENDS: " .. (fc.shared() and "ON" or "OFF"), sw / 2 - bw / 2, y + h * 0.615, bw, bh, "SHARE")
            button("MOVE FRIENDS COINS TO SOCIAL", sw / 2 - bw / 2, y + h * 0.71, bw, bh, "TOSOCIAL")
            button("MOVE SOCIAL COINS TO FRIENDS", sw / 2 - bw / 2, y + h * 0.805, bw, bh, "TOFRIENDS")
            button("CLOSE", sw / 2 - bw / 2, y + h * 0.9, bw, bh, "ESCAPE")
        else
            button("WATCH AN AD   +" .. cfg.adReward, sw / 2 - bw / 2, y + h * 0.52, bw, bh, "A")
            button("SYNC NOW", sw / 2 - bw / 2, y + h * 0.67, bw, bh, "S")
            button("CLOSE", sw / 2 - bw / 2, y + h * 0.82, bw, bh, "ESCAPE")
        end
    else
        text("LANGUAGE", sw / 2, y + h * 0.08, h * 0.08, "VCENTER", "HCENTER")
        font(false)
        if #languages == 0 then
            text("This game has only one language.", sw / 2, y + h * 0.5, h * 0.05, "VCENTER", "HCENTER")
        end
        local rows = min(#languages, 8)
        local first = max(1, min(selected - 3, #languages - rows + 1))
        local rh = h * 0.09
        for i = first, first + rows - 1 do
            local ry = y + h * 0.2 + (i - first) * rh
            local on = i == selected
            local c = on and {0.95, 0.62, 0.12} or {0.09, 0.3, 0.26}
            round(x + w * 0.1, ry, x + w * 0.9, ry + rh - 3, (rh - 3) * 0.3, c[1], c[2], c[3], 1)
            font(on)
            text(languageName(languages[i]) .. "  (" .. languages[i] .. ")", x + w * 0.14, ry + rh / 2, rh * 0.5, "VCENTER", "LEFT")
            rects[#rects + 1] = {x = x + w * 0.1, y = ry, w = w * 0.8, h = rh - 3, key = "ROW", row = i}
        end
        font(true)
        text("Up / Down and Return, or click.  Esc closes.", sw / 2, y + h * 0.95, h * 0.04, "VCENTER", "HCENTER", 0.85)
    end
end

local function eat(kp, kr, kh)
    for _, t in ipairs({kp, kr, kh, env.touches}) do if type(t) == "table" then for k in pairs(t) do t[k] = nil end end end
    local c = env.cursor
    if c and c.wheelTriggered ~= nil then c.wheelTriggered = false c.wheel = 0 end
    if env.touchcount then env.touchcount = 0 end
end

local function finishBuy(ok)
    local b = env.abBuy
    env.abBuy, page.mode = nil, nil
    if b and ok then env.addCoins(-b.price) end
    if b and type(b.callback) == "function" then pcall(b.callback, ok and true or false) end
end
local function press(key, row)
    if page.mode == "buy" then
        if key == "BUY" then finishBuy(true)
        elseif key == "COINS" then finishBuy(false) page.mode = "coins"
        else finishBuy(false) end
        return
    end
    if page.mode == "notice" then
        local n = notice
        notice, page.mode = nil, nil
        if n and type(n.callback) == "function" then pcall(n.callback, key == "ESCAPE" and 1 or 2) end
        return
    end
    local fc = env.abFriendsCoins
    if key == "SHARE" and fc then fc.setShared(not fc.shared()) return
    elseif key == "TOSOCIAL" and fc then fc.toSocial() return
    elseif key == "TOFRIENDS" and fc then fc.toFriends() return end
    if key == "ESCAPE" then page.mode = nil
    elseif key == "A" then env.watchAd()
    elseif key == "S" then sync()
    elseif key == "ROW" then useLanguage(row) end
end

local previousInput = env.abAchievementInput
env.abAchievementInput = function()
    local dt = min(env.abFrameDt or 1 / 60, 0.1)
    state.clock = state.clock + dt
    if asked ~= nil or state.clock >= state.nextSync then syncInBackground() end
    local kp, kr, kh = env.keyPressed, env.keyReleased, env.keyHold
    if not page.mode then
        if type(kp) == "table" and (kp.F9 or kp.F10) then
            if kp.F9 then openLanguages() else page.mode = "coins" end
            kp.F9, kp.F10 = nil, nil
            eat(kp, kr, kh)
            return true
        end
        return previousInput and previousInput()
    end
    if env.abServicesTick then env.abServicesTick(dt) end
    if page.mode == "ad" then
        ad.left = ad.left - dt
        if ad.left <= 0 then
            env.addCoins(cfg.adReward)
            hook("onAdWatched", cfg.adReward)
            local done = ad.done
            ad.done = nil
            page.mode = ad.from == "coins" and "coins" or nil
            if type(done) == "function" then pcall(done, cfg.adReward) end
        end
    elseif page.mode == "offline" then
        if type(kp) == "table" and (kp.ESCAPE or kp.BACKSPACE or kp.RETURN or kp.LBUTTON) then page.mode, page.back = page.back, nil end
    elseif page.mode == "notice" and type(kp) == "table" then
        if kp.ESCAPE or kp.BACKSPACE then press("ESCAPE")
        elseif kp.RETURN then press(notice and notice.buttons[2] and notice.buttons[2][2] or "ESCAPE") end
        if kp.LBUTTON and page.mode then
            local c = env.cursor or {}
            for _, r in ipairs(rects) do
                if c.x and c.x >= r.x and c.x <= r.x + r.w and c.y >= r.y and c.y <= r.y + r.h then press(r.key) break end
            end
        end
    elseif page.mode == "buy" and type(kp) == "table" then
        local enough = wallet.coins >= (env.abBuy and env.abBuy.price or 0)
        if kp.ESCAPE or kp.BACKSPACE then press("ESCAPE")
        elseif kp.RETURN then press(enough and "BUY" or "COINS") end
        if kp.LBUTTON and page.mode then
            local c = env.cursor or {}
            for _, r in ipairs(rects) do
                if c.x and c.x >= r.x and c.x <= r.x + r.w and c.y >= r.y and c.y <= r.y + r.h then press(r.key) break end
            end
        end
    elseif type(kp) == "table" then
        if kp.ESCAPE or kp.F9 or kp.F10 or kp.BACKSPACE then page.mode = nil
        elseif page.mode == "languages" then
            if kp.UP then useLanguage((selected - 2) % max(#languages, 1) + 1) end
            if kp.DOWN then useLanguage(selected % max(#languages, 1) + 1) end
            if kp.RETURN then page.mode = nil end
        else
            if kp.A or kp.RETURN then env.watchAd() ad.from = "coins" end
            if kp.S then sync() end
        end
        if kp.LBUTTON and page.mode then
            local c = env.cursor or {}
            for _, r in ipairs(rects) do
                if c.x and c.x >= r.x and c.x <= r.x + r.w and c.y >= r.y and c.y <= r.y + r.h then
                    press(r.key, r.row)
                    if r.key == "A" then ad.from = "coins" end
                    break
                end
            end
        end
    end
    eat(kp, kr, kh)
    return true
end

local previousBanner = env.abDrawBanner
env.abDrawBanner = function()
    if previousBanner then previousBanner() end
    drawWindow()
end
