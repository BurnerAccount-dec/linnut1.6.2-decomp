-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 10. Stella's abilities (4.1.2)
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

------------------------------------------------------------------ 10. Stella's abilities (4.1.2)
-- The five bird abilities of Angry Birds Stella, ported from its own scripts (BlockComponents/StellaAbility, LucaAbility, PoppyAbility, WillowAbility, DahliaAbility, BlockHighlighter, bird_utils and their parameters in
-- definitions/blocks_birds.lua, blocks_misc.lua and damagefactors.lua) to the Classic birds, with the same states, timers and numbers:
--   Red Bird (Stella's ricochet): the game slows down BEFORE a collision it can see coming (predictCollision), and after the first touch for 0.3 s (the last chance). Press and hold: the game nearly stops, the cursor aims and a path of
--                up to 2 ray-cast jumps, reflected at every wall, is drawn (the blocks on it light up red); let go and the bird jumps along it with its collisions off, kicks what it lands on, waits (grabDuration), jumps again,
--                and after the last jump flies on with jumpEndSpeed and Stella's damage factors for 0.5 s.
--   Blue Bird (Luca's scream): press and hold in slow motion (the background darkens, glass lights up), let go: the world stops while the scream's wave runs along the ray, then glass breaks, wood loses 35% of its strength and what the
--                ray crossed or stopped at is pushed. The ability stays for 1 s after the first touch.
--   Yellow Bird (Poppy's drop): press and hold with a line straight down, let go: a pause of bulletTimeDuration, then the drill: a force down every frame, its sensor takes damage x material factor x time from what it overlaps; the first
--                collision starts the drillDuration timer and weakens the force.
--   Black Bird (Willow's dash): a press starts the ability at once (no gravity, spinning, a damage sensor); holding for holdThresholdDuration slows the game and turns the bird, letting go boosts it; a touch starts it by itself.
--   White Bird (Dahlia's teleport): press: a ghost slides along the bird's flight direction (maxDistance, it stops at the ground) in slow motion, let go or wait: the bird is gone for the dash (shorter the earlier you let go),
--                arrives, and spins a kick that pushes what its sweep crosses, then hops on.
-- The Big Brother Bird (RedBigBird) keeps the shape-cast laser of section 3. Every number is in the table below and can be changed from the console (lua stellaMod.stella.maxDistance = 20).
-- Stella's world is smaller than Classic's (its birds have a radius of about 0.13, a Classic Red Bird 0.85), so every length, speed and force is multiplied by stellaMod.unit.
-- What a Classic game cannot give: Stella's sprites, animations, sounds and particles (lines and rings are drawn instead), its camera, the physics of its objects (a kick takes strength from a block; the hit of a flying bird
-- is the engine's own, with Stella's damage factors) and the lagged aiming cursor.
stellaMod = {
	enabled = true,
	unit = 6,
	minSpeed = 0.5,       -- the bird must fly at least this fast to start an ability
	birds = { RedBird = "stella", SmallBlueBird = "luca", YellowBird = "poppy", BlackBird = "willow", BasicBird2 = "dahlia" },   -- definition = ability; a definition left out (or false) keeps its own ability
	ignore = { "^zone_" },
	materials = { wood = "wood", rock = "stone", light = "glass" },   -- Classic material = the Stella material its tables use
	speedScale = 60,      -- a push of 1 (Stella's force units) changes a block's speed by this much (physics units per second); the impulse is this times the block's mass
	accelScale = 25,      -- the same for a force applied every frame: acceleration per unit of force
	stella = {
		useAiming = true, aimingSlowmo = 0.001, jumpingSlowmo = 0.15, maxDistance = 10, jumpSpeed = 7.5, lastJumpSpeed = 10, jumpEndSpeed = 3, grabDuration = 0.35,
		aimDurationStable = 5, aimDurationSpeedUp = 3, maxJumps = 2, kickImpulseForce = { default = 1, rock = 1.4, rubber = 3, gold = 5 }, kickDamage = 20, kickSpeedScale = 20,
		circleAngleStep = 45, lastChanceDelayWhenColliding = 0.3, slowDownTimeBeforeCollision = true, damageFactors = "StellaAbilityDamageFactors",
		delayAfterCollisionBeforeDamageFactorReset = 0.5, forceActivationAfterCollisionWhileAimingDuration = 1, bgDarkeningAmount = 0.4, stoppingMaterials = {},
		stoppingMaterialCollisionDamage = 150, explosionRadius = 0.35, explosionForce = 15, explosionScale = 2, aimAppearSpeed = 0.6, tapMaxTime = 0.1,
		color = { 2, 1, 1 }, lightness = 0, speed = 10,
	},
	luca = {
		aimDurationStable = 2, aimDurationSpeedUp = 3, aimSlowmo = 0.05, bgDarkeningAmount = 0.3, bgDarkeningTweenInDuration = 0.2, bgDarkeningTweenOutDuration = 0.2,
		beatAfterScreamDuration = 0.2, screamWaveSpeed = 1, screamPassThroughPushForce = 0.05, screamStopPushForce = 0.2, forceActivationAfterCollisionWhileAimingDuration = 1,
		screamRayLength = 10, screamRayDamage = { glass = 1, wood = 0.35, stone = 0 }, breakableMaterials = { glass = true, gem = true },
		rayStoppingMaterials = { staticGround = true, stone = true, platform = true, obsidian = true, metal = true }, disableAfterCollision = 1, waveTimeShare = 0.6, waveWorldSpeed = 1e-5,
		color = { 0.6, 1, 2 }, lightness = 0.2, speed = 1.5,
	},
	poppy = {
		downDrillForce = 7, downDrillForceMultplierAfterCollision = 0.1, bulletTimeDuration = 0.05, drillDuration = 1.5, impactForce = 0, impactForceRadius = 0,
		applyImpactForceOnAbilityStop = true, aimSlowmo = 0.1, damageFactors = "PoppyAbilityDamageFactors", stopOnFirstCollision = true, aimLineRaycast = true, aimLineLength = 10,
		aimDurationStable = 1, aimDurationSpeedUp = 4, damage = 200, forceActivationAfterCollisionWhileAimingDuration = 0.5, disableAfterCollision = 1,
		sensorWidth = 0.4, sensorHeight = 0.2, sensorOffsetY = 0.2,
	},
	willow = {
		duration = 4, durationAfterCollision = 4, durationAfterCollisionWhenAbilityActivated = 2, holdThresholdDuration = 0.15, damage = 200, damageFactors = "WillowAbilityDamageFactors",
		spinSpeed = 220, minSpinSpeed = 10, delayBeforeAim = 0.1, useNewAbility = true, turnSpeed = -1.2, speedWhileTurning = 1,   -- times the speed the bird had when the ability started (Stella's own speeds are far lower than Classic's, so a fixed speed slowed the bird to a crawl)
		 boostForce = 0.4, boostForceDuration = 2,
		aimDurationStable = 0.2, aimDurationSpeedUp = 3, aimSlowmo = 0.3, maxTurn = 360, densityWhenRolling = 1, sensorRadius = 0.35, aimLineLength = 10,
	},
	dahlia = {
		maxDistance = 6, delayBeforeAim = 0.01, aimDuration = 0.3, aimSlowmo = 0.125, dashDuration = 0.4, kickDuration = 0.5, kickSlowmo = 1, explosionForce = 0.5, explosionRadius = 0.5,
		uniformExplosionForce = true, passThroughForce = 0.01, horizontalMovement = false, kickRotationPhysicsOffset = -0.5, bgDarkeningDuration = 1, bgDarkeningAmount = 0.4,
		explosionScale = 400, passThroughScale = 400, hopX = 0.5, hopY = -1, ghostWidth = 0.6, ghostHeight = 0.4, aimHighlightIntensity = 0.5,
		color = { 2, 1.2, 1 }, lightness = 0, speed = 3,
	},
	-- Stella's damage factors (definitions/damagefactors.lua), in Classic's materials: the bird carries one of these while an ability runs
	damageFactors = {
		StellaAbilityDamageFactors = { damageMultiplier = { wood = 2, rock = 2, light = 3, staticGround = 0, immovable = 0 } },
		PoppyAbilityDamageFactors = { damageMultiplier = { wood = 0.7, rock = 1, light = 0.6, staticGround = 0, immovable = 0 } },
		WillowAbilityDamageFactors = { damageMultiplier = { wood = 1, rock = 0.5, light = 1, staticGround = 0, immovable = 0 } },
	},
}

do
	local sqrt, cos, sin, atan2, pi, min, max, floor, abs, ceil = _G.math.sqrt, _G.math.cos, _G.math.sin, _G.math.atan2, _G.math.pi, _G.math.min, _G.math.max, _G.math.floor, _G.math.abs, _G.math.ceil
	local find, pairs, ipairs, type, insert, remove = _G.string.find, _G.pairs, _G.ipairs, _G.type, _G.table.insert, _G.table.remove
	local next = _G.next
	local S = nil              -- the ability that runs: { kind, bird, phase, ... }
	local infos = {}           -- per bird: { entry, collided, used, disabled, lastChance, ... }
	local timers = {}          -- callDelayed: { left, fn, real }
	local wasDown = false
	local predictSlowed = false
	local lucaFade = nil       -- Luca's darkening while it fades out after the scream
	local highlights = {}      -- active BlockHighlighters

	------------------------------------------------ the game's time: Stella's setDeltaTimeMultiplier is the engine's gameSpeedFactor
	local function getMult() return physicsSpeedFactor or 1 end
	local function setMult(m) physicsSpeedFactor = m ~= 1 and m or nil end      -- (4.2) the physics' speed only: animations, the menus and the scripts' own clocks keep their pace
	local function later(delay, fn, real) timers[#timers + 1] = { left = delay, fn = fn, real = real } end
	local function clamp(v, a, b) return v < a and a or (v > b and b or v) end
	local function ease(name)     -- motion.ease when there is one, else the plain curve
		return function(t) return t end
	end

	local function toScreen(x, y)
		if type(physicsToScreenTransform) == "function" then return physicsToScreenTransform(x, y) end
		local camX, camY, ws = getCamera()
		return (x * 20 - camX) * ws, (y * 20 - camY) * ws
	end
	local function cursorPhysics()
		if stellaMod.aimPoint ~= nil then return stellaMod.aimPoint.x, stellaMod.aimPoint.y end
		if type(screenToPhysicsTransform) == "function" then return screenToPhysicsTransform(cursor.x, cursor.y) end
		local camX, camY, ws = getCamera()
		return (cursor.x / ws + camX) / 20, (cursor.y / ws + camY) / 20
	end
	local function ignored(name)
		for _, p in ipairs(stellaMod.ignore) do if find(name, p) then return true end end
		return false
	end
	local function isBirdObj(o)
		local blocks = blockTable ~= nil and blockTable.blocks or nil
		local def = blocks ~= nil and blocks[o.definition or ""] or nil
		return def ~= nil and def.group == "birds"
	end
	local function matOf(o) return stellaMod.materials[o.material or ""] or o.material end
	local function radiusOf(name)
		local o = objects.world[name]
		return (o ~= nil and o.radius) or 0.85
	end
	-- what a ray crosses, nearest first, without the bird itself, the other birds and the sensors
	local function castHits(x1, y1, x2, y2, skip)
		local out = {}
		local hits = rayCast(x1, y1, x2, y2, true)
		if hits == nil then return out end
		for _, h in ipairs(hits) do
			local o = objects.world[h.name]
			if h.name ~= skip and o ~= nil and not ignored(h.name) and not isBirdObj(o) then out[#out + 1] = h end
		end
		return out
	end
	-- Stella's updateAbilityAimProgress (bird_utils.lua): the time multiplier while aiming, and whether the aim has run out
	local function aimProgress(timer, slowmo, stable, speedUp)
		if speedUp == 0 then
			setMult(timer < stable and slowmo or 1)
			return stable <= timer
		end
		local p = clamp((timer - stable) / speedUp, 0, 1)
		setMult(slowmo + (1 - cos(p * pi * 0.5)))     -- tweenLinear(1 - cos, slowmo, 1, 1)
		return p >= 1
	end
	local function doDamage(o, amount)
		if o ~= nil and o.strength ~= nil and amount ~= 0 and not o.controllable then o.strength = o.strength - amount end
	end
	local function pushSpeed(name, dvx, dvy, x, y)
		local o = objects.world[name]
		if o == nil or (o.mass or 0) <= 0 then return end
		applyImpulse(name, dvx * o.mass, dvy * o.mass, x or o.x, y or o.y)
	end
	local function factorOf(tableName, o)
		local f = stellaMod.damageFactors[tableName]
		local v = f ~= nil and f.damageMultiplier[o.material or ""] or nil
		return v
	end
	local function registerFactors()
		local t = blockTable ~= nil and blockTable.damageFactors or nil
		if t == nil then return end
		for name, f in pairs(stellaMod.damageFactors) do
			if t[name] == nil then t[name] = { damageMultiplier = f.damageMultiplier, velocityMultiplier = f.damageMultiplier } end
		end
	end
	local function ring(x, y, r, w, cr, cg, cb, ca)
		local px, py
		for k = 0, 24 do
			local a = 2 * pi * (k % 24) / 24
			local sx, sy = toScreen(x + r * cos(a), y + r * sin(a))
			if px ~= nil then drawLine2D(px, py, sx, sy, w, cr, cg, cb, ca) end
			px, py = sx, sy
		end
	end
	local function darken(amount)
		if amount > 0 then drawRect(0, 0, 0.05, min(1, amount), 0, 0, screenWidth, screenHeight, false) end
	end

	------------------------------------------------ BlockHighlighter: a block fades towards the highlight colour and back
	local function newHighlighter(color, lightness, speed, overrideIntensity)
		local h = { color = color, lightness = lightness or 0, speed = speed or 1, blocks = {}, enabled = true, intensity = overrideIntensity }
		highlights[#highlights + 1] = h
		return h
	end
	local function setBlocks(h, set)
		if h == nil then return end
		for name, b in pairs(h.blocks) do b.on = false end
		for name, intensity in pairs(set) do
			local b = h.blocks[name]
			if b == nil then b = { progress = 0 } h.blocks[name] = b end
			b.on, b.intensity = true, intensity
		end
	end
	local function disposeHighlighter(h)
		if h == nil then return end
		h.enabled = false
		setBlocks(h, {})
	end
	local function updateHighlighters(realDt)
		local i = 1
		while i <= #highlights do
			local h = highlights[i]
			local c = h.color
			for name, b in pairs(h.blocks) do
				b.progress = clamp(b.progress + (b.on and 1 or -1) * h.speed * realDt, 0, 1)
				if objects.world ~= nil and objects.world[name] ~= nil then
					local p = b.progress * (b.intensity or 1)
					local l = h.lightness * p
					setTint(clamp(1 + (c[1] - 1) * p + l, 0, 3), clamp(1 + (c[2] - 1) * p + l, 0, 3), clamp(1 + (c[3] - 1) * p + l, 0, 3), name)
				end
				if b.progress <= 0 and not b.on then
					if objects.world ~= nil and objects.world[name] ~= nil then resetTint(name) end
					h.blocks[name] = nil
				end
			end
			if not h.enabled and next(h.blocks) == nil then remove(highlights, i) else i = i + 1 end
		end
	end

	------------------------------------------------ per bird: what happened to it
	local function infoOf(name, o)
		local i = infos[name]
		if i == nil or i.entry ~= o then i = { entry = o, name = name } infos[name] = i end
		return i
	end
	local function restoreBird(name, i)
		local o = objects.world ~= nil and objects.world[name] or nil
		if o == nil then return end
		setCollisionEnabled(name, true)
		resetObjectGravity(name)
		if i ~= nil and i.oldFactors ~= nil then o.damageFactors = i.oldFactors end
	end
	local function setFactors(name, tableName)
		local o = objects.world[name]
		local i = infoOf(name, o)
		if i.oldFactors == nil then i.oldFactors = o.damageFactors or "DefaultDamageFactors" end
		o.damageFactors = tableName
	end
	local function resetFactors(name)
		local o = objects.world ~= nil and objects.world[name] or nil
		local i = infos[name]
		if o ~= nil and i ~= nil and i.oldFactors ~= nil then o.damageFactors = i.oldFactors i.oldFactors = nil end
	end

	local function finish()
		if S ~= nil then
			local name, i = S.bird, infos[S.bird]
			disposeHighlighter(S.hl)
			if S.kind ~= "willow" then restoreBird(name, i) end
			if i ~= nil then i.used = true end
		end
		S = nil
		setMult(1)
		lucaFade = lucaFade
	end

	------------------------------------------------ Stella: ricochet
	local function stellaPlot(bx, by, dirx, diry, radius, bird, firstHit)
		local c = stellaMod.stella
		local u = stellaMod.unit
		local path = { { x = bx, y = by } }
		local x, y, left, jumps = bx, by, c.maxDistance * u, c.maxJumps
		local step = c.circleAngleStep * pi / 180
		local dx, dy = dirx, diry
		while left > 0.01 and jumps > 0 do
			local ex, ey = x + dx * left, y + dy * left
			local h = castHits(x, y, ex, ey, bird)[1]
			if h == nil then path[#path + 1] = { x = ex, y = ey } break end
			local nx, ny = h.nx, h.ny
			local o = objects.world[h.name]
			if o ~= nil and o.type == "circle" and step > 0 then
				local a = floor((atan2(ny, nx) + 0.5 * step) / step) * step
				nx, ny = cos(a), sin(a)
			end
			local tx, ty = h.x + nx * radius, h.y + ny * radius
			local dist = sqrt((tx - x) * (tx - x) + (ty - y) * (ty - y))
			path[#path + 1] = { x = tx, y = ty, nx = nx, ny = ny, name = h.name, cx = h.x, cy = h.y }
			left = max(0, left - dist)
			jumps = jumps - 1
			local dot = dx * nx + dy * ny
			dx, dy = dx - 2 * dot * nx, dy - 2 * dot * ny
			x, y = tx, ty
		end
		return path
	end
	local function aimDirection(o)
		local tx, ty = cursorPhysics()
		local dx, dy = tx - o.x, ty - o.y
		local len = sqrt(dx * dx + dy * dy)
		if len < 0.001 then
			local _, vx, vy = getVelocity(S.bird)
			dx, dy = vx, vy
			len = sqrt(dx * dx + dy * dy)
			if len < 0.001 then return 1, 0 end
		end
		return dx / len, dy / len
	end
	local stellaStartJump
	local function stellaFinish(o, dx, dy, dist)
		local c = stellaMod.stella
		local u = stellaMod.unit
		local name = S.bird
		local last = S.path[#S.path]
		local len = dist > 0.001 and dist or 1
		restoreBird(name, nil)
		setVelocity(name, dx / len * c.jumpEndSpeed * u, dy / len * c.jumpEndSpeed * u)
		setFactors(name, c.damageFactors)
		later(c.delayAfterCollisionBeforeDamageFactorReset, function() resetFactors(name) end)
		for n, t in pairs(objects.world) do        -- makeExplosion: explosionRadius, explosionForce, no damage
			if n ~= name and (t.mass or 0) > 0 and not ignored(n) and not isBirdObj(t) then
				local ox, oy = t.x - last.x, t.y - last.y
				local d = sqrt(ox * ox + oy * oy)
				if d < c.explosionRadius * u and d > 0.001 then pushSpeed(n, ox / d * c.explosionForce * c.explosionScale * 0.1, oy / d * c.explosionForce * c.explosionScale * 0.1, t.x, t.y) end
			end
		end
		infos[name].used = true
		disposeHighlighter(S.hl)
		S = nil
		setMult(1)
	end
	stellaStartJump = function(o)
		local c = stellaMod.stella
		local u = stellaMod.unit
		local p, q = S.path[S.i], S.path[S.i + 1]
		S.isLast = (S.i + 1 == #S.path)
		S.u, S.phase = 0, "jump"
		S.dist = sqrt((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y))
		S.speed = (S.isLast and (c.lastJumpSpeed or c.jumpSpeed) or c.jumpSpeed) * u / max(S.dist, 0.001)
	end
	local function stellaActivate(o)
		local c = stellaMod.stella
		local name = S.bird
		S.hasBeenActivated, S.phase, S.i = true, "jump", 1
		S.trail = {}
		for k, p in ipairs(S.path) do S.trail[k] = { x = p.x, y = p.y } end
		setCollisionEnabled(name, false)
		setObjectGravity(name, 0, 0)
		setVelocity(name, 0, 0)
		setMult(c.jumpingSlowmo)
		disposeHighlighter(S.hl)
		S.hl = nil
		stellaStartJump(o)
	end
	local function stellaStart(name, o)
		local c = stellaMod.stella
		local i = infoOf(name, o)
		S = { kind = "stella", bird = name, phase = "aim", aimTimer = 0, aimAppear = 0, darkening = 0, radius = radiusOf(name) }
		S.hl = newHighlighter(c.color, c.lightness, c.speed)
		i.lastChance = false
		setMult(c.aimingSlowmo)
	end
	local function stellaUpdate(o, dt, realDt)
		local c = stellaMod.stella
		local u = stellaMod.unit
		local name = S.bird
		if S.phase == "aim" then
			S.aimTimer = S.aimTimer + realDt
			S.aimAppear = S.aimAppear + c.aimAppearSpeed * realDt
			S.darkening = min(1, S.darkening + realDt)
			local done = aimProgress(S.aimTimer, c.aimingSlowmo, c.aimDurationStable, c.aimDurationSpeedUp)
			local dx, dy = aimDirection(o)
			S.path = stellaPlot(o.x, o.y, dx, dy, S.radius, name)
			local set = {}
			for k = 2, #S.path do
				local q = S.path[k]
				if q.name ~= nil and objects.world[q.name] ~= nil and (objects.world[q.name].mass or 0) > 0 then set[q.name] = 1 end
			end
			setBlocks(S.hl, set)
			setVelocity(name, 0, 0)
			if done or S.release then S.release = nil stellaActivate(o) end
		elseif S.phase == "jump" then
			S.u = min(1, S.u + S.speed * realDt)
			local p, q = S.path[S.i], S.path[S.i + 1]
			setPosition(name, p.x + (q.x - p.x) * S.u, p.y + (q.y - p.y) * S.u)
			setVelocity(name, 0, 0)
			if S.u >= 1 then
				p.disappear = 0
				local t = q.name ~= nil and objects.world[q.name] or nil
				if t ~= nil then        -- the kick: a collision with the target and an impulse into it
					local kick = (c.kickImpulseForce[matOf(t) or ""] or c.kickImpulseForce.default)
					doDamage(t, c.kickDamage * kick * (factorOf(c.damageFactors, t) or 1))
					pushSpeed(q.name, -q.nx * kick * c.kickSpeedScale, -q.ny * kick * c.kickSpeedScale, q.cx, q.cy)
				end
				if S.i + 1 >= #S.path then
					stellaFinish(o, q.x - p.x, q.y - p.y, S.dist)
					return
				end
				S.phase, S.grab = "grab", c.grabDuration
				S.grabAngle = atan2(q.ny or 0, q.nx or 0)
			end
		elseif S.phase == "grab" then
			S.grab = S.grab - realDt
			setVelocity(name, 0, 0)
			if S.grab <= 0 then S.i = S.i + 1 stellaStartJump(o) end
		end
		if S ~= nil and S.trail ~= nil then
			for _, p in ipairs(S.trail) do if p.disappear ~= nil then p.disappear = min(1, p.disappear + realDt * 5) end end
		end
	end
	-- predictCollision: the game slows down as the bird gets close to something (every frame it flies, until it has used or lost its ability)
	local function stellaPredict(name, o)
		local c = stellaMod.stella
		if not c.slowDownTimeBeforeCollision then return false end
		local _, vx, vy = getVelocity(name)
		local len = sqrt(vx * vx + vy * vy)
		if len < 0.001 then return false end
		local r = radiusOf(name)
		local h = castHits(o.x + r * vx / len, o.y + r * vy / len, o.x + vx, o.y + vy, name)[1]
		if h ~= nil then
			local o2 = objects.world[h.name]
			local d = 1 - h.fraction
			setMult(0.05 + 0.95 * min(1, 1.5 * (1 - d * d)))
			return true
		end
		return false
	end

	------------------------------------------------ Luca: scream
	local function lucaCast(o, dx, dy)
		local c = stellaMod.luca
		local len = c.screamRayLength * stellaMod.unit
		local ex, ey = o.x + dx * len, o.y + dy * len
		local stop, stopName, list = 1, nil, {}
		for _, h in ipairs(castHits(o.x, o.y, ex, ey, S.bird)) do
			local t = objects.world[h.name]
			if t ~= nil then
				if (t.mass or 0) <= 0 or c.rayStoppingMaterials[matOf(t)] then stop, stopName = h.fraction, h.name break end
				list[#list + 1] = h
			end
		end
		return { x1 = o.x, y1 = o.y, x2 = o.x + (ex - o.x) * stop, y2 = o.y + (ey - o.y) * stop, dx = dx, dy = dy, maxDistance = len * stop, collisions = list, stopName = stopName, rayLength = len }
	end
	local function lucaStart(name, o)
		local c = stellaMod.luca
		S = { kind = "luca", bird = name, phase = "aim", aimTimer = 0, darkening = 0, tapTime = 0, waves = {} }
		S.hl = newHighlighter(c.color, c.lightness, c.speed)
		lucaFade = nil
		setMult(c.aimSlowmo)
	end
	local function lucaRelease(o)
		local c = stellaMod.luca
		local dx, dy = aimDirection(o)
		disposeHighlighter(S.hl)
		S.hl = nil
		local w = lucaCast(o, dx, dy)
		w.timer, w.speed = 0, c.screamWaveSpeed
		w.duration = (w.maxDistance / (c.screamRayLength * stellaMod.unit)) / c.screamWaveSpeed
		S.waves[#S.waves + 1] = w
		S.phase = "scream"
		infoOf(S.bird, o).used = true
		S.darkening = 1
	end
	local function lucaApply(w, o)
		local c = stellaMod.luca
		local k = c.screamPassThroughPushForce * stellaMod.speedScale
		for _, h in ipairs(w.collisions) do
			local t = objects.world[h.name]
			if t ~= nil then
				local m = matOf(t)
				if c.breakableMaterials[m] then
					if t.strength ~= nil then t.strength = 0 end
				elseif not t.controllable then
					local strength = (blockTable.blocks[t.definition or ""] or {}).strength or t.strength
					doDamage(t, (strength or 0) * (c.screamRayDamage[m] or 0))
					pushSpeed(h.name, w.dx * k * (w.maxDistance / w.rayLength) * 60 / 60, w.dy * k * (w.maxDistance / w.rayLength), w.x2, w.y2)
				end
			end
		end
		if w.stopName ~= nil and objects.world[w.stopName] ~= nil then
			local k2 = c.screamStopPushForce * stellaMod.speedScale
			pushSpeed(w.stopName, w.dx * k2, w.dy * k2, objects.world[w.stopName].x, objects.world[w.stopName].y)
		end
	end
	local function lucaUpdate(o, dt, realDt)
		local c = stellaMod.luca
		local name = S.bird
		if S.phase == "aim" then
			S.aimTimer = S.aimTimer + realDt
			S.waveTimer = (S.waveTimer or 0) + realDt
			S.glowTimer = (S.glowTimer or 0) + realDt
			S.darkening = min(1, S.aimTimer / c.bgDarkeningTweenInDuration)
			setMult(c.aimSlowmo)
			local dx, dy = aimDirection(o)
			S.dx, S.dy = dx, dy
			S.preview = lucaCast(o, dx, dy)
			local set = {}
			for _, h in ipairs(S.preview.collisions) do
				local t = objects.world[h.name]
				if t ~= nil and c.breakableMaterials[matOf(t)] then set[h.name] = 1 end
			end
			setBlocks(S.hl, set)
			local done = aimProgress(S.aimTimer, c.aimSlowmo, c.aimDurationStable, c.aimDurationSpeedUp)
			if done or S.release then S.release = nil lucaRelease(o) end
		elseif S.phase == "scream" then
			setMult(c.waveWorldSpeed)       -- the world stands still while the wave runs (setDeltaTimeMultiplier(1e-5))
			local w = S.waves[1]
			w.timer = w.timer + realDt
			if not w.reachedEnd and w.timer >= w.duration * c.waveTimeShare then
				w.reachedEnd = true
				later(c.beatAfterScreamDuration, function()
					lucaApply(w, o)
					w.finished = true
				end, true)
			end
			if w.finished then
				S.phase = "fade"
				S.fade = 1
				lucaFade = { amount = 1, speed = 1 / c.bgDarkeningTweenOutDuration }
				infos[name].used = true
				setMult(1)
				restoreBird(name, infos[name])
				S = nil
			end
		end
	end

	------------------------------------------------ Poppy: drop
	local function poppyStart(name, o)
		local c = stellaMod.poppy
		S = { kind = "poppy", bird = name, phase = "aim", aimTimer = 0, rot = getRotation ~= nil and getRotation(name) or 0 }
		setRotation(name, 0)
		setMult(c.aimSlowmo)
	end
	local function poppySensor(o)
		local c = stellaMod.poppy
		local u = stellaMod.unit
		return o.x, o.y + c.sensorOffsetY * u, c.sensorWidth * u * 0.5, c.sensorHeight * u * 0.5
	end
	local function overlapped(cx, cy, hw, hh, skip)    -- the objects a box overlaps (their centre and extent)
		local out = {}
		for n, t in pairs(objects.world) do
			if n ~= skip and not ignored(n) and not isBirdObj(t) then
				local ew, eh
				if t.radius ~= nil then ew, eh = t.radius, t.radius else ew, eh = (t.width or 0) * 0.5, (t.height or 0) * 0.5 end
				if t.angle ~= nil and t.width ~= nil then local a = abs(sin(t.angle)) local b = abs(cos(t.angle)) ew, eh = ew * b + eh * a, ew * a + eh * b end
				if abs(t.x - cx) <= hw + ew and abs(t.y - cy) <= hh + eh then out[#out + 1] = t end
			end
		end
		return out
	end
	local function poppyRelease(o)
		local i = infoOf(S.bird, o)
		S.phase, S.timer, S.x, S.y = "triggered", 0, o.x, o.y
		setMult(1)
		setVelocity(S.bird, 0, 0)
	end
	local function poppyStop()
		if S == nil or S.phase == "stopped" then return end
		resetFactors(S.bird)
		finish()
	end
	local function poppyUpdate(o, dt, realDt)
		local c = stellaMod.poppy
		local u = stellaMod.unit
		local name = S.bird
		if S.phase == "aim" then
			S.aimTimer = S.aimTimer + realDt
			local done = aimProgress(S.aimTimer, c.aimSlowmo, c.aimDurationStable, c.aimDurationSpeedUp)
			local hit = c.aimLineRaycast and castHits(o.x, o.y, o.x, o.y + c.aimLineLength * u, name)[1] or nil
			S.tapY = hit ~= nil and hit.y or o.y + c.aimLineLength * u
			setRotation(name, 0)
			if done or S.release then S.release = nil poppyRelease(o) end
		elseif S.phase == "triggered" then
			S.timer = S.timer + dt
			setPosition(name, S.x, S.y)
			setRotation(name, 0)
			setVelocity(name, 0, 0)
			if S.timer > c.bulletTimeDuration then
				setAngularVelocity(name, 0)
				S.phase = "drill"
			end
		elseif S.phase == "drill" then
			setRotation(name, 0)
			setFactors(name, c.damageFactors)
			local f = (infos[name].collided and c.downDrillForceMultplierAfterCollision or 1) * c.downDrillForce * stellaMod.accelScale * u / 6
			local _, vx, vy = getVelocity(name)
			setVelocity(name, vx, vy + f * dt)
			local cx, cy, hw, hh = poppySensor(o)
			for _, t in ipairs(overlapped(cx, cy, hw, hh, name)) do
				if not t.controllable then
					doDamage(t, c.damage * (factorOf(c.damageFactors, t) or 1) * dt)
				end
			end
		end
	end

	------------------------------------------------ Willow: dash
	local function willowDir(name)
		local _, vx, vy = getVelocity(name)
		local len = sqrt(vx * vx + vy * vy)
		if len < 0.001 then return 1, 0 end
		return vx / len, vy / len
	end
	local function willowStart(name, o, passive)
		local c = stellaMod.willow
		local i = infoOf(name, o)
		local dx, dy = willowDir(name)
		S = { kind = "willow", bird = name, phase = "active", speed0 = getVelocity(name), timer = 0, holdTimer = 0, holding = false, released = false, aimTimer = 0, dx = dx, dy = dy, turned = 0, spin = 0,
			duration = passive and c.durationAfterCollision or c.duration, hasTapped = not passive, oldDensity = o.density }
		setObjectGravity(name, 0, 0)
		o.density = c.densityWhenRolling
		setFactors(name, c.damageFactors)
	end
	local function willowRelease()
		local c = stellaMod.willow
		S.released, S.holding, S.boostTimer = true, false, c.boostForceDuration
	end
	local function willowStop()
		local name = S.bird
		local o = objects.world[name]
		if o ~= nil then
			resetObjectGravity(name)
			if S.oldDensity ~= nil then o.density = S.oldDensity end
		end
		resetFactors(name)
		local i = infos[name]
		if i ~= nil then i.used = true end
		S = nil
		setMult(1)
	end
	local function willowUpdate(o, dt, realDt, down, released)
		local c = stellaMod.willow
		local u = stellaMod.unit
		local name = S.bird
		local i = infos[name]
		S.spin = S.spin + realDt
		S.aimTimer = S.aimTimer + realDt
		setMult(1)
		local aimDone = false
		if c.useNewAbility and not i.collided and not S.released then
			aimDone = aimProgress(S.aimTimer, c.aimSlowmo, c.aimDurationStable, c.aimDurationSpeedUp)
		end
		S.timer = S.timer + realDt
		if S.timer >= S.duration then willowStop() return end
		local p = min(1, S.timer / c.duration)         -- the spin slows down over the duration
		setRotation(name, (o.angle or 0) + (c.minSpinSpeed + (1 - p) * (c.spinSpeed - c.minSpinSpeed)) * pi / 180 * realDt)
		if released or aimDone or (i.collided and not S.hasTapped) then
			if (released or aimDone) and not S.released then S.timer, S.duration = 0, c.boostForceDuration end    -- letting go starts the boost phase on a new timer: the hold phase's clock does not run on into it
			if not S.released then willowRelease() end
		end
		if S.boostTimer ~= nil then
			S.boostTimer = S.boostTimer - dt
			if S.boostTimer <= 0 then S.boostTimer = nil end
			local f = c.boostForce * stellaMod.accelScale * u / 6
			local _, vx, vy = getVelocity(name)
			setVelocity(name, vx + S.dx * f * dt, vy + S.dy * f * dt)
		end
		if not S.holding and not S.released and down then
			S.holdTimer = S.holdTimer + realDt
			if S.holdTimer >= c.holdThresholdDuration then
				S.holding, S.holdTimer = true, 0
			end
		end
		if S.holding then
			local a = c.turnSpeed * dt
			local ca, sa = cos(a), sin(a)
			S.dx, S.dy = S.dx * ca - S.dy * sa, S.dx * sa + S.dy * ca
			S.turned = S.turned + a
			local sp = (S.speed0 or 0) * c.speedWhileTurning
			setVelocity(name, S.dx * sp, S.dy * sp)
			S.holdTimer = S.holdTimer + realDt
		end
		S.sx, S.sy = o.x, o.y
		local cx, cy, r = o.x, o.y, c.sensorRadius * u
		for _, t in ipairs(overlapped(cx, cy, r, r, name)) do
			if not t.controllable then doDamage(t, c.damage * (factorOf(c.damageFactors, t) or 1) * dt) end
		end
	end

	------------------------------------------------ Dahlia: teleport
	local function dahliaStart(name, o)
		local c = stellaMod.dahlia
		local u = stellaMod.unit
		local dx, dy = willowDir(name)
		if c.horizontalMovement then dx, dy = 1, 0 end
		S = { kind = "dahlia", bird = name, phase = "aim", dx = dx, dy = dy, aimingT = 0, aimingTimer = c.aimDuration, delay = c.delayBeforeAim, sx = o.x, sy = o.y, gx = o.x, gy = o.y, darkening = 0,
			hitGround = false, radius = radiusOf(name) }
		S.hl = newHighlighter(c.color, c.lightness, c.speed)
		-- the ghost stays inside the level: what is further out than every object in it is where the game removes a bird
		local x0, y0, x1, y1 = o.x, o.y, o.x, o.y
		for n, t in pairs(objects.world) do
			if n ~= name and t.x ~= nil and t.y ~= nil then x0, x1, y0, y1 = min(x0, t.x), max(x1, t.x), min(y0, t.y), max(y1, t.y) end
		end
		S.bx0, S.by0, S.bx1, S.by1 = x0, y0, x1, y1
		setRotation(name, atan2(dy, dx))
		setVelocity(name, 0, 0)
		setObjectGravity(name, 0, 0)
		setCollisionEnabled(name, false)
	end
	local function dahliaAffected()    -- the objects the ghost has overlapped on its way
		local c = stellaMod.dahlia
		local u = stellaMod.unit
		local set = S.affected or {}
		S.affected = set
		local hw, hh = c.ghostWidth * u * 0.5, c.ghostHeight * u * 0.5
		for _, t in ipairs(overlapped(S.gx, S.gy, hw, hh, S.bird)) do set[t.name] = t end
		return set
	end
	local function dahliaKick(o, point)
		local c = stellaMod.dahlia
		S.phase, S.kickTimer, S.rotation, S.glows, S.kicked = "kick", c.kickDuration, 0, {}, {}
		if point ~= nil then S.kx, S.ky = point.x, point.y end
		setCollisionEnabled(S.bird, false)
		setObjectGravity(S.bird, 0, 0)
		setVelocity(S.bird, 0, 0)
		setVisible(S.bird, true)
	end
	local function dahliaEndDash(o)
		local c = stellaMod.dahlia
		local k = c.passThroughForce * c.passThroughScale
		for n, t in pairs(S.affected or {}) do
			if objects.world[n] ~= nil then pushSpeed(n, S.dx * k, S.dy * k, t.x, t.y) end
		end
		setVisible(S.bird, true)
		setVelocity(S.bird, 0, 0)
		dahliaKick(o)
	end
	local function dahliaStartDash(o)
		local c = stellaMod.dahlia
		S.dashTimer = S.aimingT * c.dashDuration
		S.darkeningPeak = S.darkening
		disposeHighlighter(S.hl)
		S.hl = nil
		setMult(1)
		if S.dashTimer == 0 then dahliaEndDash(o) else
			S.phase, S.dashDuration, S.dashT = "dash", S.dashTimer, 0
			setVisible(S.bird, false)
		end
	end
	local function dahliaUpdate(o, dt, realDt, released)
		local c = stellaMod.dahlia
		local u = stellaMod.unit
		local name = S.bird
		if S.phase == "aim" then
			S.particleTimer = (S.particleTimer or 0) + realDt
			setPosition(name, S.sx, S.sy)
			setMult(c.aimSlowmo)
			if S.delay ~= nil then
				S.delay = S.delay - dt
				if S.delay <= 0 then S.delay = nil end
				S.aimingT = 0
			elseif not S.hitGround then
				local t = max(0, S.aimingTimer - dt)
				local p = 1 - t / c.aimDuration
				local e = 1 - (1 - p) * (1 - p)
				S.gx, S.gy = o.x + e * S.dx * c.maxDistance * u, o.y + e * S.dy * c.maxDistance * u
				if S.bx0 ~= nil then S.gx, S.gy = clamp(S.gx, S.bx0, S.bx1), clamp(S.gy, S.by0, S.by1) end
				S.aimingTimer, S.aimingT = t, e
				setRotation(name, atan2(S.dy, S.dx))
				local set = {}
				for n, t2 in pairs(dahliaAffected()) do if (t2.mass or 0) > 0 then set[n] = c.aimHighlightIntensity end end
				setBlocks(S.hl, set)
				local g = castHits(S.sx, S.sy, S.gx, S.gy, name)
				-- (4.5) only the ground stops the ghost (Stella's StaticGround): the immovable blocks of a Classic level are blocks like any other, the ghost goes through them
				for _, h in ipairs(g) do
					local t2 = objects.world[h.name]
					if t2 ~= nil and (t2.mass or 0) <= 0 and (h.name == "ground" or matOf(t2) == "staticGround" or find(t2.definition or "", "Ground", 1, true)) then S.hitGround = true S.gx, S.gy = h.x, h.y break end
				end
			end
			S.darkening = min(1, S.darkening + realDt / c.bgDarkeningDuration)
			if S.aimingTimer ~= 0 and not released and not S.hitGround then
			else
				setMult(1)
				dahliaStartDash(o)
			end
		elseif S.phase == "dash" then
			S.dashTimer = max(0, S.dashTimer - dt)
			local p = 1 - S.dashTimer / S.dashDuration
			local t = p * p * p
			t = t * t
			setPosition(name, S.sx + t * (S.gx - S.sx), S.sy + t * (S.gy - S.sy))
			setVelocity(name, 0, 0)
			S.dashT = t
			S.darkening = (1 - t) * (1 - t) * (1 - t) * S.darkeningPeak          -- tweenEaseCubicOut(1 - t)
			if S.dashTimer == 0 then dahliaEndDash(o) end
		elseif S.phase == "kick" then
			S.kickTimer = max(0, S.kickTimer - dt)
			S.rotation = (1 - S.kickTimer / c.kickDuration) * pi * 2
			setRotation(name, -S.rotation + pi * 0.5)        -- the body turns to the direction the kick pushes in
			setMult(c.kickSlowmo)
			local cx, cy = S.kx or o.x, S.ky or o.y
			cx, cy = o.x, o.y
			local rr = c.explosionRadius * u
			-- the kick is a leg that turns with the body: it points where the body does (th, going from straight down all the way round), and an object is struck on the frame
			-- the leg passes the object's bearing from the bird; it is pushed the way the leg points, which is where the body faces
			local th = -S.rotation + pi * 0.5
			local k = c.explosionForce * c.explosionScale
			for _, t in ipairs(overlapped(cx, cy, rr, rr, name)) do
				if t.mass ~= 0 and S.kicked[t.name] == nil then
					local b = atan2(t.y - cy, t.x - cx)
					while b > pi * 0.5 do b = b - 2 * pi end
					while b <= pi * 0.5 - 2 * pi do b = b + 2 * pi end
					if th <= b then
						S.kicked[t.name] = true
						S.glows[t.name] = { time = 1, x = t.x, y = t.y }
						pushSpeed(t.name, cos(th) * k * 0.1, sin(th) * k * 0.1, t.x, t.y)
					end
				end
			end
			for n, g in pairs(S.glows) do g.time = g.time - dt if g.time <= 0 then S.glows[n] = nil end end
			S.darkening = max(0, S.darkening - realDt)
			if S.kickTimer == 0 then
				restoreBird(name, infos[name])
				setVelocity(name, c.hopX * u, c.hopY * u)
				setMult(1)
				infos[name].used = true
				S = nil
			end
		end
	end

	------------------------------------------------ the five: starting, releasing, stopping
	local kinds = {
		stella = { start = stellaStart, update = stellaUpdate, release = function(o) S.release = true end },
		luca = { start = lucaStart, update = lucaUpdate, release = function(o) S.release = true end },
		poppy = { start = poppyStart, update = poppyUpdate, release = function(o) S.release = true end },
		willow = { start = willowStart, update = willowUpdate },
		dahlia = { start = dahliaStart, update = dahliaUpdate },
	}

	-- the bird that can start an ability now: Classic's flying bird, if it is one of these and its ability is still there
	local function candidate()
		if currentGameMode ~= updateGame or objects.world == nil then return nil end
		local fb = flyingBird
		if fb == nil or fb.name == nil then return nil end
		local o = objects.world[fb.name]
		local kind = o ~= nil and stellaMod.birds[o.definition or ""] or nil
		if not kind then return nil end
		local i = infoOf(fb.name, o)
		if i.used or i.disabled then return nil end
		if getVelocity(fb.name) < stellaMod.minSpeed and not i.lastChance then return nil end
		if i.collided and kind ~= "luca" and kind ~= "poppy" and not (kind == "stella" and i.lastChance) then return nil end
		return fb.name, o, kind, i
	end
	stellaMod.start = function(kind)
		if S ~= nil then return false end
		local name, o, k, i = candidate()
		if name == nil then return false end
		kind = kind or k
		if kinds[kind] == nil then return false end
		kinds[kind].start(name, o)
		return true
	end
	stellaMod.release = function()
		if S ~= nil and kinds[S.kind].release ~= nil then kinds[S.kind].release() end
	end
	stellaMod.state = function() return S end
	stellaMod.active = function(name) return S ~= nil and S.bird == name end
	stellaMod.cancel = function()
		if S ~= nil then
			if S.kind == "willow" then willowStop() else finish() end
		end
	end

	local function holdAbilities()
		-- the five birds' own abilities are switched off: the game sees a specialty it does not know and does nothing
		local blocks = blockTable ~= nil and blockTable.blocks or nil
		if blocks == nil then return end
		registerFactors()
		for def, kind in pairs(stellaMod.birds) do
			local b = blocks[def]
			if b ~= nil then
				if kind and stellaMod.enabled then
					if b.specialty ~= "STELLA" then b.stellaOldSpecialty = b.specialty b.specialty = "STELLA" end
				elseif b.stellaOldSpecialty ~= nil then
					b.specialty = b.stellaOldSpecialty b.stellaOldSpecialty = nil
				end
			end
		end
	end

	engine.on("levelLoaded", function()
		infos, timers, highlights, lucaFade, wasDown, predictSlowed = {}, {}, {}, nil, false, false
		S = nil
		setMult(1)
	end)

	-- Stella's onCollision of every component, on the bird's first touch (and Poppy's drill on every one)
	engine.on("contact", function(a, b)
		if objects.world == nil then return end
		local name, other
		local fa, fb = flyingBird ~= nil and flyingBird.name or nil, nil
		if a == fa then name, other = a, b elseif b == fa then name, other = b, a end
		if name == nil and S ~= nil then
			if a == S.bird then name, other = a, b elseif b == S.bird then name, other = b, a end
		end
		if name == nil or ignored(other) then return end
		local o = objects.world[name]
		local kind = o ~= nil and stellaMod.birds[o.definition or ""] or nil
		if not kind or not stellaMod.enabled then return end
		local i = infoOf(name, o)
		local first = not i.collided
		i.collided = true
		if S ~= nil and S.bird == name then
			if kind == "poppy" and (S.phase == "drill" or S.phase == "triggered") then
				local c = stellaMod.poppy
				if not S.stopTimerSet then
					S.stopTimerSet = true
					local mine = S
					later(c.drillDuration, function() if S == mine and S ~= nil then poppyStop() end end)
				end
			elseif kind == "willow" and first == false then
			elseif S.phase == "aim" and first then
				local c = stellaMod[S.kind]
				if S.kind ~= "dahlia" then
					local mine = S
					later(c.forceActivationAfterCollisionWhileAimingDuration, function() if S == mine and S ~= nil and S.phase == "aim" then S.release = true end end, true)
				end
			end
			if kind == "willow" and first then
				local c = stellaMod.willow
				S.timer = max(S.timer, S.duration - c.durationAfterCollisionWhenAbilityActivated)
				if not S.released then willowRelease() end
			end
			return
		end
		if not first or i.used or i.disabled then return end
		if kind == "stella" then       -- the last chance: the game almost stops for lastChanceDelayWhenColliding, in which the ability can still be started
			local c = stellaMod.stella
			i.lastChance = true
			setMult(0.05)
			later(c.lastChanceDelayWhenColliding, function()
				if i.lastChance and (S == nil or S.bird ~= name) then
					i.lastChance, i.used = false, true
					setMult(1)
				end
			end, true)
		elseif kind == "luca" or kind == "poppy" then
			local c = stellaMod[kind]
			later(c.disableAfterCollision, function()
				if infos[name] == i and not i.used and (S == nil or S.bird ~= name) then i.disabled = true end
			end)
		elseif kind == "willow" then
			later(0, function()
				if S == nil and infos[name] == i and not i.used and objects.world[name] ~= nil then willowStart(name, objects.world[name], true) end
			end)
		elseif kind == "dahlia" then
			later(0, function()
				if S == nil and infos[name] == i and not i.used and objects.world[name] ~= nil then
					local o2 = objects.world[name]
					S = { kind = "dahlia", bird = name, phase = "kick", dx = 0, dy = 0, affected = {}, sx = o2.x, sy = o2.y, gx = o2.x, gy = o2.y, darkening = 0 }
					dahliaKick(o2, { x = o2.x, y = o2.y })
				end
			end)
		end
	end)

	engine.on("update", function(dt, realDt)
		holdAbilities()
		realDt = dt            -- the frame's real time; dt becomes the physics' time (what the world moved in this frame)
		dt = dt * getMult()
		local touch = touchcount ~= nil and touchcount > 0
		local down = (keyPressed ~= nil and (keyPressed["LBUTTON"] or keyPressed["SPACE"])) or (keyHold ~= nil and (keyHold["LBUTTON"] or keyHold["SPACE"])) or touch or stellaMod.forceDown == true   -- forceDown: for scripts and tests, the button counts as held
		local pressed = down and not wasDown
		local released = wasDown and not down
		-- paused (or in a pop-up): a running ability waits, with its timers, and goes on when the game does; a button let go meanwhile counts on the first frame after
		if stellaMod.enabled and S ~= nil and currentGameMode ~= updateGame and objects.world ~= nil and objects.world[S.bird] ~= nil then return end
		wasDown = down and true or false
		if not stellaMod.enabled then
			if S ~= nil then stellaMod.cancel() end
			return
		end
		-- callDelayed
		local k = 1
		while k <= #timers do
			local t = timers[k]
			t.left = t.left - (t.real and realDt or dt)
			if t.left <= 0 then remove(timers, k) t.fn() else k = k + 1 end
		end
		if lucaFade ~= nil then
			lucaFade.amount = lucaFade.amount - lucaFade.speed * realDt
			if lucaFade.amount <= 0 then lucaFade = nil end
		end
		updateHighlighters(realDt)
		if S ~= nil then
			local o = objects.world ~= nil and objects.world[S.bird] or nil
			if o == nil or currentGameMode ~= updateGame then
				if S.kind == "willow" then willowStop() else finish() end
			else
				-- (4.5) the game removes a shot bird that has moved slower than 0.05 for a second (isReadyForRemoveTimer in updateGame): an ability that holds the
				-- bird still (Stella between her jumps, Dahlia while she aims and dashes, Luca's scream) lost the bird halfway. While an ability runs the clock is kept full
				o.isReadyForRemoveTimer = 1.0
				o.frozen = false
				if type(setSleeping) == "function" then setSleeping(S.bird, false) end
				if S.kind == "willow" then
					kinds.willow.update(o, dt, realDt, down, released)
				elseif S.kind == "dahlia" then
					kinds.dahlia.update(o, dt, realDt, released)
				else
					if S.phase == "aim" and released then S.release = true end
					kinds[S.kind].update(o, dt, realDt)
				end
			end
		else
			local name, o, kind, i = candidate()
			if name ~= nil then
				if pressed then
					stellaMod.start()
				elseif kind == "stella" and not i.lastChance then
					predictSlowed = stellaPredict(name, o) or predictSlowed
					if not predictSlowed then end
				end
			elseif predictSlowed then
				predictSlowed = false
				setMult(1)
			end
			if name ~= nil and kind == "stella" and not predictSlowed == false and not stellaPredict(name, o) then
				predictSlowed = false
				if not i.lastChance then setMult(1) end
			end
		end
	end)

	------------------------------------------------ drawing: the darkening behind the scene, the aids on top
	engine.on("drawBackground", function()
		if S ~= nil then
			local c = stellaMod[S.kind]
			if S.kind == "stella" and S.darkening ~= nil then darken(ease() and (S.darkening) * c.bgDarkeningAmount)
			elseif S.kind == "luca" and S.darkening ~= nil then darken(min(1, S.darkening) * c.bgDarkeningAmount)
			elseif S.kind == "dahlia" and S.darkening ~= nil then darken(S.darkening * c.bgDarkeningAmount) end
		elseif lucaFade ~= nil then
			darken(lucaFade.amount * stellaMod.luca.bgDarkeningAmount)
		end
	end)
	engine.on("drawGame", function()
		if S == nil or objects.world == nil or objects.world[S.bird] == nil then return end
		local o = objects.world[S.bird]
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
		if S.kind == "stella" then
			local c = stellaMod.stella
			if S.phase == "aim" and S.path ~= nil then
				-- StellaAbility's aim drawing (r19_0): a dot every half Stella unit along each jump, marching with the aim timer; they grow from the tip (aimAppearTimer) with a sine,
				-- fade with the distance along the path, and each bounce gets a wall marker
				local u = stellaMod.unit
				local maxD = c.maxDistance
				local x0, y0 = toScreen(0, 0)
				local x1, y1 = toScreen(1, 0)
				local ws = x1 - x0                         -- pixels in a physics unit
				local path = S.path
				local lens, sum = {}, 0
				for k = 2, #path do lens[k] = sqrt((path[k].x - path[k - 1].x) ^ 2 + (path[k].y - path[k - 1].y) ^ 2) / u sum = sum + lens[k] end
				local appear = min(1, S.aimAppear)
				local count = (ceil(sum * 2) + 1 + 11 * #path) * appear       -- r7_18: how many dots are out so far
				local phase = (S.aimTimer % 0.5) * 2                          -- r9_18: the marching offset
				local drawn, along = 0, 0
				for k = 2, #path do
					local a, b = path[k - 1], path[k]
					local len = lens[k]
					if len > 0.0001 then
						local take = min(len, maxD - along)
						local dx, dy = (b.x - a.x) / len / u * take, (b.y - a.y) / len / u * take      -- in Stella units
						local n = ceil(take * 2)
						for m = 1, n do
							drawn = drawn + 1
							if drawn <= count then
								local f0, f1 = (m - 1) / n, m / n
								local px = a.x + (dx * (f0 + phase * (f1 - f0))) * u
								local py = a.y + (dy * (f0 + phase * (f1 - f0))) * u
								local size = sin(clamp(0.1 * (count - drawn - phase), 0, 1) * pi * 0.83) * 2
								local alpha = clamp(1 - (along + sqrt((px - a.x) ^ 2 + (py - a.y) ^ 2) / u) / maxD, 0, 1)
								if size > 0.01 then
									local sx, sy = toScreen(px, py)
									local d = 0.17 * u * ws * size
									setRenderState(0, 0, 1, 1, 0, 0, 0, alpha)
									_G.res.drawSprite("TRAIL_WHITE_1", sx, sy, "VCENTER", "HCENTER", d, d)
								end
							end
						end
						along = along + take
						if maxD < along then break end
						if drawn <= count and b.nx ~= nil then       -- the wall marker (STELLA_5 in the game): a short tick along the wall, seen from the tip
							local size = sin(clamp(0.1 * (count - drawn), 0, 1) * pi * 0.83) * 2
							local fade = (0.5 + (S.aimTimer or 0) / (c.aimDurationStable + c.aimDurationSpeedUp) * 0.5) * (1 - 0.5 * along / maxD)
							local tx, ty = toScreen(b.cx - b.ny * 0.9 * size, b.cy + b.nx * 0.9 * size)
							local ux, uy = toScreen(b.cx + b.ny * 0.9 * size, b.cy - b.nx * 0.9 * size)
							drawLine2D(tx, ty, ux, uy, 4, 255, 255, 255, 255 * min(0.6, fade * 1.5))
						end
						drawn = drawn + 10
					end
				end
				setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
			elseif S.trail ~= nil then
				for k = 2, #S.trail do
					local p, q = S.trail[k - 1], S.trail[k]
					local fade = 1 - (p.disappear or 0)
					if fade > 0 then
						local sx1, sy1 = toScreen(p.x, p.y)
						local sx2, sy2 = toScreen(q.x, q.y)
						drawLine2D(sx1, sy1, sx2, sy2, 8 * fade, 255, 200, 200, 150 * fade)
					end
				end
			end
		elseif S.kind == "luca" then
			local c = stellaMod.luca
			if S.phase == "aim" and S.preview ~= nil then
				-- LucaAbility's aim drawing (L10_1): wave marks every 0.65 of a mark's width along the ray, marching with waveTimer (a quarter second a step),
				-- the first and the last two fading in and out; a glow with the wall's normal where a block stops the ray, a target where it ends in the open
				local w = S.preview
				local x0, y0 = toScreen(0, 0)
				local x1, y1 = toScreen(1, 0)
				local ws = x1 - x0
				local step = 0.45                                    -- the mark's width (physics units) times 0.65
				local n = floor(w.maxDistance / step)
				local ang = atan2(w.dy, w.dx)
				local march = ((S.waveTimer or 0) % 0.25) * 4 * step
				for i = 0, n do
					local a = 1
					if i < 2 then a = min(1, i / 2 + march / step / 2)
					elseif i >= n - 2 then a = max(0, (n - i) / 2 - march / step / 2) end
					if a > 0.01 then
						local px = w.x1 + w.dx * (i * step + march)
						local py = w.y1 + w.dy * (i * step + march)
						local sx, sy = toScreen(px, py)
						local len, th = 0.34 * ws, 0.13 * ws
						local ca, sa = cos(ang), sin(ang)
						local nxp, nyp = -sa * th * 0.5, ca * th * 0.5
						drawLine2D(sx - ca * len * 0.5 + nxp, sy + sa * len * 0.5 - nyp, sx + ca * len * 0.5 - nxp, sy - sa * len * 0.5 + nyp, 4, 120, 200, 255, 255 * a)
						drawLine2D(sx - ca * len * 0.5, sy + sa * len * 0.5, sx + ca * len * 0.5, sy - sa * len * 0.5, 2, 255, 255, 255, 255 * a)
					end
				end
				local pulse = sin(min(pi, (S.glowTimer or 0) / 0.02 * pi))
				if w.stopName ~= nil then
					-- STELLA_BOUNCE_GLOW: a flat glow lying on the wall, turned with its normal
					local na = atan2(-w.dy, -w.dx)
					for k = 1, 3 do
						local rr = (0.5 + 0.35 * k) * 0.9
						ring(w.x2, w.y2, rr, 3, 120, 200, 255, 90 / k)
					end
					local ex, ey = toScreen(w.x2, w.y2)
					local tx, ty = cos(na + pi / 2) * 1.6 * ws, sin(na + pi / 2) * 1.6 * ws
					drawLine2D(ex - tx, ey + ty, ex + tx, ey - ty, 6, 160, 220, 255, 200)
				else
					-- LUCA_TARGET: a ring with a cross where the ray ends in the open
					local ex, ey = toScreen(w.x2, w.y2)
					ring(w.x2, w.y2, 0.8, 3, 120, 200, 255, 230)
					ring(w.x2, w.y2, 0.4, 2, 255, 255, 255, 230)
					drawLine2D(ex - 0.55 * ws, ey, ex + 0.55 * ws, ey, 2, 255, 255, 255, 200)
					drawLine2D(ex, ey - 0.55 * ws, ex, ey + 0.55 * ws, 2, 255, 255, 255, 200)
				end
			elseif S.phase == "scream" then
				local w = S.waves[1]
				-- (4.5) the wave reaches the end of the ray when the scream strikes (waveTimeShare of its time) and waits there for the beat: drawn over the whole
				-- time, it was cut off at about two thirds of the ray when the ability ended
				local k = clamp(w.timer / max(0.001, w.duration * stellaMod.luca.waveTimeShare), 0, 1)
				local sx1, sy1 = toScreen(w.x1, w.y1)
				local sx2, sy2 = toScreen(w.x1 + (w.x2 - w.x1) * k, w.y1 + (w.y2 - w.y1) * k)
				drawLine2D(sx1, sy1, sx2, sy2, 12, 90, 180, 255, 110)
				drawLine2D(sx1, sy1, sx2, sy2, 3, 255, 255, 255, 255)
				ring(w.x1 + (w.x2 - w.x1) * k, w.y1 + (w.y2 - w.y1) * k, 1.5, 3, 200, 230, 255, 220)
			end
		elseif S.kind == "poppy" and S.phase == "aim" and S.tapY ~= nil then
			-- PoppyAbility's aim drawing (L11_1): an aiming mark every half unit along the line to the tap (ceil(length * 2) of them, 0.9 of the way),
			-- and a twirl at the tap that cycles through four frames in 0.2 s
			local len = abs(S.tapY - o.y)
			local n = ceil(len / stellaMod.unit * 2)
			local x0, y0 = toScreen(0, 0)
			local x1, y1 = toScreen(1, 0)
			local ws = x1 - x0
			for i = 1, n do
				local sx, sy = toScreen(o.x, o.y + (S.tapY - o.y) * 0.9 * (i / n))
				setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
				_G.res.drawSprite("TRAIL_WHITE_1", sx, sy, "VCENTER", "HCENTER", 0.17 * stellaMod.unit * ws, 0.17 * stellaMod.unit * ws)
			end
			local frame = floor(1 + 3 * ((S.aimTimer or 0) % 0.2) / 0.2)      -- POPPY_TWIRL_GROUND_1..4
			for k = 0, 3 do
				ring(o.x, S.tapY, (0.5 + 0.18 * ((frame + k) % 4)) * (k == 0 and 1.2 or 0.7), k == 0 and 3 or 1, 255, 220, 60, k == 0 and 230 or 110)
			end
		elseif S.kind == "poppy" and S.phase == "drill" then
			local cx, cy, hw, hh = poppySensor(o)
			local x1, y1 = toScreen(cx - hw, cy - hh)
			local x2, y2 = toScreen(cx + hw, cy + hh)
			drawRectLines(x1, y1, x2, y2, 2, 255, 220, 60, 200)
		elseif S.kind == "willow" then
			local c = stellaMod.willow
			local len = c.aimLineLength
			if not S.released then
				-- WillowAbility's aim drawing (L5_1): the line is cut where a ray hits something; floor(length * 3) aiming marks along it, fading from full to 0.1;
				-- a twirl lying on the surface at the contact, turned with its normal
				local x0, y0 = toScreen(0, 0)
				local x1, y1 = toScreen(1, 0)
				local ws = x1 - x0
				local L = len
				local contact
				for _, h in ipairs(castHits(o.x, o.y, o.x + S.dx * len, o.y + S.dy * len, S.bird)) do
					L = len * h.fraction contact = h break
				end
				local n = floor(L / stellaMod.unit * 3)
				for i = 1, n do
					local f = i / n
					local sx, sy = toScreen(o.x + S.dx * L * f, o.y + S.dy * L * f)
					setRenderState(0, 0, 1, 1, 0, 0, 0, 0.1 + 0.9 * (1 - f * L / len))
					_G.res.drawSprite("TRAIL_WHITE_1", sx, sy, "VCENTER", "HCENTER", 0.17 * stellaMod.unit * ws, 0.17 * stellaMod.unit * ws)
				end
				setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
				if contact ~= nil then
					local cx, cy = o.x + S.dx * L, o.y + S.dy * L
					local sx, sy = toScreen(cx, cy)
					local tx, ty = -S.dy * 0.9 * ws, S.dx * 0.9 * ws
					drawLine2D(sx - tx, sy + ty, sx + tx, sy - ty, 4, 120, 255, 160, 220)
					ring(cx, cy, 0.35, 2, 255, 255, 255, 200)
				end
			end
			ring(o.x, o.y, c.sensorRadius * stellaMod.unit, 2, 120, 255, 160, 180)
		elseif S.kind == "dahlia" then
			local c = stellaMod.dahlia
			if S.phase == "aim" or S.phase == "dash" then
				-- DahliaAbility's aim drawing (L17_1): 20 particles, each with its own height across the path and speed, flow from the bird to the ghost
				-- (position (x + (10 * particleTimer % speed) / speed) % 1), wobble with a sine and colour from orange to pink along the way; a bigger end mark
				if S.particles == nil then
					S.particles = {}
					for i = 1, 20 do S.particles[i] = { x = _G.math.random(), y = _G.math.random(), speed = 0.5 * (1 + _G.math.random()) } end
				end
				local ang = atan2(S.gy - S.sy, S.gx - S.sx)
				local ca, sa = cos(ang), sin(ang)
				local x0, y0 = toScreen(0, 0)
				local x1, y1 = toScreen(1, 0)
				local ws = x1 - x0
				for _, q in ipairs(S.particles) do
					local f = (q.x + ((10 * (S.particleTimer or 0)) % q.speed) / q.speed) % 1
					local wob = (2 * (q.y - 0.5) + 0.2 * sin(f * pi * 6)) * S.radius
					local px = S.sx + (S.gx - S.sx) * f - sa * wob
					local py = S.sy + (S.gy - S.sy) * f + ca * wob
					local sx, sy = toScreen(px, py)
					local r = 1.5 * (0.91 + (0.99 - 0.91) * f)
					local g = 1.5 * (0.59 + (0.71 - 0.59) * f)
					local b = 1.5 * (0.40 + (0.99 - 0.40) * f)
					local d = 0.125 * 1.2 * stellaMod.unit * ws
					setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
					drawLine2D(sx - ca * d, sy + sa * d, sx + ca * d, sy - sa * d, 0.45 * d, min(255, 255 * r), min(255, 255 * g), min(255, 255 * b), 255)
				end
				local ex, ey = toScreen(S.gx, S.gy)
				drawLine2D(ex - sa * S.radius * ws, ey - ca * S.radius * ws, ex + sa * S.radius * ws, ey + ca * S.radius * ws, 5, 255, 190, 255, 255)
				ring(S.gx, S.gy, S.radius, 3, 255, 255, 255, 230)
			elseif S.phase == "kick" then
				local rr = c.explosionRadius * stellaMod.unit
				local a0 = -S.rotation + pi * 0.5
				local sx1, sy1 = toScreen(o.x, o.y)
				local sx2, sy2 = toScreen(o.x + cos(a0) * rr, o.y + sin(a0) * rr)
				drawLine2D(sx1, sy1, sx2, sy2, 4, 255, 230, 160, 255)
				ring(o.x, o.y, rr, 2, 255, 230, 160, 160)
				for n, g in pairs(S.glows or {}) do ring(g.x, g.y, 0.8 * g.time ^ 5 + 0.2, 3, 255, 240, 200, 255 * g.time) end
			end
		end
	end)
end
