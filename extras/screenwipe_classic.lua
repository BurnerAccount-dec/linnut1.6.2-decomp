-- Star Wars style screen wipe for Angry Birds Classic 1.6.2.
--
-- What it does: when a level is loaded, the picture on the screen is kept (res.captureSprite) and drawn over the
-- new level while a clip rectangle shrinks towards the bottom right corner (res.setClipRect), so the old screen wipes
-- away and the level shows underneath. It is the same logic as GameScene.initializeTransition / updateTransition /
-- drawTransition in Angry Birds Star Wars (one second, swipe from the top left).
--
-- How to use: copy this file next to gamelogic.lua (data/scripts/screenwipe_classic.lua). The engine loads it by
-- itself in Classic 1.6.2 when it is there; to load it by hand put this line at the end of gamelogic.lua:
--   loadLuaFile(scriptPath .. "/screenwipe_classic.lua", "", false, false)
-- Nothing else in the game scripts is changed: it wraps loadLevel, update and drawMenu.

if screenwipe ~= nil or _G.type(loadLevel) ~= "function" or _G.type(drawMenu) ~= "function" or _G.type(update) ~= "function" then
	return
end

screenwipe = {
	time = 1,          -- seconds the wipe takes (transitionTimes in the Star Wars scripts)
	sprite = "CAPTURED_SCREENSHOT",
	timer = nil,       -- nil: no wipe running
}

res.keepLastFrame(true)     -- the script asks for the picture in the middle of a frame: let captureSprite take the last finished frame

local originalLoadLevel = loadLevel
local originalUpdate = update
local originalDrawMenu = drawMenu

-- GameScene.initializeTransition: keep the last picture before the new level replaces it
loadLevel = function(...)
	if screenwipe.timer ~= nil then
		res.releaseSpriteSheet(screenwipe.sprite)
	end
	res.captureSprite(screenwipe.sprite)
	screenwipe.timer = 0
	return originalLoadLevel(...)
end

-- GameScene.updateTransition
update = function(dt, realDt)
	originalUpdate(dt, realDt)
	if screenwipe.timer ~= nil then
		screenwipe.timer = screenwipe.timer + (realDt or dt)
		if screenwipe.timer >= screenwipe.time then
			screenwipe.timer = nil
			res.releaseSpriteSheet(screenwipe.sprite)
		end
	end
end

-- GameScene.drawTransition: the old picture, clipped to the part of the screen that has not been wiped yet
drawMenu = function(...)
	originalDrawMenu(...)
	if screenwipe.timer ~= nil then
		local t = screenwipe.timer / screenwipe.time
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
		res.setClipRect(t * screenWidth, t * screenHeight, screenWidth - t * screenWidth, screenHeight - t * screenHeight)
		res.drawSprite(screenwipe.sprite, 0, -1)
		res.setClipRect(0, 0, screenWidth, screenHeight)
	end
end
