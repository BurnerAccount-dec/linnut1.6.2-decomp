-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 7. leaving a level (3.6)
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

------------------------------------------------------------------ 7. leaving a level (3.6)
-- Pressing Level select or Menu on the pause page asks first: S or Return (or the left button) leaves and keeps the state of the level, so it is
-- there the next time; N or D leaves and forgets it; Escape or C goes back to the game. The game is frozen while the pop-up is open (the global modalOpen: the engine gives the game
-- no keys and no mouse and the pop-up reads them from modalInput). The music of a level keeps playing on the pause page and stops once the level is left.
exitPrompt = {
	enabled = true,
	saveKeys = { "S", "RETURN" },
	discardKeys = { "N", "D" },
	cancelKeys = { "ESCAPE", "C", "KEY_BACK" },
	title = "Leave this level?",
	saveText = "Leave and keep (S)",
	discardText = "Leave, start over (N)",
	cancelText = "Back (Esc)",
}
do
	local type, ipairs, pairs = _G.type, _G.ipairs, _G.pairs
	local pending = nil       -- { page =, index = } the button press that opened the pop-up
	local inLevel = false
	local buttons = {}

	engine.on("levelLoaded", function() inLevel = true end)

	local function ready()
		return exitPrompt.enabled and inLevel and type(saveStates) == "table" and saveStates.enabled ~= false and objects.world ~= nil
	end

	-- The pause page's Level select and Menu buttons (every game has them under these names, on the global pausePage or g_ingamePausePage) are re-wired: pressing one only
	-- opens the pop-up; the press the game would have made (mode, page, callFunction) is made when the pop-up is answered.
	local function stopLevelMusic()
		if type(musics) ~= "table" then return end
		for _, m in pairs(musics) do
			if type(m) == "string" and _G.res.isAudioPlaying(m) then _G.res.stopAudio(m) end
		end
	end

	local function replay(page, index, held)
		if held.updateFunction ~= nil then
			setGameMode(held.updateFunction)
			if currentGameMode == updateMenu then
				if held.page ~= nil and held.page.popup == true then setActivePopupPage(held.page)
				else setActiveMenuPage(held.page) popupPage = nil end
			end
			if currentGameMode == addPopupMenu then setGameMode(updateMenu) setActivePopupPage(held.page) end
			if currentGameMode == removePopupMenu then setGameMode(updateMenu) popupPage = nil end
			if type(handleGameModeChange) == "function" then handleGameModeChange(page, index) end
		end
		if held.callFunction ~= nil then held.callFunction(held.callParam1) end
	end

	local function wire(page)
		if type(page) ~= "table" or type(page.items) ~= "table" then return end
		for index, item in ipairs(page.items) do
			if type(item) == "table" and item.abLeave == nil and (item.name == "buttonMenu" or item.name == "buttonMainMenu") then
				local held = { updateFunction = item.updateFunction, page = item.page, callFunction = item.callFunction, callParam1 = item.callParam1 }
				item.abLeave = held
				item.updateFunction = nil
				item.callFunction = function()
					if pending ~= nil or not ready() then replay(page, index, held) return end
					modalInput = nil          -- the keys of the last pop-up are still in it
					pending = { page = page, index = index, held = held }
					modalOpen = true
				end
			end
		end
	end

	local function finish(keep)
		local p = pending
		pending, modalOpen, modalInput = nil, false, nil
		if p == nil then return end
		if keep then saveStates.save() else saveStates.forget() end
		local held = p.held
		-- the game's button may set its page after the pop-up was made (the level select does): read it again
		held.page = p.page.items[p.index].page
		replay(p.page, p.index, held)
		inLevel = false
		stopLevelMusic()
	end

	local function anyKey(list, pressed)
		for _, k in ipairs(list) do if pressed[k] then return true end end
		return false
	end

	local function inside(b, x, y) return x >= b.x1 and x <= b.x2 and y >= b.y1 and y <= b.y2 end

	local function handle()
		if pending == nil then return end
		local input = modalInput
		if input == nil then return end
		if anyKey(exitPrompt.saveKeys, input.pressed) then finish(true) return end
		if anyKey(exitPrompt.discardKeys, input.pressed) then finish(false) return end
		if anyKey(exitPrompt.cancelKeys, input.pressed) then pending, modalOpen, modalInput = nil, false, nil return end
		if input.clicked then
			if buttons.save and inside(buttons.save, input.x, input.y) then finish(true)
			elseif buttons.discard and inside(buttons.discard, input.x, input.y) then finish(false)
			elseif buttons.cancel and inside(buttons.cancel, input.x, input.y) then pending, modalOpen, modalInput = nil, false, nil end
		end
	end

	local function drawPrompt()
		if pending == nil then return end
		local w, h = screenWidth, screenHeight
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
		drawRect(0, 0, 0, 0.55, 0, 0, w, h, false)
		local bw, bh = w * 0.24, h * 0.11
		local cx, cy = w * 0.5, h * 0.5
		local pw, ph = bw * 3 + w * 0.06, h * 0.36
		popupBox(cx - pw / 2, cy - ph / 2, cx + pw / 2, cy + ph / 2)
		local function button(key, text, i, r, g, b)
			local x1 = cx - pw / 2 + w * 0.015 + (i - 1) * (bw + w * 0.015)
			local y1 = cy + ph / 2 - bh - h * 0.04
			buttons[key] = { x1 = x1, y1 = y1, x2 = x1 + bw, y2 = y1 + bh }
			popupButton(x1, y1, x1 + bw, y1 + bh, r, g, b)
			if type(fontBasic) ~= "nil" then _G.res.useFont(fontBasic) end
			_G.res.drawString("", text, x1 + bw / 2, y1 + bh / 2, "VCENTER", "HCENTER")
		end
		if type(fontBasic) ~= "nil" then _G.res.useFont(fontBasic) end
		_G.res.drawString("", exitPrompt.title, cx, cy - ph / 2 + h * 0.07, "VCENTER", "HCENTER")
		button("save", exitPrompt.saveText, 1, 0.2, 0.5, 0.2)
		button("discard", exitPrompt.discardText, 2, 0.55, 0.2, 0.15)
		button("cancel", exitPrompt.cancelText, 3, 0.25, 0.3, 0.45)
	end

	-- the pop-up is drawn over the game and over the pause page; the wrappers are made here, once, before the screen wipe installs its own
	local originalUpdate, originalDrawGame, originalDrawMenu = update, drawGame, drawMenu
	update = function(dt, realDt)
		originalUpdate(dt, realDt)
		wire(pausePage)
		wire(g_ingamePausePage)
		handle()
		if not inLevel and currentGameMode == updateMenu then stopLevelMusic() end      -- the pause page keeps the music; the menus after a level do not
	end
	if type(originalDrawGame) == "function" then drawGame = function(...) originalDrawGame(...) drawPrompt() end end
	if type(originalDrawMenu) == "function" then drawMenu = function(...) originalDrawMenu(...) drawPrompt() end end
end
