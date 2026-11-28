-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 9. Social levels (3.9)
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

------------------------------------------------------------------ 9. Social levels (3.9)
-- F12 (in a menu or in a level) opens a list of the levels of a stand-in Angry Birds Social server (the "Angry Birds Social Server" folder: start.bat runs it) and plays the one
-- that is picked: Up / Down choose, Left / Right turn the page, Return (or a click on a level) downloads it as a Classic level and starts it, Escape (or F4) closes. The levels are
-- the server's /classic/levels.txt (one level a line: key, name, user and plays with tabs between them) and /classic/<key>.lua (the level as a Classic level file). `server` is its address;
-- a server that is not running just says so in the list. `httpGet` and `playLevelText` are the engine's natives for this. The server also keeps the best score of every level (the list shows it):
-- the score of a cleared level is sent to it under `player`, so several people on one server (and the Social game itself) compete on the same levels.
socialLevels = {
	enabled = true,
	key = "F12",
	server = "http://localhost",
	player = "Player",        -- the name the server shows with the scores sent from the engine (the same name as a Social account shares its scores with it)
	sendScores = true,        -- the score of a cleared Social level goes to the server (a short list of the best scores is shown with the levels)
	closeKeys = { "ESCAPE", "F12", "KEY_BACK" },
	playKeys = { "RETURN", "SPACE" },
	rows = 8,                -- levels shown on a page
	timeout = 3,             -- seconds to wait for the server
	title = "Social levels",
}
do
	local type, ipairs = _G.type, _G.ipairs
	local floor, max, min = _G.math.floor, _G.math.max, _G.math.min
	local gmatch, find = _G.string.gmatch, _G.string.find
	local open, list, selected, status, rowRects = false, {}, 1, "", {}
	local current, sentScore = nil, 0     -- the level that is being played from the list, and the score that was sent for it

	local function urlEncode(text)
		return (_G.string.gsub(_G.tostring(text), "[^%w%-_%.]", function(c) return _G.string.format("%%%02X", _G.string.byte(c)) end))
	end

	local function load()
		list, selected, status = {}, 1, ""
		if type(httpGet) ~= "function" then status = "This engine build has no httpGet: rebuild it" return end
		local text, err = httpGet(socialLevels.server .. "/classic/levels.txt", socialLevels.timeout)
		if text == nil then status = "No answer from " .. socialLevels.server .. " (" .. _G.tostring(err) .. "). Is the server running?" return end
		for line in gmatch(text, "[^\r\n]+") do
			local key, name, user, plays, topScore, topPlayer = _G.string.match(line, "^([^\t]*)\t([^\t]*)\t([^\t]*)\t?([^\t]*)\t?([^\t]*)\t?([^\t]*)")
			if key ~= nil and key ~= "" then list[#list + 1] = { key = key, name = name, user = user, plays = plays, topScore = _G.tonumber(topScore) or 0, topPlayer = topPlayer or "" } end
		end
		if #list == 0 then status = "The server has no levels yet" end
	end

	local function close() open, modalOpen, modalInput = false, false, nil end

	local function play()
		local entry = list[selected]
		if entry == nil then return end
		local text, err = httpGet(socialLevels.server .. "/classic/" .. entry.key .. ".lua", socialLevels.timeout)
		if text == nil then status = "Could not download " .. entry.name .. " (" .. _G.tostring(err) .. ")" return end
		close()
		-- a level played from here replaces the level instead of being spawned into it, even if the level spawner takes dropped files
		local spawnerDrop = levelSpawner ~= nil and levelSpawner.drop
		if levelSpawner ~= nil then levelSpawner.drop = false end
		local ok = playLevelText(text)
		if levelSpawner ~= nil then levelSpawner.drop = spawnerDrop end
		if not ok then _G.print("Social levels: this game cannot start a level from text\n") return end
		current, sentScore = entry, 0
		if highscores ~= nil then highscores["dropped.temp"] = nil end        -- the score of the level before is not this level's
		if socialLevels.sendScores then (httpGetAsync or httpGet)(socialLevels.server .. "/classic/play?key=" .. entry.key .. "&player=" .. urlEncode(socialLevels.player), 1, true) end
	end

	local function anyKey(keys, pressed)
		for _, k in ipairs(keys) do if pressed[k] then return true end end
		return false
	end

	-- the score of the level played from the list is in highscores["dropped.temp"] once it is cleared; a better one after a restart is sent again
	local function sendScore()
		if current == nil or not socialLevels.sendScores or highscores == nil then return end
		local entry = highscores["dropped.temp"]
		if type(entry) ~= "table" or type(entry.score) ~= "number" or entry.score <= sentScore then return end
		sentScore = entry.score
		local fire = httpGetAsync or httpGet
		fire(socialLevels.server .. "/classic/score?key=" .. current.key .. "&player=" .. urlEncode(socialLevels.player) .. "&score=" .. _G.string.format("%d", entry.score), 1, true)
	end

	local function handle()
		if not socialLevels.enabled then return end
		sendScore()
		if not open then
			if keyPressed ~= nil and keyPressed[socialLevels.key] and not modalOpen and type(playLevelText) == "function" then
				load()
				modalInput = nil
				open, modalOpen = true, true
			end
			return
		end
		local input = modalInput
		if input == nil then return end
		local p = input.pressed
		if anyKey(socialLevels.closeKeys, p) then close() return end
		local rows = socialLevels.rows
		if p["DOWN"] then selected = min(#list, selected + 1) elseif p["UP"] then selected = max(1, selected - 1)
		elseif p["RIGHT"] then selected = min(#list, selected + rows) elseif p["LEFT"] then selected = max(1, selected - rows) end
		if anyKey(socialLevels.playKeys, p) then play() return end
		if input.clicked then
			for _, r in ipairs(rowRects) do
				if input.x >= r.x1 and input.x <= r.x2 and input.y >= r.y1 and input.y <= r.y2 then
					if selected == r.index then play() else selected = r.index end
					return
				end
			end
		end
	end

	local function drawPanel()
		if not open then return end
		local w, h = screenWidth, screenHeight
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
		drawRect(0, 0, 0, 0.6, 0, 0, w, h, false)
		local pw, ph = w * 0.6, h * 0.82
		local x1, y1 = (w - pw) / 2, (h - ph) / 2
		popupBox(x1, y1, x1 + pw, y1 + ph)
		if type(fontBasic) ~= "nil" then _G.res.useFont(fontBasic) end
		_G.res.drawString("", socialLevels.title, w / 2, y1 + ph * 0.07, "VCENTER", "HCENTER")
		rowRects = {}
		if status ~= "" then
			-- a long message (the server's address and the reason) is broken into lines that fit the panel
			local lines, line = {}, ""
			for word in _G.string.gmatch(status, "%S+") do
				if line ~= "" and _G.res.getStringWidth(line .. " " .. word) > pw * 0.9 then lines[#lines + 1] = line line = word else line = (line == "" and word) or (line .. " " .. word) end
			end
			lines[#lines + 1] = line
			for i = 1, #lines do _G.res.drawString("", lines[i], w / 2, y1 + ph * 0.5 + (i - (#lines + 1) / 2) * 34, "VCENTER", "HCENTER") end
		end
		local rows = socialLevels.rows
		local first = floor((selected - 1) / rows) * rows + 1
		local rowH = ph * 0.075
		for i = first, min(#list, first + rows - 1) do
			local e = list[i]
			local ry1 = y1 + ph * 0.15 + (i - first) * (rowH + ph * 0.012)
			local r = { x1 = x1 + pw * 0.04, y1 = ry1, x2 = x1 + pw * 0.96, y2 = ry1 + rowH, index = i }
			rowRects[#rowRects + 1] = r
			if i == selected then popupButton(r.x1, r.y1, r.x2, r.y2, 0.95, 0.62, 0.12, true) else popupButton(r.x1, r.y1, r.x2, r.y2, 0.09, 0.3, 0.26) end
			_G.res.drawString("", e.name .. "   by " .. e.user .. (e.topScore > 0 and ("   best " .. e.topScore .. " (" .. e.topPlayer .. ")") or (e.plays ~= "" and ("   (" .. e.plays .. " plays)") or "")), w / 2, (r.y1 + r.y2) / 2, "VCENTER", "HCENTER")
		end
		if #list > 0 then _G.res.drawString("", "page " .. (floor((selected - 1) / rows) + 1) .. " / " .. (floor((#list - 1) / rows) + 1), w / 2, y1 + ph * 0.875, "VCENTER", "HCENTER") end
		_G.res.drawString("", "Up / Down: choose    Left / Right: page", w / 2, y1 + ph * 0.925, "VCENTER", "HCENTER")
		_G.res.drawString("", "Return: play    Esc: close", w / 2, y1 + ph * 0.968, "VCENTER", "HCENTER")
	end

	local originalUpdate, originalDrawGame, originalDrawMenu = update, drawGame, drawMenu
	update = function(dt, realDt)
		originalUpdate(dt, realDt)
		handle()
	end
	if type(originalDrawGame) == "function" then drawGame = function(...) originalDrawGame(...) drawPanel() end end
	if type(originalDrawMenu) == "function" then drawMenu = function(...) originalDrawMenu(...) drawPanel() end end
end
