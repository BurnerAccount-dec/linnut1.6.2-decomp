-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 3. laser mod (3.3)
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

------------------------------------------------------------------ 3. laser mod (3.3)
-- Three effects that work together. Each one has its switch below, set it to false to turn it off; all numbers can be changed from the console (lua laserMod.damage = 50).
--   laserBird:   tap (or press SPACE) while a Big Brother Bird flies. The game slows down and a laser shoots from the bird to the cursor (at most laserMod.maxRange units) and pierces what it crosses:
--                it pushes the blocks and pigs on its line and takes strength from them (the game destroys what reaches 0 itself, with score and particles).
--                Rock and everything that cannot move (the ground, the immovable planks) stop it. Birds also turn to face their flight, from the launch until it first touches something.
--                Blocks of the material "light" (ice, glass) break at once; everything else gets a push of laserMod.push (a speed, so heavy blocks are pushed as hard as light ones) and loses strength.
--   waveShader:  a GLSL shader of your own ("heatwave", made here with createShader) makes the whole background, the sky included, shimmer: sideways and up and down, and the strength breathes.
--   gravityZone: a sensor ball in front of the slingshot. Whatever flies in gets its own upwards gravity and a violet tint until it leaves again. Right click anywhere in a level to move the zone there.
laserMod = {
	laserBird = true,
	waveShader = false,   -- (4.1.2.1) off: the shimmer is a full-screen pass over the background and took about half of the frame rate; set it to true for the shimmer
	gravityZone = true,

	birdNames = { "RedBigBird", "BIRD_BIG_BROTHER" },   -- a bird whose name or definition contains one of these can fire the laser (4.1.2: the Big Brother Bird; the Red Bird has Stella's ricochet, section 10)
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
		if stellaMod ~= nil and stellaMod.enabled and stellaMod.birds[obj.definition or ""] and stellaMod.active ~= nil and stellaMod.active(name) then return true end
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
