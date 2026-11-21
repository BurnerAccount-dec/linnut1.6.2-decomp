-- Extra scripts for Angry Birds Classic 1.6.2 / 1.6.3.1, loaded by the engine when data/scripts/extra_scripts.lua exists next to gamelogic.lua
-- (or by hand: loadLuaFile(scriptPath .. "/extra_scripts.lua", "", false, false) at the end of gamelogic.lua).
--   1. the Star Wars screen wipe between a menu and a level
--   2. the Star Wars aiming aid: while the bird is pulled back, the whole flight path is drawn as dots
--   4. (3.3) restart: the restart button first moves every object back to where the level began, then restarts without the screen wipe
--   10. (3.9) episodes.lua: the six episodes of the old menu (before 2.0.0) read from one file
--   7. (3.6) leaving a level: a pop-up to leave with or without keeping the state of the level; the level's music stops in the menus
--   3. (3.3) the laser mod: a piercing laser and slow motion for the Red Bird, a heat-wave GLSL shader on the background, an anti-gravity sensor zone
-- Loading it a second time does nothing.

if extraScripts ~= nil or _G.type(loadLevel) ~= "function" or _G.type(update) ~= "function" then
	return
end
extraScripts = true
print("extra_scripts.lua loaded (screen wipe, aiming aid, laser mod, restart, shape blocks, leave pop-up, episodes, version 19" .. (_G.type(simulateAimingAid) == "function" and ")" or ": this engine build is too old for the aiming aid, rebuild it)"))

------------------------------------------------------------------ the pop-ups' look (4.1)
-- Every pop-up of this file (leaving a level, the level spawner, the Social levels) is a round-cornered panel in the green of the pause menu with its pale yellow rim,
-- and round buttons; drawRoundRect is the engine's native (an older exe without it gets square corners).
popupBox = function(x1, y1, x2, y2, radius)
	radius = radius or 24
	if _G.type(drawRoundRect) == "function" then
		drawRoundRect(x1 - 4, y1 - 4, x2 + 4, y2 + 4, radius + 4, 0, 0, 0, 0.22)                    -- a soft shadow
		drawRoundRect(x1 - 2, y1 - 2, x2 + 2, y2 + 2, radius + 2, 0.97, 0.95, 0.59, 1)               -- the pale yellow rim
		drawRoundRect(x1, y1, x2, y2, radius, 0.13, 0.42, 0.36, 1)                                    -- the pause menu's green
	else
		drawRect(0.13, 0.42, 0.36, 1, x1, y1, x2, y2, false)
		drawRectLines(x1, y1, x2, y2, 3, 248, 242, 151, 255)
	end
end
popupButton = function(x1, y1, x2, y2, r, g, b, selected)
	local radius = (y2 - y1) * 0.3
	if _G.type(drawRoundRect) == "function" then
		drawRoundRect(x1 - 2, y1 - 2, x2 + 2, y2 + 2, radius + 2, 1, 1, 1, selected and 1 or 0.85)
		drawRoundRect(x1, y1, x2, y2, radius, r, g, b, 1)
		drawRoundRect(x1 + 2, y1 + 2, x2 - 2, (y1 + y2) / 2, radius - 1, 1, 1, 1, 0.16)              -- the glossy upper half of the game's buttons
	else
		drawRect(r, g, b, 1, x1, y1, x2, y2, false)
		drawRectLines(x1, y1, x2, y2, 2, 255, 255, 255, 255)
	end
end

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

------------------------------------------------------------------ 2. aiming aid
-- The Star Wars / Space aiming aid: dots run along the arc the bird that is pulled back will fly (the free flight, as in Star Wars; no blocks, no ground). The launch speed is worked out the way the game
-- launches a bird (shootSelectedBird): the impulse over the mass, along the pulled-back rubber band. The dots use the engine's aiming aid, the same as
-- Star Wars and Space.
aimingAid = {
	enabled = true,
	points = 150,     -- the length of the path: one point per physics step, 1/30 second (150 = 5 seconds)
	sprite = nil,     -- the dot sprite; nil: the bird's aimingAidSprite when the game has it (Star Wars), else Space's TRAIL_AIM_1 / TRAIL_AIM_BLUE (needs Space's INGAME_PARTICLES_1 sheet in the images folder), else TRAIL_FLOWER_2, else TRAIL_WHITE_1
	scale = 3,        -- dot size factor (Classic's TRAIL_WHITE sprites are small)
	debug = false,    -- true: prints why the dots are or are not drawn, every frame
	showing = false,
	silhouette = true,       -- the dots are silhouettes of the selected bird's current sprite in its main colour (the old dots are commented out below)
	silhouetteScale = 0.4,   -- size of those silhouettes compared with the bird
	silhouetteAlpha = 0.65,   -- 35% see-through
}

-- the length the rubber band can be pulled to: a function in 1.6.x, the variable shootMaxLength in 1.3.5 and the Free iPhone version
local function maxBandLength()
	if _G.type(rubberBandMaximumLength) == "function" then return rubberBandMaximumLength() end
	return shootMaxLength
end

local originalDrawGame2 = drawGame
drawGame = function(...)
	local a = aimingAid
	local name = selectedBird ~= nil and selectedBird.name or nil
	local dragging = name ~= nil or (g_birdDragOnTouchAttributes ~= nil and g_birdDragOnTouchAttributes.dragging)
	if name == nil and dragging then name = currentBirdName end
	if birdSelected == false or levelCompleted == true then dragging = false end   -- the level ended (or the bird was let go) while it was held: the game unselects it, the touch-drag flag may stay set
	local ok = a.enabled and dragging and name ~= nil and rubberBandPos ~= nil and levelStartPosition ~= nil and objects.world[name] ~= nil
		and maxBandLength() ~= nil and _G.type(simulateAimingAid) == "function"
	if a.debug then print("aimingAid: enabled=" .. _G.tostring(a.enabled) .. " dragging=" .. _G.tostring(dragging) .. " bird=" .. _G.tostring(name) .. " band=" .. _G.tostring(rubberBandLength) .. " ok=" .. _G.tostring(ok)) end
	if ok then
		local dx, dy = levelStartPosition.x - rubberBandPos.x, levelStartPosition.y - rubberBandPos.y
		local dist = _G.math.sqrt(dx * dx + dy * dy)
		if dist > 0.0001 then
			local distFactor = _G.math.min(1, _G.math.max(0, rubberBandLength / maxBandLength()))
			local def = blockTable.blocks[objects.world[name].definition or ""]
			-- the impulse shootSelectedBird applies, worked out as the Sling Scope power-up does it (3.3.0's getLaunchVelocity): the bird's own launch force if it has one, and the King Sling's extra power
			local force = -((def and def.slingshotLaunchForce) or defaultForce) * physicsScale * (selectedBird and selectedBird.mass or objects.world[name].mass) * distFactor * (g_slingshot_power or 1)
			if not a.showing then
				a.showing = true
				-- the dots are small silhouettes of the bird that is pulled back: its current sprite, filled with the main colour of that sprite
				local birdSprite = objects.world[name].sprite
				local sprite, scale
				if a.silhouette and birdSprite ~= nil and _G.type(createSilhouette) == "function" then
					local silhouette = "AIMSIL_" .. birdSprite
					if spriteExists(silhouette) or createSilhouette(silhouette, birdSprite, -1, 0, 0, a.silhouetteAlpha) then
						sprite, scale = silhouette, a.silhouetteScale
					end
				end
				if sprite == nil then sprite, scale = "TRAIL_WHITE_1", a.scale end   -- the bird has no sprite to copy
				--[[ The old system (the dots of Space / Star Wars, else TRAIL_FLOWER_2 or the white puff), kept for anyone who wants it back: remove this comment and the silhouette block above.
				local def = getObjectDefinition and getObjectDefinition(name)
				sprite, scale = a.sprite or (def and def.aimingAidSprite), 1
				if not (sprite and spriteExists(sprite)) then   -- Space's light blue dot: its sheet INGAME_PARTICLES_1 (.dat and .png from Angry Birds Space) is loaded from the images folder when it is there
					_G.pcall(_G.res.createSpriteSheet, imagePath .. "/INGAME_PARTICLES_1.dat")
					sprite, scale = nil, 1
					for _, n in _G.ipairs({"TRAIL_AIM_1", "TRAIL_AIM_BLUE"}) do if spriteExists(n) then sprite = n break end end
					if not sprite then   -- no Space / Star Wars dot: the flower trail of the Classic games, then the white puff
						for _, n in _G.ipairs({"TRAIL_FLOWER_2", "TRAIL_WHITE_1"}) do if spriteExists(n) then sprite, scale = n, (n == "TRAIL_WHITE_1" and a.scale or 0.7) break end end
					end
				end
				]]
				setAimingAidSprite(sprite, scale)
				enableAimingAid(true)
			end
			-- birds that are pushed while they fly (the Boomerang Bird's lift: gamelogic applies flyVerticalForce every frame until it is tapped) get the same push in the simulation, as the Sling Scope passes it to its native
			local fx, fy = 0, 0
			if def ~= nil then fx, fy = def.flyHorizontalForce or 0, def.flyVerticalForce or 0 end
			simulateAimingAid(name, dx / dist * force, dy / dist * force, a.points, rubberBandPos.x, rubberBandPos.y, fx, fy)
		end
	elseif a.showing then
		a.showing = false
		enableAimingAid(false)
		clearAimingAid(1)
	end
	originalDrawGame2(...)
end

------------------------------------------------------------------ 3. laser mod (3.3)
-- Three effects that work together. Each one has its switch below, set it to false to turn it off; all numbers can be changed from the console (lua laserMod.damage = 50).
--   laserBird:   tap (or press SPACE) while a Red Bird flies. The game slows down and a laser shoots from the bird to the cursor (at most laserMod.maxRange units) and pierces what it crosses:
--                it pushes the blocks and pigs on its line and takes strength from them (the game destroys what reaches 0 itself, with score and particles).
--                Rock and everything that cannot move (the ground, the immovable planks) stop it. A Red Bird also turns to face its flight, from the launch until it first touches something.
--                Blocks of the material "light" (ice, glass) break at once; everything else gets a push of laserMod.push (a speed, so heavy blocks are pushed as hard as light ones) and loses strength.
--   waveShader:  a GLSL shader of your own ("heatwave", made here with createShader) makes the whole background, the sky included, shimmer: sideways and up and down, and the strength breathes.
--   gravityZone: a sensor ball in front of the slingshot. Whatever flies in gets its own upwards gravity and a violet tint until it leaves again. Right click anywhere in a level to move the zone there.
laserMod = {
	laserBird = true,
	waveShader = true,
	gravityZone = true,

	birdNames = { "RedBird", "BIRD_RED" },   -- a bird whose name or definition contains one of these can fire the laser
	slowFactor = 0.25,    -- the physics run at this speed while the laser is out (physicsSpeedFactor; 1 = normal)
	slowTime = 0.6,       -- real seconds the slow motion and the beam last
	maxRange = 1000,      -- the longest the beam can be, in physics units (the slingshot of the first level is at x = -12, its pigs at x = 42)
	blockMaterials = { rock = true },   -- the beam stops at these materials and at everything that has no mass (the ground, "immovable" blocks)
	rotateExclude = { "[Bb][Oo][Oo][Mm][Ee][Rr][Aa][Nn][Gg]" },   -- (3.6) birds whose name or definition matches one of these are not turned (the Boomerang Bird)
	rotateBirds = true,   -- every bird points in its direction of flight (only the picture is turned, with setVisualRotation; a bird whose definition has rotateWhileFlying = true is turned by the game itself, as Blu is in Rio, and is left alone, and so is the Boomerang Bird)
	damage = 25,          -- strength taken from every object the beam crosses
	breakMaterials = { light = true },   -- objects of these materials (ice and glass are "light") break at once
	push = 30,            -- the speed the beam gives what it crosses along its direction (physics units per second; the impulse is this times the object's mass)
	ignore = { "^zone_" },   -- the beam goes through these (the sensor zone)
	shapeSides = 8,       -- the beam is a shape cast (rayCast with a shape table): a regular polygon with this many sides (8 = an octagon; 0 = a plain ray) ...
	shapeRadius = nil,    -- ... this big, centre to corner, in physics units (nil: the size of the bird, its radius from objects.world)
	shapeRadiusDefault = 0.8,   -- when the bird's size is not known
	beamTime = 0.15,      -- real seconds the beam needs to reach the cursor position it was fired at (cached when the laser fires) ...
	beamEase = "inCubic", -- ... with this easing (a name of motion.easing, or a function(t)): it starts slowly and lunges at the end. Whatever the beam crosses is hit when its head passes it
	minSpeed = 3,         -- the bird must fly at least this fast to fire (a bird that waits on the ground does not)

	waveAmplitude = 4,    -- sideways shimmer in sheet pixels, it breathes by +-waveBreath
	waveAmplitudeY = 6,   -- up and down shimmer: this is what moves the sky, a picture that only changes from top to bottom
	waveBreath = 3,
	waveSpeed = 4,
	waveTint = { 0.85, 0.95, 1.1 },   -- the colour the shimmer multiplies the background with (a little blue)

	zoneName = "zone_vortex_1",
	zoneOffsetX = 20,     -- the zone sits this far in front of the slingshot and
	zoneOffsetY = -2,     -- this far above it (negative y is up)
	zoneRadius = 7,
	zoneGravityY = -15,   -- the pull inside the zone, in physics units per second squared (negative: upwards)
	zoneTint = { 0.8, 0.4, 1.0 },
}

do
	local find, sqrt, sin, pairs, ipairs, type = _G.string.find, _G.math.sqrt, _G.math.sin, _G.pairs, _G.ipairs, _G.type
	local laserTimer, beam = 0, nil
	local beamEaseOf = function(t) return t end
	local savedSpeed = nil
	local fired = {}
	local landed = {}     -- birds that have touched something since their launch: they stop turning
	local wasTouching = false
	local shaderTime, shaderReady = 0, false
	local zonePending, zoneMade = false, false
	local inside = {}
	local tinted = {}     -- objects that carry the zone's tint: the tint belongs to the name, so it is taken off when the object leaves, is removed, or the level changes

	-- the game's own transform when it has one (Classic does), else the camera: physics units are 20 per world unit
	local function toScreen(x, y)
		if type(physicsToScreenTransform) == "function" then return physicsToScreenTransform(x, y) end
		local camX, camY, ws = getCamera()
		return (x * 20 - camX) * ws, (y * 20 - camY) * ws
	end

	local function isRedBird(name, obj)
		for _, p in ipairs(laserMod.birdNames) do
			if find(name, p, 1, true) or find(obj.definition or "", p, 1, true) then return true end
		end
		return false
	end

	local function ignored(name, obj)
		for _, p in ipairs(laserMod.ignore) do
			if find(name, p) or find(obj.definition or "", p) then return true end
		end
		return false
	end

	local turnedBirds = {}
	-- birds that turn by themselves or must not turn: the Boomerang Bird flies a path of its own, and a bird whose definition has rotateWhileFlying is turned by the game
	local function rotateExcluded(name, obj)
		for _, p in ipairs(laserMod.rotateExclude) do
			if find(name, p) or find(obj.definition or "", p) then return true end
		end
		local blocks = blockTable ~= nil and blockTable.blocks or nil
		local def = blocks ~= nil and blocks[obj.definition or ""] or nil
		return def ~= nil and def.rotateWhileFlying == true
	end

	-- any bird, in any game: its definition says group "birds", or its name or definition has "bird" in it
	local function isBird(name, obj)
		local blocks = blockTable ~= nil and blockTable.blocks or nil
		local def = blocks ~= nil and blocks[obj.definition or ""] or nil
		if def ~= nil then
			if def.rotateWhileFlying then return false end
			if def.group == "birds" then return true end
		end
		return find(name, "[Bb][Ii][Rr][Dd]") ~= nil or find(obj.definition or "", "[Bb][Ii][Rr][Dd]") ~= nil
	end

	local function endSlowMotion()
		if savedSpeed ~= nil then
			physicsSpeedFactor = savedSpeed ~= 1 and savedSpeed or nil
			savedSpeed = nil
		end
		beam = nil
		laserTimer = 0
	end

	local function fireLaser(bird, x, y, vx, vy)
		-- the aim is the cursor; without one (or with the cursor on the bird) the bird's own direction
		local tx, ty = x + vx, y + vy
		if cursor ~= nil then
			if type(screenToPhysicsTransform) == "function" then
				tx, ty = screenToPhysicsTransform(cursor.x, cursor.y)
			else
				local camX, camY, ws = getCamera()
				tx, ty = (cursor.x / ws + camX) / 20, (cursor.y / ws + camY) / 20
			end
		end
		local len = sqrt((tx - x) * (tx - x) + (ty - y) * (ty - y))
		if len < 0.01 then tx, ty = x + vx, y + vy len = sqrt(vx * vx + vy * vy) end
		if len < 0.001 then return end
		local dx, dy = (tx - x) / len, (ty - y) / len
		len = len < laserMod.maxRange and len or laserMod.maxRange
		local ex, ey = x + dx * len, y + dy * len
		if savedSpeed == nil then savedSpeed = physicsSpeedFactor or 1 end
		physicsSpeedFactor = laserMod.slowFactor
		laserTimer = laserMod.slowTime
		-- the shape cast: an octagon the size of the bird, swept from the bird to the cached cursor position (with shapeSides = 0 it is a plain ray)
		local shape = nil
		if laserMod.shapeSides >= 3 then
			local r = laserMod.shapeRadius or (objects.world[bird] ~= nil and objects.world[bird].radius) or laserMod.shapeRadiusDefault
			shape = { sides = laserMod.shapeSides, radius = r, angle = 0, skip = bird }
		end
		local hits = rayCast(x, y, ex, ey, true, shape)      -- nearest first
		local list, endFrac = {}, 1
		if hits ~= nil then
			for _, hit in ipairs(hits) do
				local target = objects.world[hit.name]
				if hit.name ~= bird and target ~= nil and not ignored(hit.name, target) then
					if (target.mass or 0) <= 0 or laserMod.blockMaterials[target.material] then
						endFrac = hit.fraction       -- rock and the immovable stop the beam here
						break
					end
					list[#list + 1] = hit
				end
			end
		end
		local f = laserMod.beamEase
		if type(f) == "string" then f = motion ~= nil and motion.ease ~= nil and motion.ease(f) or nil end
		beamEaseOf = type(f) == "function" and f or function(t) return t end
		beam = { x1 = x, y1 = y, x2 = x + (ex - x) * endFrac, y2 = y + (ey - y) * endFrac, dx = dx, dy = dy, endFrac = endFrac, age = 0, head = 0, pending = list, shape = shape, r = shape ~= nil and shape.radius or 0 }
	end

	-- the beam's head advances with the ease; whatever it has reached is hit now
	local function applyHit(hit, dx, dy)
		local target = objects.world[hit.name]
		if target == nil then return end
		local mass = target.mass or 0
		applyImpulse(hit.name, dx * laserMod.push * mass, dy * laserMod.push * mass, hit.x, hit.y)
		if target.strength ~= nil then
			if laserMod.breakMaterials[target.material] then target.strength = 0
			else target.strength = target.strength - laserMod.damage end
		end
	end
	local function growBeam(step)
		if beam == nil or beam.head >= 1 then return end
		beam.age = beam.age + step
		local t = laserMod.beamTime > 0 and beam.age / laserMod.beamTime or 1
		if t > 1 then t = 1 end
		beam.head = beamEaseOf(t)
		if t >= 1 then beam.head = 1 end
		local reached = beam.head * beam.endFrac
		local i = 1
		while i <= #beam.pending do
			local hit = beam.pending[i]
			if hit.fraction <= reached or beam.head >= 1 then
				applyHit(hit, beam.dx, beam.dy)
				_G.table.remove(beam.pending, i)
			else i = i + 1 end
		end
	end

	------------------------------------------------ the sensor zone
	-- Box2D reports a body that enters or leaves a sensor to these two global functions (they were already there if another script defined them: that one is called too)
	local oldEnter, oldExit = enterCollision, exitCollision
	local function zoneAndOther(a, b)
		if find(a, "^zone_vortex") then return a, b end
		if find(b, "^zone_vortex") then return b, a end
		return nil
	end

	enterCollision = function(a, b)
		if oldEnter then oldEnter(a, b) end
		if not laserMod.gravityZone then return end
		local zone, other = zoneAndOther(a, b)
		if zone == nil or objects.world[other] == nil then return end
		local key = zone .. ":" .. other
		inside[key] = (inside[key] or 0) + 1      -- a body with two fixtures reports twice: count, act on the first
		if inside[key] == 1 then
			setObjectGravity(other, 0, laserMod.zoneGravityY)
			setTint(laserMod.zoneTint[1], laserMod.zoneTint[2], laserMod.zoneTint[3], other)
			tinted[other] = true
		end
	end

	exitCollision = function(a, b)
		if oldExit then oldExit(a, b) end
		local zone, other = zoneAndOther(a, b)
		if zone == nil then return end
		local key = zone .. ":" .. other
		if inside[key] ~= nil then
			inside[key] = inside[key] - 1
			if inside[key] <= 0 then
				inside[key] = nil
				if objects.world[other] ~= nil then resetObjectGravity(other) end
				resetTint(other)
				tinted[other] = nil
			end
		end
	end

	-- the aiming aid (section 2) asks the engine for the arc of the bird; the engine reads this global and bends the arc inside the zone, as the real flight bends
	local function publishZone()
		local z = laserMod.gravityZone and objects.world ~= nil and objects.world[laserMod.zoneName] or nil
		if z ~= nil then aimGravityZones = { { x = z.x, y = z.y, r = laserMod.zoneRadius, gx = 0, gy = laserMod.zoneGravityY } }
		else aimGravityZones = nil end
	end

	local function makeZone()
		-- the sensor is a circle without a sprite, density 0 so it never moves
		if objects.world[laserMod.zoneName] ~= nil then zoneMade = true return end
		local x, y = levelStartPosition.x + laserMod.zoneOffsetX, levelStartPosition.y + laserMod.zoneOffsetY
		createCircle(laserMod.zoneName, "", x, y, laserMod.zoneRadius, 0, 0, 0, false, 1)
		setAsSensor(laserMod.zoneName, true)
		-- the game's explosions (TNT) compare the strength and defence of everything in their radius: a sensor without one made makeExplosion fail
		local z = objects.world[laserMod.zoneName]
		if z ~= nil then z.strength = z.strength or 1e9 z.defence = z.defence or 1e9 end
		zoneMade = true
	end

	------------------------------------------------ a level has loaded
	local function clearTints()
		for name in pairs(tinted) do resetTint(name) end
		tinted = {}
	end
	engine.on("objectRemoved", function(name)
		if tinted[name] then resetTint(name) tinted[name] = nil end
	end)

	engine.on("levelLoaded", function(path)
		endSlowMotion()
		clearTints()          -- the objects of a new level, or of a restart, have the same names as the old ones
		fired, inside, landed = {}, {}, {}
		zoneMade = false
		zonePending = laserMod.gravityZone      -- the slingshot position is only known a little later: the zone is made in the first game frame
		if laserMod.waveShader and type(createShader) == "function" then
			local ok, err = createShader("heatwave", [[
vec4 shade(vec4 texel, vec2 uv, vec4 tint) {
    float amp = u_param0.x * u_texel.x;
    float speed = u_param0.y;
    vec2 bent = uv;
    bent.x += sin(uv.y * 40.0 + u_time * speed) * amp;
    bent.y += sin(uv.x * 25.0 + u_time * speed * 0.7) * u_param0.z * u_texel.y;
    vec4 c = sampleTex(bent);
    c.rgb *= u_param1.rgb;
    return c * tint;
}
]])
			if ok then
				shaderReady = true
				setShader("heatwave", "background")
				setShaderParam("heatwave", 0, laserMod.waveAmplitude, laserMod.waveSpeed, laserMod.waveAmplitudeY, 0)
				setShaderParam("heatwave", 1, laserMod.waveTint[1], laserMod.waveTint[2], laserMod.waveTint[3], 1)
			else
				print("laser mod: the heatwave shader does not compile: " .. _G.tostring(err))
			end
		end
	end)

	engine.on("contact", function(a, b)
		if find(a, "^zone_") or find(b, "^zone_") then return end     -- flying into the sensor zone is not landing
		landed[a] = true
		landed[b] = true
	end)

	-- the colour the game clears the screen with is what the sky is above its picture. A flat colour has nothing to shimmer, but it must get the same tint as the picture,
	-- or a band of the old colour stays at the top of the screen: drawn first, under the layers (drawRect takes 0 to 1 here)
	engine.on("drawBackgroundBefore", function()
		if laserMod.waveShader and shaderReady then
			local r, g, b = getBGColor()
			local t = laserMod.waveTint
			drawRect(r / 255 * t[1], g / 255 * t[2], b / 255 * t[3], 1, 0, 0, screenWidth, screenHeight, false)
		end
	end)

	------------------------------------------------ every frame
	engine.on("update", function(dt, realDt)
		local step = realDt or dt

		if shaderReady then
			if laserMod.waveShader then
				shaderTime = shaderTime + step
				setShaderParam("heatwave", 0, laserMod.waveAmplitude + sin(shaderTime * 2) * laserMod.waveBreath, laserMod.waveSpeed, laserMod.waveAmplitudeY + sin(shaderTime * 2) * laserMod.waveBreath, 0)
			else
				setShader("none", "background")
				shaderReady = false
			end
		end

		publishZone()
		if zonePending and currentGameMode == updateGame and levelStartPosition ~= nil and levelStartPosition.x ~= 0 then
			zonePending = false
			makeZone()
		end

		growBeam(step)
		if laserTimer > 0 then
			laserTimer = laserTimer - step
			if laserTimer <= 0 then endSlowMotion() end
		end

		-- right click: the zone moves to the cursor
		if laserMod.gravityZone and zoneMade and keyPressed ~= nil and keyPressed["RBUTTON"] and currentGameMode == updateGame and objects.world[laserMod.zoneName] ~= nil then
			local x, y
			if type(screenToPhysicsTransform) == "function" then
				x, y = screenToPhysicsTransform(cursor.x, cursor.y)
			else
				local camX, camY, ws = getCamera()
				x, y = (cursor.x / ws + camX) / 20, (cursor.y / ws + camY) / 20
			end
			setPosition(laserMod.zoneName, x, y)
		end

		if laserMod.rotateBirds and currentGameMode == updateGame and objects.world ~= nil and birdSelected ~= true then
			-- (3.6) only the picture is turned (setVisualRotation adds to the body's angle), the physics body is left alone; a bird that has landed is turned back
			for name, obj in pairs(objects.world) do
				if isBird(name, obj) then
					if landed[name] == nil then
						local speed, vx, vy = getVelocity(name)
						if speed > laserMod.minSpeed and not rotateExcluded(name, obj) then
							setVisualRotation(_G.math.atan2(vy, vx) - (obj.angle or 0), name)
							turnedBirds[name] = true
						end
					elseif turnedBirds[name] then
						setVisualRotation(0, name)
						turnedBirds[name] = nil
					end
				end
			end
		end

		local touching = touchcount ~= nil and touchcount > 0
		local pressed = (touching and not wasTouching) or (keyPressed ~= nil and keyPressed["SPACE"])
		wasTouching = touching
		if laserMod.laserBird and pressed and currentGameMode == updateGame and objects.world ~= nil then
			for name, obj in pairs(objects.world) do
				if fired[name] == nil and isRedBird(name, obj) and birdSelected ~= true then
					local speed, vx, vy = getVelocity(name)
					if speed > laserMod.minSpeed then
						fired[name] = true
						fireLaser(name, obj.x, obj.y, vx, vy)
						break
					end
				end
			end
		end
	end)

	------------------------------------------------ drawing: the beam and the outline of the zone, on top of the scene
	engine.on("drawGame", function()
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
		if beam ~= nil then
			local hx, hy = beam.x1 + (beam.x2 - beam.x1) * beam.head, beam.y1 + (beam.y2 - beam.y1) * beam.head
			local x1, y1 = toScreen(beam.x1, beam.y1)
			local x2, y2 = toScreen(hx, hy)
			local ex = toScreen(beam.x1 + beam.r, beam.y1)
			local rs = ex - x1                       -- the beam's half width on the screen
			if rs > 1 then
				drawLine2D(x1, y1, x2, y2, rs * 2, 255, 40, 40, 90)
				local sh = beam.shape
				if sh ~= nil then                    -- the octagon at the head and at the start
					local n, px, py = sh.sides, nil, nil
					for k = 0, n do
						local a = 6.2831853 * (k % n) / n
						local ox, oy = rs * _G.math.cos(a), rs * _G.math.sin(a)
						if px ~= nil then
							drawLine2D(px, py, x2 + ox, y2 + oy, 2, 255, 255, 255, 255)
						end
						px, py = x2 + ox, y2 + oy
					end
				end
			end
			drawLine2D(x1, y1, x2, y2, 3, 255, 255, 255, 255)
		end
		local z = laserMod.gravityZone and objects.world ~= nil and objects.world[laserMod.zoneName] or nil
		if z ~= nil then
			local sx, sy = toScreen(z.x, z.y)
			local ex = toScreen(z.x + laserMod.zoneRadius, z.y)
			local r = ex - sx
			drawRectLines(sx - r, sy - r, sx + r, sy + r, 2, 180, 80, 255, 160)
		end
	end)
end

------------------------------------------------------------------ 4. restart (3.3)
-- The restart button (and F5, and the restart buttons of the pause and the end-of-level pages) first moves every object that is left back to where the level began, as a tween
-- (motion.to, so the movement can be changed here), and then restarts the level with no screen wipe. Objects that were destroyed come back with the restart itself.
-- The entrance to a level, and every other wipe, are as before.
levelRestart = {
	enabled = true,
	time = 0.8,             -- seconds the objects need to get back (real seconds)
	ease = "inCubic",       -- ease in only: slow at first, fastest at the end (any easing of motion.easing, or a function)
	minDistance = 0.02,     -- an object that is closer than this (physics units) to its place is left where it is
}

do
	local pi, abs, sqrt, pairs, type, find = _G.math.pi, _G.math.abs, _G.math.sqrt, _G.pairs, _G.type, _G.string.find
	local snapshot, snapPending, restarting = nil, false, false
	local snapLevel = nil     -- which level the snapshot is of: a restart loads the same one
	local function levelId() return _G.tostring(levelFolder) .. "/" .. _G.tostring(levelName) end

	engine.on("levelLoaded", function()
		snapshot, snapPending, restarting = nil, true, false
	end)

	engine.on("update", function(dt, realDt)
		if screenwipe.skip then
			screenwipe.skipTime = screenwipe.skipTime - (realDt or dt)
			if screenwipe.skipTime <= 0 then screenwipe.skip = false end
		end
		-- the places the game loads the level in: written down in the first game frame (when the slingshot position is known, as in section 3)
		if snapPending and currentGameMode == updateGame and levelStartPosition ~= nil and levelStartPosition.x ~= 0 and objects.world ~= nil then
			snapPending = false
			snapLevel = levelId()
			snapshot = {}
			for name, obj in pairs(objects.world) do
				if obj.x ~= nil and (obj.mass or 0) > 0 and not find(name, "^zone_") then snapshot[name] = { x = obj.x, y = obj.y, angle = obj.angle or 0 } end
			end
		end
	end)

	-- A restart is a call of setGameMode(updateLoading) that is made either by the restart button in the level itself (while the game mode is updateGame, with no menu page open),
	-- or by a menu item with restartLevel = true (the pause page, the end-of-level pages). That call is held back until the objects are home.
	local originalSetGameMode2 = setGameMode
	if type(originalSetGameMode2) == "function" and type(motion) == "table" then
		setGameMode = function(mode, ...)
			local item = page ~= nil and page.items ~= nil and selectedMenuItem ~= nil and page.items[selectedMenuItem] or nil
			-- only the same level again is a restart: the menu item that was used last can be a restart button that is still selected when another level is picked,
			-- so the level that is about to load must be the one the snapshot is of
			-- (a level button of the level selection, or the menu, is never a restart: coming back to a level from the menu is a visit)
			if mode == updateMenu then snapLevel = nil end
			local isRestart = snapLevel ~= nil and levelId() == snapLevel and ((item ~= nil and item.restartLevel) or (item == nil and currentGameMode == updateGame))
			if isRestart and (mode == updateLoading or mode == updateLoadingEx) and _G.type(saveStates) == "table" and saveStates.forget then saveStates.forget() end   -- a restart starts the level again, from the beginning
			if (mode ~= updateLoading and mode ~= updateLoadingEx) or not levelRestart.enabled or not isRestart or snapshot == nil or objects.world == nil then
				return originalSetGameMode2(mode, ...)
			end
			if restarting then return end
			local args = { ... }
			local waiting = 0
			local function restartNow()
				restarting = false
				screenwipe.skip, screenwipe.skipTime = true, 5
				return originalSetGameMode2(mode, _G.unpack(args))
			end
			restarting = true
			-- a bird that split (the Blue Bird) is no longer in the world under its name: its pieces, which carry the name as parentName or as the start of theirs
			-- (the one that is flyingBird first), go back to its place instead
			local pieces = {}
			for name, place in pairs(snapshot) do
				if objects.world[name] == nil then
					for oname, o in pairs(objects.world) do
						if snapshot[oname] == nil and o.x ~= nil and (o.parentName == name or find(oname, name, 1, true) == 1) then
							pieces[#pieces + 1] = { name = oname, place = place, main = o == flyingBird }
						end
					end
				end
			end
			_G.table.sort(pieces, function(a, b) if a.main ~= b.main then return a.main end return a.name < b.name end)
			local pieceOf = {}
			for _, pc in _G.ipairs(pieces) do if pieceOf[pc.name] == nil then pieceOf[pc.name] = pc.place end end
			for name, place in pairs(pieceOf) do snapshot[name] = place end
			for name, place in pairs(snapshot) do
				local obj = objects.world[name]
				if obj ~= nil and obj.x ~= nil then
					local da = (place.angle - (obj.angle or 0) + pi) % (2 * pi) - pi    -- the short way round
					local dx, dy = place.x - obj.x, place.y - obj.y
					if sqrt(dx * dx + dy * dy) > levelRestart.minDistance or abs(da) > 0.02 then
						waiting = waiting + 1
						motion.to(name, { dx = dx, dy = dy, dangle = da, time = levelRestart.time, ease = levelRestart.ease, realTime = true, release = "stop",
							onDone = function() waiting = waiting - 1 if waiting == 0 then restartNow() end end })
					end
				end
			end
			if waiting == 0 then return restartNow() end
		end
	end
end

------------------------------------------------------------------ 5. shape blocks (3.4): a star and a wedge in the wood group; with holes (3.6)
-- Blocks of any outline. A block of type "polygon" has `vertices` (x, y from 0 to 1 over the block's size) and the physics takes concave outlines too (the engine splits them
-- into convex pieces, so a star or a chevron is one body that collides along its real edge). The sprite is cut to the same outline with createPolygonSprite, one for each
-- damage stage. The blocks are added to blockTable.blocks as WoodBlock13, 14 and 15 and to the wood group of the level editor (its W / Q keys), so a level that names them
-- loads them, and `createObject` makes one from a script. To add your own: put another entry in shapeBlocks.blocks (a name and a list of {x=, y=} points).
-- Blocks with holes (3.6): an entry can have `holes`, a list of shapes in 0..1 over the block ({type="box", x=, y=, w=, h=, angle=}, {type="circle", x=, y=, radius=},
-- {type="polygon", points={...}}), at any position, even over an edge or a corner. The physics is made with the holes cut out (registerHoles) and the sprites
-- are cut to match. WoodBlock16 is the wood square with a square hole in the middle, WoodBlock17 a wooden circle with three round cut-outs. An entry without
-- `vertices` is a box (`type = "box"`).
shapeBlocks = {
	enabled = true,
	source = "BLOCK_WOOD_4X4_",   -- the sprite of the undamaged block (this name plus 1); the damage stages are those of the template
	template = "WoodBlock9",      -- the block (the wood square) that supplies strength, material and the damage sprites that are cut
	outline = 3,                  -- pixels along the edge that are darkened, so the cut has an edge (0 = none)
	blocks = {},                  -- filled below: [name] = { vertices = { {x=, y=}, ... } }
}
do
	local cos, sin, pi = _G.math.cos, _G.math.sin, _G.math.pi
	local function star(points, outer, inner)
		local v = {}
		for i = 0, points * 2 - 1 do
			local r = i % 2 == 0 and outer or inner
			local a = -pi / 2 + i * pi / points
			v[#v + 1] = { x = 0.5 + r * cos(a), y = 0.5 + r * sin(a) }
		end
		return v
	end
	shapeBlocks.blocks.WoodBlock13 = { vertices = star(5, 0.5, 0.21) }                                                    -- a five pointed star (concave)
	shapeBlocks.blocks.WoodBlock14 = { vertices = { { x = 0, y = 1 }, { x = 1, y = 1 }, { x = 1, y = 0.25 } } }          -- a wedge (a door stop)
	local function circleOutline(n)
		local v = {}
		for i = 0, n - 1 do v[#v + 1] = { x = 0.5 + 0.5 * cos(2 * pi * i / n), y = 0.5 + 0.5 * sin(2 * pi * i / n) } end
		return v
	end
	shapeBlocks.blocks.WoodBlock16 = { type = "box", holes = { { type = "box", x = 0.5, y = 0.5, w = 0.5, h = 0.5 } } }    -- the wood square with a square hole in the middle
	shapeBlocks.blocks.WoodBlock17 = { vertices = circleOutline(40), holes = {                                                -- a wooden circle with three round cut-outs
		{ type = "circle", x = 0.5, y = 0.27, radius = 0.13 }, { type = "circle", x = 0.3, y = 0.62, radius = 0.1 }, { type = "circle", x = 0.69, y = 0.65, radius = 0.115 } } }
	shapeBlocks.blocks.WoodBlock15 = { vertices = { { x = 0, y = 0 }, { x = 0.35, y = 0 }, { x = 0.5, y = 0.55 }, { x = 0.65, y = 0 }, { x = 1, y = 0 }, { x = 0.62, y = 1 }, { x = 0.38, y = 1 } } }   -- a chevron (concave)

	local done = {}
	local function register()
		if not shapeBlocks.enabled or blockTable == nil or blockTable.blocks == nil then return end
		-- a level can load only the blocks it names, so the template is kept from the time the whole table was there
		local template = blockTable.blocks[shapeBlocks.template]
		if template ~= nil then shapeBlocks.templateCopy = template end
		template = template or shapeBlocks.templateCopy
		if template == nil then return end
		for name, def in _G.pairs(shapeBlocks.blocks) do
			if done[name] == nil and blockTable.blocks[name] == nil then
				local copy = { type = def.type or (def.vertices ~= nil and "polygon" or "box"), controllable = false, group = template.group, material = template.material, strength = template.strength,
					groupIndex = _G.tonumber(name:match("(%d+)$")) or 0, vertices = def.vertices, holes = def.holes, damageSprites = {} }
				copy.sprite = shapeBlocks.source .. "1"
				if def.holes ~= nil and _G.type(registerHoles) == "function" then registerHoles(name .. "_1", def.holes, def.vertices) end    -- the physics of any block made from this sprite gets the holes
				for stage, d in _G.pairs(template.damageSprites or {}) do copy.damageSprites[stage] = { sprite = d.sprite, min = d.min, max = d.max } end
				done[name] = { cut = false, def = copy }
			end
			-- the game can load blocks.lua again (a new table): the entry is put back
			if done[name] ~= nil and blockTable.blocks[name] ~= done[name].def and blockTable.blocks[name] == nil then blockTable.blocks[name] = done[name].def end
		end
		local group = blockTable.groups ~= nil and blockTable.groups[template.group] or nil
		if group ~= nil and group.lastIndex ~= nil then
			for name in _G.pairs(done) do
				local n = done[name].def.groupIndex
				if n > group.lastIndex then group.lastIndex = n end
			end
		end
	end
	local square = { { x = 0, y = 0 }, { x = 1, y = 0 }, { x = 1, y = 1 }, { x = 0, y = 1 } }
	-- the sprites come from sheets that a level loads, so they are cut when they exist
	local function cut()
		if _G.type(createPolygonSprite) ~= "function" or _G.type(spriteExists) ~= "function" then return end
		for name, state in _G.pairs(done) do
			if not state.cut then
				local def = state.def
				local ready = true
				for stage, d in _G.pairs(def.damageSprites) do if not spriteExists(d.original or d.sprite) then ready = false end end
				if ready and spriteExists(shapeBlocks.source .. "1") then
					for stage, d in _G.pairs(def.damageSprites) do
						d.original = d.original or d.sprite
						local cutName = name .. "_" .. stage
						createPolygonSprite(cutName, d.original, def.vertices or square, shapeBlocks.outline, 0.55, true, def.holes)
						d.sprite = cutName
					end
					createPolygonSprite(name .. "_1", shapeBlocks.source .. "1", def.vertices or square, shapeBlocks.outline, 0.55, true, def.holes)
					def.sprite = name .. "_1"
					state.cut = true
				end
			end
		end
	end
	-- any block of any game whose definition has `holes` gets them (blocks.lua tables of mods: { type = "box", sprite = "...", holes = { {type = "circle", x = 0.5, y = 0.5, radius = 0.2} } })
	local registeredHoles = {}
	local function registerDefinitionHoles()
		if _G.type(registerHoles) ~= "function" or blockTable == nil or blockTable.blocks == nil then return end
		for name, def in _G.pairs(blockTable.blocks) do
			if _G.type(def) == "table" and def.holes ~= nil and def.sprite ~= nil and registeredHoles[def.sprite] ~= def.holes then
				registeredHoles[def.sprite] = def.holes
				registerHoles(def.sprite, def.holes, def.vertices)
			end
		end
	end
	local function prepare() register() registerDefinitionHoles() cut() end
	prepare()
	engine.on("levelLoaded", prepare)
	engine.on("update", function() if shapeBlocks.enabled then prepare() end end)
	-- the game can load blocks.lua again between two frames (a new table) and make the objects of the level before this script's own update runs: the blocks are put back first
	if _G.type(createObject) == "function" then
		local createObjectBefore = createObject
		createObject = function(definitions, ...)
			if definitions == blockTable and shapeBlocks.enabled then register() registerDefinitionHoles() end
			return createObjectBefore(definitions, ...)
		end
	end
	-- a level file makes its objects before `levelLoaded`: the sprites are cut (and the holes known) before that
	local loadLevelBefore = loadLevel
	loadLevel = function(...)
		prepare()
		return loadLevelBefore(...)
	end
end


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

------------------------------------------------------------------ 8. level spawner (3.7, folders and preview 3.8, placing 3.8.1)
-- F1 in a level opens the spawner: the game stops and a small picture of the level shows what is chosen. Left / Right pick another level of the folder, Up / Down another level folder
-- (pack1, pack2, ... in the game's levels folder, any folder that has level files), and Return (or a click on the picture) puts that level's blocks, pigs and joints into the level that is being
-- played, around the cursor; Escape (or F1 again) closes it. A level file dropped on the window does the same without the panel. It works like the vehicles of Red's Mighty Feathers
-- (3.3.0), which are level files (`world` and `joints`) made at a place of the running level, every object with a new name (spawnPrefix + number + the old name) and every joint tied to the
-- new names. Every object is made the way the game's own level loader makes it (turned to its angle, with its material and the theme's texture, pigs counted as level goals), so static
-- blocks have their pictures and everything stands as it was saved. The ground and the slingshot's zone are left out. The birds of the spawned level join the line-up behind the birds
-- that are left (`levelSpawner.birds = false` leaves them out). Spawned objects are ordinary objects of the level: they take damage, they are in the save states.
levelSpawner = {
	enabled = true,
	key = "F1",
	closeKeys = { "ESCAPE", "F1", "KEY_BACK" },
	spawnKeys = { "RETURN", "SPACE" },
	prefix = "sp",             -- the names of spawned objects start with this and a number
	skip = { "Ground", "Slingshot" },   -- definitions that are not spawned
	birds = true,              -- (3.8) the birds of the spawned level join the line-up (false: they are left out, as in 3.7)
	preview = true,            -- (3.8) a small picture of the level in the panel
	placement = true,          -- (3.8.1) a click on the picture lets the level be moved about as a half-transparent copy; Return (or a click) puts it down, Escape goes back
	drop = true,               -- a level file dropped on the window is spawned too (false: it replaces the level, as in 3.1)
	title = "Spawn a level here",
}
do
	local type, pairs, ipairs, find, match, gsub, tonumber = _G.type, _G.pairs, _G.ipairs, _G.string.find, _G.string.match, _G.string.gsub, _G.tonumber
	local sort, floor, fmod, cos, sin, pi, max, min = _G.table.sort, _G.math.floor, _G.math.fmod, _G.math.cos, _G.math.sin, _G.math.pi, _G.math.max, _G.math.min
	local open = false
	local root, folders, folderIndex, files, fileIndex = "levels/", {}, 1, {}, 1
	local count = 0
	local panel, buttons = nil, {}
	local cache = {}          -- file -> the level's tables (or false), kept while the panel is open

	local function readLevel(file)
		local c = cache[file]
		if c ~= nil then return c or nil end
		local text = type(readDataFile) == "function" and readDataFile(file) or nil
		local env = nil
		if text ~= nil then
			local chunk = _G.loadstring(text, file)
			if chunk ~= nil then
				local e = {}
				_G.setfenv(chunk, e)
				if _G.pcall(chunk) and type(e.world) == "table" then env = e end
			end
		end
		cache[file] = env or false
		return env
	end

	local function natural(a, b)
		local pa, na = match(a, "^(.-)(%d+)$")
		local pb, nb = match(b, "^(.-)(%d+)$")
		if pa ~= nil and pa == pb then return tonumber(na) < tonumber(nb) end
		return a < b
	end

	-- the folders and the levels of a folder in the order the game loads them: `levelOrder.packs` and `levelOrder[pack]` (Level7 is the 9th level of pack1, so 1-9), then whatever else is there
	local function fileList(name)
		local present, out, seen = {}, {}, {}
		local list = type(listDataFolder) == "function" and listDataFolder(root .. name .. "/") or nil
		if list ~= nil then
			for _, e in ipairs(list) do
				if not e.dir and find(e.name, "%.lua$") and not find(e.name, "%.temp") then present[(gsub(e.name, "%.lua$", ""))] = true end
			end
		end
		local order = type(levelOrder) == "table" and levelOrder[name] or nil
		if type(order) == "table" then
			for i = 1, #order do
				local n = order[i]
				if present[n] and not seen[n] then out[#out + 1] = n seen[n] = true end
			end
		end
		local rest = {}
		for n in pairs(present) do if not seen[n] then rest[#rest + 1] = n end end
		sort(rest, natural)
		for _, n in ipairs(rest) do out[#out + 1] = n end
		return out
	end

	local function packNumber(name)
		if type(levelOrder) == "table" and type(levelOrder.packs) == "table" then
			for i, n in ipairs(levelOrder.packs) do if n == name then return i end end
		end
		return nil
	end

	-- the place of a level in the game's order of its folder (Level7 is 9th in pack1: 1-9)
	local function gameIndex(folder, name)
		local order = type(levelOrder) == "table" and levelOrder[folder] or nil
		if type(order) == "table" then for i = 1, #order do if order[i] == name then return i end end end
		return nil
	end

	local function start()
		local f = gsub(_G.tostring(levelFolder or "levels/"), "\\", "/")
		if not find(f, "/$") then f = f .. "/" end
		local base, pack = match(f, "^(.*/)([^/]+)/$")
		root, pack = base or f, pack or ""
		folders = {}
		local list = type(listDataFolder) == "function" and listDataFolder(root) or nil
		local present = {}
		if list ~= nil then
			for _, e in ipairs(list) do
				if e.dir and e.name ~= "temp" and #fileList(e.name) > 0 then present[e.name] = true end
			end
		end
		if type(levelOrder) == "table" and type(levelOrder.packs) == "table" then
			for _, n in ipairs(levelOrder.packs) do
				if present[n] then folders[#folders + 1] = n present[n] = nil end
			end
		end
		local rest = {}
		for n in pairs(present) do rest[#rest + 1] = n end
		sort(rest, natural)
		for _, n in ipairs(rest) do folders[#folders + 1] = n end
		folderIndex = 0
		for i, n in ipairs(folders) do if n == pack then folderIndex = i end end
		if folderIndex == 0 then folders[#folders + 1] = pack folderIndex = #folders end
		files = fileList(folders[folderIndex])
		fileIndex = 0
		for i, n in ipairs(files) do if n == _G.tostring(levelName) then fileIndex = i end end
		if fileIndex == 0 then files[#files + 1] = _G.tostring(levelName) fileIndex = #files end
		cache = {}
	end

	local function pathOf() return root .. folders[folderIndex] .. "/" .. files[fileIndex] .. ".lua" end

	local function stepFile(dir) if #files > 0 then fileIndex = (fileIndex - 1 + dir) % #files + 1 end end

	local function stepFolder(dir)
		for _ = 1, #folders do
			folderIndex = (folderIndex - 1 + dir) % #folders + 1
			local list = fileList(folders[folderIndex])
			if #list > 0 then files, fileIndex = list, 1 return end
		end
	end

	local function isSkipped(def)
		for _, s in ipairs(levelSpawner.skip) do if def == s then return true end end
		local b = blockTable.blocks[def]
		if b == nil then return true end
		return b.controllable == true and not levelSpawner.birds
	end

	-- one object the way loadLevelInternal makes it: created, turned, material, the theme's texture, the sprites of its damage and, for a pig, a level goal
	local function make(o, name, x, y)
		local ok, made = _G.pcall(createObject, blockTable, o.definition, name, x, y)
		made = made or name
		local v = ok and objects.world[made] or nil
		if v == nil then return nil end
		local a = fmod(o.angle or 0, pi * 2)
		if a < 0 then a = a + pi * 2 end
		setRotation(made, a)
		setMaterial(made, v.material)
		if v.texture ~= nil then
			local theme = blockTable.themes ~= nil and blockTable.themes[currentTheme] or nil
			setTexture(made, theme ~= nil and theme.texture or v.texture)
		end
		if v.levelGoal then
			v.blinkTimer = _G.math.random(5, 30) / 10
			v.oinkTimer = _G.math.random(5, 30) / 10
			if levelGoals ~= nil then levelGoals[made] = v end
			setObjectParameter(made, 1, 1)
		end
		if type(getDamageSprite) == "function" then
			local ok2, sprites = _G.pcall(getDamageSprite, v, blockTable.blocks)
			if ok2 and sprites ~= nil then v.damageSprite, v.blinkSprite, v.smileSprite = sprites.sprite, sprites.blink, sprites.smile end
		end
		v.frozen = false
		return made, v
	end

	-- where the next bird of the line-up stands: behind the waiting bird that is last, one gap further (the gap between the waiting birds, 3.4 when there is one or none)
	local function lineEnd()
		local lastX, lastY, gap, waiting = nil, nil, 3.4, {}
		for n, v in pairs(objects.world) do
			if v.controllable and v.startNumber ~= nil and not v.shot and v ~= flyingBird and n ~= birdToSlingshotBirdName and n ~= currentBirdName then waiting[#waiting + 1] = v end
		end
		sort(waiting, function(a, b) return a.x > b.x end)
		if #waiting >= 2 then gap = max(1.5, waiting[1].x - waiting[2].x) end
		if #waiting >= 1 then lastX, lastY = waiting[#waiting].x, waiting[#waiting].y end
		if lastX == nil then
			lastX = levelStartPosition ~= nil and levelStartPosition.x or -10     -- no bird waits: the line starts at the slingshot, on the ground
			lastY = levelStartPosition ~= nil and levelStartPosition.y + 7.35 or -1.8
		end
		return lastX, lastY, gap
	end

	local function spawn(file)
		local data = readLevel(file or pathOf())
		if data == nil then return false end
		local list, birdList, cx, cy, n = {}, {}, 0, 0, 0
		for name, o in pairs(data.world) do
			if type(o) == "table" and o.definition ~= nil and o.x ~= nil and not find(name, "^zone_") and not isSkipped(o.definition) then
				o.name = o.name or name
				if blockTable.blocks[o.definition].controllable then birdList[#birdList + 1] = o
				else list[#list + 1] = o cx, cy, n = cx + o.x, cy + o.y, n + 1 end
			end
		end
		if n == 0 and #birdList == 0 then return false end
		if n > 0 then cx, cy = cx / n, cy / n end
		local tx, ty = cx, cy
		if cursor ~= nil and type(screenToPhysicsTransform) == "function" then tx, ty = screenToPhysicsTransform(cursor.x, cursor.y)
		elseif cursor ~= nil and type(getCamera) == "function" then
			local camX, camY, ws = getCamera()
			tx, ty = (cursor.x / ws + camX) / 20, (cursor.y / ws + camY) / 20
		end
		count = count + 1
		local names = {}
		for _, o in ipairs(list) do
			local made = make(o, levelSpawner.prefix .. count .. "_" .. o.name, o.x - cx + tx, o.y - cy + ty)
			if made ~= nil then names[o.name] = made end
		end
		-- the birds: in the order of the spawned level, behind the birds that are left, and made static as the birds that wait are
		sort(birdList, function(a, b)
			if a.startNumber ~= nil and b.startNumber ~= nil and a.startNumber ~= b.startNumber then return a.startNumber < b.startNumber end
			return natural(a.name, b.name)
		end)
		if #birdList > 0 then
			local lx, ly, gap = lineEnd()
			for _, o in ipairs(birdList) do
				lx = lx - gap
				local made, v = make(o, levelSpawner.prefix .. count .. "_" .. o.name, lx, ly)
				if made ~= nil then
					names[o.name] = made
					setRotation(made, 0)
					birdsCounter = (birdsCounter or 0) + 1
					v.startNumber = birdsCounter
					v.animTimer = _G.math.random(10, 30) / 10
					v.jumpTimer = _G.math.random(10, 30) / 10
					setObjectParameter(made, 2, 0)
					birds[made] = v
				end
			end
		end
		if type(data.joints) == "table" then
			for _, j in pairs(data.joints) do
				if type(j) == "table" and names[j.end1] ~= nil and names[j.end2] ~= nil then
					local copy = {}
					for k, v in pairs(j) do copy[k] = v end
					copy.end1, copy.end2 = names[j.end1], names[j.end2]
					copy.name = copy.end1 .. copy.end2
					_G.pcall(createJoint, copy)
				end
			end
		end
		return true
	end

	local placing = nil
	local previewOf, previewRect      -- defined further down, used by handle
	local function close() open, modalOpen, modalInput, cache, placing = false, false, nil, {}, nil end

	local function anyKey(list, pressed)
		for _, k in ipairs(list) do if pressed[k] then return true end end
		return false
	end

	local function inside(r, x, y) return r ~= nil and x >= r.x1 and x <= r.x2 and y >= r.y1 and y <= r.y2 end

	engine.on("levelLoaded", function() open = false end)

	-- a level file dropped on the window while a level is played is spawned at the cursor instead of replacing the level (outside a level, or with the spawner off, as before)
	local originalDropped = onFileDropped
	onFileDropped = function(path)
		if levelSpawner.enabled and levelSpawner.drop and currentGameMode == updateGame and objects ~= nil and objects.world ~= nil then
			if open then close() end
			if spawn(gsub(_G.tostring(path), "\\", "/")) then _G.print("spawned " .. _G.tostring(path) .. "\n") return true end
			_G.print("not spawned (not a level file?): " .. _G.tostring(path) .. "\n")
		end
		if type(originalDropped) == "function" then return originalDropped(path) end
	end

	local function handle()
		if not levelSpawner.enabled then return end
		if not open then
			if keyPressed ~= nil and keyPressed[levelSpawner.key] and currentGameMode == updateGame and objects.world ~= nil and not modalOpen then
				start()
				modalInput = nil
				open, modalOpen = true, true
			end
			return
		end
		local input = modalInput
		if input == nil then return end
		local p = input.pressed
		if placing ~= nil then
			if anyKey(levelSpawner.closeKeys, p) then placing = nil
			elseif anyKey(levelSpawner.spawnKeys, p) or input.clicked then
				local file = placing
				placing = nil
				spawn(file)
				close()
			end
			return
		end
		if anyKey(levelSpawner.closeKeys, p) then close() return end
		if p["LEFT"] then stepFile(-1) elseif p["RIGHT"] then stepFile(1) elseif p["UP"] then stepFolder(1) elseif p["DOWN"] then stepFolder(-1) end
		if input.clicked then
			for _, b in ipairs(buttons) do
				if inside(b, input.x, input.y) then b.action() return end
			end
		end
		if levelSpawner.placement and input.clicked and inside(previewRect, input.x, input.y) and previewOf(pathOf()) ~= nil then
			placing = pathOf()
		elseif anyKey(levelSpawner.spawnKeys, p) or (input.clicked and inside(panel, input.x, input.y)) then
			spawn()
			close()
		elseif input.clicked then close() end
	end

	-- the level's blocks drawn with their own pictures (static blocks, which the game fills with the theme's texture, and blocks without a picture here are bars: a line as thick as the block, turned like it)
	local function colourOf(b, def)
		if b.controllable then
			if find(def, "BigBrother") or find(_G.tostring(b.sprite), "BIG_BROTHER") then return 120, 20, 45 end   -- the giant Red Bird: maroon
			return 230, 60, 60
		end
		if b.levelGoal then return 90, 200, 90 end
		local s = b.sprite or ""
		if find(s, "LIGHT") or find(s, "GLASS") or find(s, "ICE") then return 150, 215, 250 end
		if find(s, "WOOD") then return 190, 130, 70 end
		if find(s, "STONE") or find(s, "HEAVY") then return 165, 165, 165 end
		if b.material == "immovable" then return 85, 68, 50 end
		return 205, 205, 205
	end

	local function spriteOf(b)
		if b.damageSprites ~= nil and b.damageSprites.damage1 ~= nil then return b.damageSprites.damage1.sprite end
		return b.sprite
	end

	function previewOf(file)
		local c = cache["preview " .. file]
		if c ~= nil then return c or nil end
		local data = readLevel(file)
		local items, x1, y1, x2, y2 = {}, 1e9, 1e9, -1e9, -1e9
		local ps = physicsScale or 0.05
		if data ~= nil then
			for name, o in pairs(data.world) do
				local b = type(o) == "table" and o.definition ~= nil and o.x ~= nil and blockTable.blocks[o.definition] or nil
				if b ~= nil and not find(name, "^zone_") and not isSkipped(o.definition) then
					local sprite = spriteOf(b)
					local sw, sh = 0, 0
					if sprite ~= nil then sw, sh = _G.res.getSpriteBounds("", sprite) end
					sw, sh = sw or 0, sh or 0
					local static = b.material == "immovable"
					local w, h
					if sw > 0 and sh > 0 then w, h = sw * ps, sh * ps
					elseif b.radius ~= nil then w, h = b.radius * 2, b.radius * 2
					else w, h = 1, 1 end
					local r, g, bl = colourOf(b, o.definition)
					items[#items + 1] = { x = o.x, y = o.y, w = w, h = h, a = o.angle or 0, r = r, g = g, b = bl, static = static, bird = b.controllable == true,
						sprite = (sw > 0 and not static) and sprite or nil, sw = sw, sh = sh }
					local e = max(w, h) / 2
					x1, y1, x2, y2 = min(x1, o.x - e), min(y1, o.y - e), max(x2, o.x + e), max(y2, o.y + e)
				end
			end
		end
		local result = #items > 0 and { items = items, x1 = x1, y1 = y1, x2 = x2, y2 = y2 } or false
		cache["preview " .. file] = result
		return result or nil
	end

	-- one item at the screen point px, py: its sprite (scale per sprite pixel) or a bar, drawn with `alpha` (0 to 1)
	local function drawItem(it, px, py, scale, alpha)
		if it.sprite ~= nil then
			local qx, qy = px / scale, py / scale       -- the render state scales the coordinates it is given; the sprite turns about the point of itself given as the pivot (its middle)
			setRenderState(0, 0, scale, scale, it.a, it.sw / 2, it.sh / 2, alpha)
			_G.res.drawSprite(it.sprite, qx, qy, "VCENTER", "HCENTER")
			setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
		else
			local ps = physicsScale or 0.05
			local k = scale / ps
			local len, thick, ang = it.w, it.h, it.a
			if it.h > it.w then len, thick, ang = it.h, it.w, it.a + pi / 2 end
			local dx, dy = cos(ang) * len * k / 2, sin(ang) * len * k / 2
			drawLine2D(px - dx, py - dy, px + dx, py + dy, max(1, thick * k), it.r, it.g, it.b, 255 * alpha)
		end
	end

	local function drawPreview(ax1, ay1, ax2, ay2)
		previewRect = { x1 = ax1, y1 = ay1, x2 = ax2, y2 = ay2 }
		drawRect(0.1, 0.1, 0.13, 1, ax1, ay1, ax2, ay2, false)
		local pv = previewOf(pathOf())
		if pv == nil then return end
		local pw, ph = ax2 - ax1 - 12, ay2 - ay1 - 12
		local sc = min(pw / max(pv.x2 - pv.x1, 0.01), ph / max(pv.y2 - pv.y1, 0.01))
		local ox = ax1 + 6 + (pw - (pv.x2 - pv.x1) * sc) / 2
		local oy = ay1 + 6 + (ph - (pv.y2 - pv.y1) * sc) / 2
		-- the ground (world y 0) cuts off what is below it, as in the game (the static blocks reach down into it)
		local gy = oy + (0 - pv.y1) * sc
		local clipped = gy < ay2 and gy > ay1
		if clipped then _G.res.setClipRect(ax1, ay1, ax2 - ax1, gy - ay1) end
		for pass = 1, 2 do
			for _, it in ipairs(pv.items) do
				if (pass == 1) == it.static then
					drawItem(it, ox + (it.x - pv.x1) * sc, oy + (it.y - pv.y1) * sc, sc * (physicsScale or 0.05), 1)
				end
			end
		end
		if clipped then
			_G.res.setClipRect(0, 0, screenWidth, screenHeight)
			drawRect(0.2, 0.26, 0.12, 1, ax1, gy, ax2, ay2, false)
		end
	end

	-- placing: the level as a half-transparent copy that follows the cursor (it has no body, nothing collides with it); Return or a click puts it down
	local function cursorPhysics()
		if cursor ~= nil and type(screenToPhysicsTransform) == "function" then return screenToPhysicsTransform(cursor.x, cursor.y) end
		return 0, 0
	end

	local function drawGhost()
		if placing == nil then return end
		local pv = previewOf(placing)
		if pv == nil then return end
		local cx, cy, n = 0, 0, 0
		for _, it in ipairs(pv.items) do
			if not it.bird then cx, cy, n = cx + it.x, cy + it.y, n + 1 end
		end
		if n == 0 then return end
		cx, cy = cx / n, cy / n
		local tx, ty = cursorPhysics()
		local camX, camY, ws = getCamera()
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
		_G.res.setClipRect(0, 0, screenWidth, (0 * 20 - camY) * ws)       -- not below the ground
		for pass = 1, 2 do
			for _, it in ipairs(pv.items) do
				if not it.bird and (pass == 1) == it.static then
					drawItem(it, ((it.x - cx + tx) * 20 - camX) * ws, ((it.y - cy + ty) * 20 - camY) * ws, ws, 0.5)
				end
			end
		end
		_G.res.setClipRect(0, 0, screenWidth, screenHeight)
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
	end

	local function button(x1, y1, x2, y2, label, action)
		buttons[#buttons + 1] = { x1 = x1, y1 = y1, x2 = x2, y2 = y2, action = action }
		popupButton(x1, y1, x2, y2, 0.95, 0.62, 0.12)
		_G.res.drawString("", label, (x1 + x2) / 2, (y1 + y2) / 2, "VCENTER", "HCENTER")
	end

	local function drawPanel()
		if not open then return end
		if placing ~= nil then drawGhost() return end
		local w, h = screenWidth, screenHeight
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
		drawRect(0, 0, 0, 0.45, 0, 0, w, h, false)
		local pw, ph = w * 0.62, h * (levelSpawner.preview and 0.74 or 0.3)
		local x1, y1 = (w - pw) / 2, (h - ph) / 2
		panel = { x1 = x1, y1 = y1, x2 = x1 + pw, y2 = y1 + ph }
		buttons = {}
		popupBox(x1, y1, x1 + pw, y1 + ph)
		if type(fontBasic) ~= "nil" then _G.res.useFont(fontBasic) end
		_G.res.drawString("", levelSpawner.title, w / 2, y1 + ph * 0.06, "VCENTER", "HCENTER")
		local bw, bh = 44, 44
		local rowY
		if levelSpawner.preview then
			drawPreview(x1 + bw + 24, y1 + ph * 0.11, x1 + pw - bw - 24, y1 + ph * 0.66)
			rowY = y1 + ph * 0.73
		else rowY = y1 + ph * 0.45 end
		local pn = packNumber(folders[folderIndex])
		_G.res.drawString("", (pn ~= nil and (pn .. "-" .. (gameIndex(folders[folderIndex], files[fileIndex]) or fileIndex) .. "   ") or "") .. pathOf() .. "   (" .. fileIndex .. " / " .. #files .. ")", w / 2, rowY, "VCENTER", "HCENTER")
		local midY = y1 + ph * (levelSpawner.preview and 0.385 or 0.45)
		button(x1 + 10, midY - bh / 2, x1 + 10 + bw, midY + bh / 2, "<", function() stepFile(-1) end)
		button(x1 + pw - 10 - bw, midY - bh / 2, x1 + pw - 10, midY + bh / 2, ">", function() stepFile(1) end)
		local fy = rowY + 52
		button(w / 2 - 150, fy - 16, w / 2 - 20, fy + 16, "folder -", function() stepFolder(-1) end)
		button(w / 2 + 20, fy - 16, w / 2 + 150, fy + 16, "folder +", function() stepFolder(1) end)
		_G.res.drawString("", "Left / Right: level    Up / Down: folder", w / 2, y1 + ph * 0.905, "VCENTER", "HCENTER")
		_G.res.drawString("", levelSpawner.placement and "Return: spawn here    Click the picture: place it    Esc: close" or "Return: spawn here    Esc: close", w / 2, y1 + ph * 0.968, "VCENTER", "HCENTER")
	end

	local originalUpdate, originalDrawGame = update, drawGame
	update = function(dt, realDt)
		originalUpdate(dt, realDt)
		handle()
	end
	if type(originalDrawGame) == "function" then drawGame = function(...) originalDrawGame(...) drawPanel() end end
end

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
		if socialLevels.sendScores then httpGet(socialLevels.server .. "/classic/play?key=" .. entry.key .. "&player=" .. urlEncode(socialLevels.player), 1) end
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
		httpGet(socialLevels.server .. "/classic/score?key=" .. current.key .. "&player=" .. urlEncode(socialLevels.player) .. "&score=" .. _G.string.format("%d", entry.score), 1)
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

------------------------------------------------------------------ 10. (3.9) episodes.lua: the episodes of the menu before 2.0.0, kept in one file
-- Before 2.0.0 the six episodes are written out in gamelogic.lua. If an episodes.lua sits next to gamelogic.lua, it sets here what the menu builds from:
-- which levels each world holds (and in which folder), which art each episode tile shows and in which order the tiles stand. See episodes.lua in extras/.
episodes = nil
do
	-- next to gamelogic.lua, or (an .apk / .ipa keeps its scripts in a cache folder) next to this file
	local file = _G.loadfile(scriptPath .. "/episodes.lua") or (extraScriptFolder ~= nil and _G.loadfile(extraScriptFolder .. "/episodes.lua")) or nil
	if file ~= nil then _G.setfenv(file, _G.getfenv(1)) end
	if file ~= nil and _G.pcall(file) and _G.type(episodes) == "table" then
		-- the six pages of the old menu: the first world of each (pack number), how many worlds it has, and the variable of its page
		local slots = {
			{ first = 1, page = "levelSelectionPagesBasic" }, { first = 4, page = "levelSelectionPagesExtra" }, { first = 6, page = "levelSelectionPagesPack3" },
			{ first = 9, page = "levelSelectionPagesPack4" }, { first = 12, page = "levelSelectionPagesPack5" }, { first = 15, page = "levelSelectionPagesPack6" },
		}
		local env = _G.getfenv(1)
		local originalCreateMenuPages = createMenuPages
		createMenuPages = function(...)
			-- style = "popup": the episodes are drawn as pop-up boxes with their names as text (the way the mobile versions draw them), not as picture tiles
			if episodes.style == "popup" and g_newEpisodeScreen ~= nil then g_newEpisodeScreen.enabled, g_newEpisodeScreen.lastSetEnabled = false, false end
			-- the lists are changed in place, the score totals of the menu hold the same tables
			for s = 1, 6 do
				local worlds = episodes[s] and episodes[s].worlds or {}
				for w = 1, #worlds do
					local pack = slots[s].first + w - 1
					local list = levelOrder[levelOrder.packs[pack]]
					if list ~= nil then
						for i = #list, 1, -1 do list[i] = nil end
						for i = 1, #worlds[w].levels do list[i] = worlds[w].levels[i] end
						if worlds[w].folder ~= nil then levelOrder.packs[pack] = worlds[w].folder levelOrder[worlds[w].folder] = list end
					end
				end
			end
			originalCreateMenuPages(...)
			for s = 1, 6 do
				local page = env[slots[s].page]
				if page ~= nil and episodes[s] ~= nil and episodes[s].worlds ~= nil then
					for i = page.firstLevelIndex, #page.items do
						local item = page.items[i]
						if item.pageIndex ~= nil then
							local pack = slots[s].first + item.pageIndex - 1
							local name = levelOrder[levelOrder.packs[pack]][item.pageLevelIndex]
							item.filename = name
							item.folder = levelPath .. "/" .. levelOrder.packs[pack] .. "/"
							if item.callParam1 ~= nil then item.callParam1.filename, item.callParam1.folder = item.filename, item.folder end
						end
					end
				end
				if episodes[s] ~= nil and episodes[s].tile ~= nil then getItemByName(episodeSelectionPage.items, "episodeButton" .. s).sprite = "BUTTON_EP_" .. episodes[s].tile end
			end
		end

		-- style = "popup": the boxes grow a little under the cursor (the retail hover code only handles single sprites, the boxes are not)
		if episodes.style == "popup" and env.g_newEpisodeScreen ~= nil then
			local originalUpdate = update
			update = function(...)
				originalUpdate(...)
				if env.episodeSelectionPage ~= nil and env.currentMenuPage == env.episodeSelectionPage then
					for i = 1, 4 do
						local box = env.getItemByName(env.episodeSelectionPage.items, "episode" .. i)
						if box ~= nil and box.width ~= nil then
							if box.popW == nil or (box.width ~= box.popShownW) then box.popW, box.popH, box.popK = box.width, box.height, 1 end
							local on = env.cursor ~= nil and _G.math.abs(env.cursor.x - box.x) < box.popW / 2 and _G.math.abs(env.cursor.y - box.y) < box.popH / 2
							box.popK = box.popK + ((on and 1.08 or 1) - box.popK) * 0.3
							box.width, box.height = box.popW * box.popK, box.popH * box.popK
							box.popShownW = box.width
						end
					end
				end
			end
		end

		local originalPrepareMenuPage = prepareMenuPage
		prepareMenuPage = function(page, ...)
			-- the pop-up boxes are drawn when the hover buttons are off, so they are off while this page is built (the other menus keep theirs)
			local hover = env.g_enableMouseOverStates
			if page == episodeSelectionPage and episodes.style == "popup" and env.g_newEpisodeScreen ~= nil then env.g_enableMouseOverStates = false end
			originalPrepareMenuPage(page, ...)
			env.g_enableMouseOverStates = hover
			if page == episodeSelectionPage and episodes.style == "popup" and env.g_newEpisodeScreen ~= nil then
				-- episodes[n].name is the title of box n (it replaces the built-in "1. Poached Eggs"; write the number in it if you want one)
				for n = 1, 4 do
					local title = episodes[n] and episodes[n].name and getItemByName(page.items, "episode" .. n .. "Text")
					if title then title.text = episodes[n].name end
				end
			end
			for s = 1, 6 do
				if page == env[slots[s].page] then
					-- the places of a world that has fewer levels than its page are left empty
					for i = page.firstLevelIndex, #page.items do
						if page.items[i].pageIndex ~= nil and page.items[i].filename == nil then
							page.items[i].sprite, page.items[i].text, page.items[i].selectable = "", nil, false
						end
					end
				end
			end
			if page == episodeSelectionPage and episodes.style ~= "popup" and _G.type(episodes.order) == "table" then
				-- tile k of the screen shows episode order[k]: every part of an episode's tile moves by the way between the two places
				local names = { "episodeButton", "episodeButtonScore", "episodeButtonStars", "episodeButtonLock" }
				local home = {}
				for k = 1, 6 do home[k] = getItemByName(page.items, "episodeButton" .. k) end
				local x, y = {}, {}
				for k = 1, 6 do x[k], y[k] = home[k].x, home[k].y end
				for k = 1, 6 do
					local e = episodes.order[k]
					if e ~= nil and e ~= k then
						for n = 1, #names do
							local item = getItemByName(page.items, names[n] .. e)
							item.x, item.y = item.x + x[k] - x[e], item.y + y[k] - y[e]
						end
					end
				end
			end
		end
	end
end
