-- Extra scripts for Angry Birds Classic 1.6.2 / 1.6.3.1, loaded by the engine when data/scripts/extra_scripts.lua exists next to gamelogic.lua
-- (or by hand: loadLuaFile(scriptPath .. "/extra_scripts.lua", "", false, false) at the end of gamelogic.lua).
--   1. the Star Wars screen wipe between a menu and a level
--   2. the Star Wars aiming aid: while the bird is pulled back, the whole flight path is drawn as dots
--   4. (3.3) restart: the restart button first moves every object back to where the level began, then restarts without the screen wipe
--   3. (3.3) the laser mod: a piercing laser and slow motion for the Red Bird, a heat-wave GLSL shader on the background, an anti-gravity sensor zone
-- Loading it a second time does nothing.

if extraScripts ~= nil or _G.type(loadLevel) ~= "function" or _G.type(update) ~= "function" then
	return
end
extraScripts = true
print("extra_scripts.lua loaded (screen wipe, aiming aid, laser mod, restart, shape blocks, version 17" .. (_G.type(simulateAimingAid) == "function" and ")" or ": this engine build is too old for the aiming aid, rebuild it)"))

------------------------------------------------------------------ 1. screen wipe
screenwipe = {
	time = 1,            -- seconds the wipe takes
	direction = "right", -- "right": the old picture is wiped away from its right edge towards the left, "left": from its left edge towards the right
	sprite = "CAPTURED_SCREENSHOT",
	timer = nil,         -- nil: no wipe running
	waiting = false,     -- the level is loading: the wipe starts when the loading screen is gone
	active = false,      -- the wrappers below are installed
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
		if screenwipe.skip then return originalSetGameMode(mode, ...) end
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
	rotateBirds = true,   -- every bird points in its direction of flight (a bird whose definition has rotateWhileFlying = true is turned by the game itself, as Blu is in Rio, and is left alone)
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
			for name, obj in pairs(objects.world) do
				if landed[name] == nil and isBird(name, obj) then
					local speed, vx, vy = getVelocity(name)
					if speed > laserMod.minSpeed then setRotation(name, _G.math.atan2(vy, vx)) end
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
			local isRestart = levelId() == snapLevel and ((item ~= nil and item.restartLevel) or (item == nil and currentGameMode == updateGame))
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
		local template = blockTable.blocks[shapeBlocks.template]
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
	register()
	engine.on("levelLoaded", function() register() cut() end)
	engine.on("update", function() if shapeBlocks.enabled then register() cut() end end)
end

