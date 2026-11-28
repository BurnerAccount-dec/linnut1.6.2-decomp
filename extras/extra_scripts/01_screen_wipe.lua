-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 1. screen wipe
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

------------------------------------------------------------------ 1. screen wipe
screenwipe = {
	time = 1,            -- seconds the wipe takes
	direction = "right", -- "right": the old picture is wiped away from its right edge towards the left, "left": from its left edge towards the right
	sprite = "CAPTURED_SCREENSHOT",
	timer = nil,         -- nil: no wipe running
	waiting = false,     -- the level is loading: the wipe starts when the loading screen is gone
	active = false,      -- the wrappers below are installed
	editor = true,       -- (3.6) false: the editor is wiped like everything else
	skip = false,        -- the next level load is a restart: no wipe (section 4 sets it, and clears it after a few seconds if no level came)
	skipTime = 0,
}

local originalLoadLevel = loadLevel
local originalSetGameMode = setGameMode
local originalUpdate, originalDrawMenu, originalDrawGame

local function drawWipe()
	if screenwipe.timer ~= nil then
		local t = screenwipe.timer / screenwipe.time
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
		if screenwipe.direction == "left" then
			_G.res.setClipRect(t * screenWidth, 0, screenWidth - t * screenWidth, screenHeight)
		else
			_G.res.setClipRect(0, 0, screenWidth - t * screenWidth, screenHeight)
		end
		_G.res.drawSprite(screenwipe.sprite, 0, -1)
		_G.res.setClipRect(0, 0, screenWidth, screenHeight)
	end
end

local function uninstall()
	update = originalUpdate
	if originalDrawMenu then drawMenu = originalDrawMenu end
	if originalDrawGame then drawGame = originalDrawGame end
	screenwipe.active = false
end

local function install()
	if screenwipe.active then return end
	screenwipe.active = true
	originalUpdate, originalDrawMenu, originalDrawGame = update, drawMenu, drawGame

	update = function(dt, realDt)
		originalUpdate(dt, realDt)
		if screenwipe.keepTimer ~= nil then
			-- the loading screen was not followed by a level (the story pages, the menus): stop reading the screen back, it costs a lot of frame rate
			screenwipe.keepTimer = screenwipe.keepTimer + (realDt or dt)
			if screenwipe.keepTimer > 3 then
				screenwipe.keepTimer = nil
				_G.res.keepLastFrame(false)
				if screenwipe.timer == nil and not screenwipe.waiting then uninstall() return end
			end
		end
		if screenwipe.waiting and currentGameMode ~= updateLoading and currentGameMode ~= updateLoadingEx then
			screenwipe.waiting = false
			screenwipe.timer = 0
		elseif screenwipe.timer ~= nil then
			screenwipe.timer = screenwipe.timer + (realDt or dt)
			if screenwipe.timer >= screenwipe.time then
				screenwipe.timer = nil
				_G.res.releaseSpriteSheet(screenwipe.sprite)
				uninstall()
			end
		end
	end
	if _G.type(originalDrawMenu) == "function" then
		drawMenu = function(...)
			originalDrawMenu(...)
			drawWipe()
		end
	end
	if _G.type(originalDrawGame) == "function" then
		drawGame = function(...)
			originalDrawGame(...)
			drawWipe()
		end
	end
end

-- reading the screen back costs frame rate: the frame before a level loads is the only one that is kept
if _G.type(originalSetGameMode) == "function" then
	setGameMode = function(mode, ...)
		-- (3.6) no wipe in the level editor, nor on the way to it: a load that starts while the game mode is the editor's is not wiped
		if mode == updateEditor then screenwipe.editorLoad = screenwipe.editor ~= false end
		if mode == updateLoading or mode == updateLoadingEx then screenwipe.editorLoad = screenwipe.editor ~= false and currentGameMode == updateEditor
		elseif mode == updateGame or mode == updateMenu then screenwipe.editorLoad = false end
		if screenwipe.skip or screenwipe.editorLoad then return originalSetGameMode(mode, ...) end
		if mode == updateLoading or mode == updateLoadingEx then
			_G.res.keepLastFrame(true)
			screenwipe.keepTimer = 0
			install()
		elseif screenwipe.keepTimer ~= nil and not screenwipe.waiting and screenwipe.timer == nil then
			screenwipe.keepTimer = nil
			_G.res.keepLastFrame(false)
		end
		return originalSetGameMode(mode, ...)
	end
end

loadLevel = function(...)
	if screenwipe.skip then      -- a restart: the level is loaded at once, nothing is wiped
		screenwipe.skip = false
		return originalLoadLevel(...)
	end
	-- (3.7.3.1) the editor is never wiped: the level pages of the editor set the editor's mode before the level is loaded, the play test and the way back to the editor load while startedFromEditor is set
	if screenwipe.editorLoad or (screenwipe.editor ~= false and (currentGameMode == updateEditor or startedFromEditor == true)) then return originalLoadLevel(...) end
	if screenwipe.timer ~= nil or screenwipe.waiting then
		_G.res.releaseSpriteSheet(screenwipe.sprite)
	end
	_G.res.captureSprite(screenwipe.sprite)
	_G.res.keepLastFrame(false)
	screenwipe.keepTimer = nil
	screenwipe.timer = nil
	screenwipe.waiting = true
	install()
	return originalLoadLevel(...)
end
