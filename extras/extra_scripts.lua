-- Extra scripts for Angry Birds Classic 1.6.2 / 1.6.3.1, loaded by the engine when data/scripts/extra_scripts.lua exists next to gamelogic.lua
-- (or by hand: loadLuaFile(scriptPath .. "/extra_scripts.lua", "", false, false) at the end of gamelogic.lua).
--   1. the Star Wars screen wipe between a menu and a level
--   2. the Star Wars aiming aid: while the bird is pulled back, the whole flight path is drawn as dots
-- Loading it a second time does nothing.

if extraScripts ~= nil or _G.type(loadLevel) ~= "function" or _G.type(update) ~= "function" then
	return
end
extraScripts = true
print("extra_scripts.lua loaded (screen wipe, aiming aid, version 6" .. (_G.type(simulateAimingAid) == "function" and ")" or ": this engine build is too old for the aiming aid, rebuild it)"))

------------------------------------------------------------------ 1. screen wipe
screenwipe = {
	time = 1,            -- seconds the wipe takes
	direction = "right", -- "right": the old picture is wiped away from its right edge towards the left, "left": from its left edge towards the right
	sprite = "CAPTURED_SCREENSHOT",
	timer = nil,         -- nil: no wipe running
	waiting = false,     -- the level is loading: the wipe starts when the loading screen is gone
	active = false,      -- the wrappers below are installed
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

------------------------------------------------------------------ 2. aiming aid
-- The Star Wars / Space aiming aid: dots run along the arc the bird that is pulled back will fly (the free flight, as in Star Wars; no blocks, no ground). The launch speed is worked out the way the game
-- launches a bird (shootSelectedBird): the impulse over the mass, along the pulled-back rubber band. The dots use the engine's aiming aid, the same as
-- Star Wars and Space.
aimingAid = {
	enabled = true,
	points = 150,     -- the length of the path: one point per physics step, 1/30 second (150 = 5 seconds)
	sprite = nil,     -- the dot sprite; nil: the bird's aimingAidSprite when the game has it (Star Wars), else Space's TRAIL_AIM_1 / TRAIL_AIM_BLUE (needs Space's INGAME_PARTICLES_1 sheet in the images folder), else TRAIL_WHITE_1
	scale = 3,        -- dot size factor (Classic's TRAIL_WHITE sprites are small)
	debug = false,    -- true: prints why the dots are or are not drawn, every frame
	showing = false,
}

local originalDrawGame2 = drawGame
drawGame = function(...)
	local a = aimingAid
	local name = selectedBird ~= nil and selectedBird.name or nil
	local dragging = name ~= nil or (g_birdDragOnTouchAttributes ~= nil and g_birdDragOnTouchAttributes.dragging)
	if name == nil and dragging then name = currentBirdName end
	local ok = a.enabled and dragging and name ~= nil and rubberBandPos ~= nil and levelStartPosition ~= nil and objects.world[name] ~= nil
		and _G.type(rubberBandMaximumLength) == "function" and _G.type(simulateAimingAid) == "function"
	if a.debug then print("aimingAid: enabled=" .. _G.tostring(a.enabled) .. " dragging=" .. _G.tostring(dragging) .. " bird=" .. _G.tostring(name) .. " band=" .. _G.tostring(rubberBandLength) .. " ok=" .. _G.tostring(ok)) end
	if ok then
		local dx, dy = levelStartPosition.x - rubberBandPos.x, levelStartPosition.y - rubberBandPos.y
		local dist = _G.math.sqrt(dx * dx + dy * dy)
		if dist > 0.0001 then
			local distFactor = _G.math.min(1, _G.math.max(0, rubberBandLength / rubberBandMaximumLength()))
			local force = -defaultForce * physicsScale * (selectedBird and selectedBird.mass or objects.world[name].mass) * distFactor   -- the impulse shootSelectedBird applies
			if not a.showing then
				a.showing = true
				local def = getObjectDefinition and getObjectDefinition(name)
				local sprite, scale = a.sprite or (def and def.aimingAidSprite), 1
				if not (sprite and spriteExists(sprite)) then   -- Space's light blue dot: its sheet INGAME_PARTICLES_1 (.dat and .png from Angry Birds Space) is loaded from the images folder when it is there
					_G.pcall(_G.res.createSpriteSheet, imagePath .. "/INGAME_PARTICLES_1.dat")
					sprite, scale = nil, 1
					for _, n in _G.ipairs({"TRAIL_AIM_1", "TRAIL_AIM_BLUE"}) do if spriteExists(n) then sprite = n break end end
					if not sprite then sprite, scale = "TRAIL_WHITE_1", a.scale end
				end
				setAimingAidSprite(sprite, scale)
				enableAimingAid(true)
			end
			simulateAimingAid(name, dx / dist * force, dy / dist * force, a.points, rubberBandPos.x, rubberBandPos.y)
		end
	elseif a.showing then
		a.showing = false
		enableAimingAid(false)
		clearAimingAid(1)
	end
	originalDrawGame2(...)
end
