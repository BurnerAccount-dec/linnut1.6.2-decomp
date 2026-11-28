-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 11. the Christmas tree slingshot (4.5)
-- Angry Birds Friends 2.0.0's Christmas tree slingshot (slingshots.XmasTree, scripts/slingshots/slingshot_xmas.lua) in Classic 1.6.2 / 1.6.3.1, built the way
-- Friends builds it: six tree parts stacked on the slingshot's spot, the lowest one fixed, each of the others held to the one below by a hinge (turning at most
-- 0.3 pi either way) and two stiff springs, the top one welded on. The bird in the slingshot is tied to the fifth part, so the tree bends when the bird is pulled
-- back, and a stand-in bird sits on the tree; when the bird is shot five Christmas baubles fly with it, in a fan of 8 degrees, and pass through everything for
-- 0.2 seconds. The next bird comes after 1.5 seconds instead of 1. The normal slingshot and its rubber band are not drawn while the tree is up.
-- F6 in a level puts the tree up or takes it down (christmasSlingshot.key); christmasSlingshot.enabled = true puts it up in every level.
--
-- The pictures and sounds are Friends' own and are not in Classic: make a folder named xmas_slingshot in the game's data folder (beside scripts) and copy into it,
-- from the Angry Birds Friends 2.0.0 apk (open it as a zip):
--   assets/data/images/480x320_android/INGAME_INGAME_SLINGSHOT6.dat and INGAME_INGAME_SLINGSHOT6.pvr
--   assets/data/audio/sfx/xmas_slingshot_activation.mp3, xmas_slingshot_rubber_band.mp3, xmas_slingshot_release_shot.mp3, xmas_ornament_breaking_01.mp3,
--   xmas_ornament_breaking_02.mp3 and xmas_ornament_breaking_03.mp3
-- Without the pictures the tree is not put up. The sounds can be left out. Friends' falling snow is left out (its flakes are in a sheet Classic does not have).

christmasSlingshot = {
	enabled = false,
	key = "F6",
	folder = "xmas_slingshot",
	parts = 6,
	offsets = { -0.3, 2.5, 4.5, 6.3, 7.5, 9.5 },   -- how far above the slingshot's spot each part starts (metres)
	ammunition = 5,                               -- baubles a shot
	spread = 8,                                   -- the fan of the baubles (degrees)
	reloadDelay = 1.5,
	active = false,
}

do
	local C = christmasSlingshot
	local type, pairs, ipairs, cos, sin, atan2, pi, random = _G.type, _G.pairs, _G.ipairs, _G.math.cos, _G.math.sin, _G.math.atan2, _G.math.pi, _G.math.random
	local loaded, haveSprites, sounds = false, false, {}
	local pending = {}            -- baubles that do not collide yet: { name =, left = }
	local tip = "slingshotTree_5"

	local function load()
		if loaded then return haveSprites end
		loaded = true
		_G.res.createSpriteSheet(C.folder .. "/INGAME_INGAME_SLINGSHOT6.dat")
		haveSprites = type(spriteExists) ~= "function" or spriteExists("SLING_SHOT_TREE_PART_1")
		for _, s in ipairs({ "xmas_slingshot_activation", "xmas_slingshot_rubber_band", "xmas_slingshot_release_shot",
				"xmas_ornament_breaking_01", "xmas_ornament_breaking_02", "xmas_ornament_breaking_03" }) do
			local file = C.folder .. "/" .. s .. ".mp3"
			if type(fileExists) ~= "function" or fileExists(file) then
				_G.res.createAudio(file, s)
				sounds[s] = true
			end
		end
		audioGroups.xmas_ornament_break = { "xmas_ornament_breaking_01", "xmas_ornament_breaking_02", "xmas_ornament_breaking_03" }

		-- Friends' materials and blocks (blocks.lua), in Classic's names: material instead of materialName, collision = false for collider_types.none
		blockTable.materials.xmasSlingshot = { strength = 99999, defence = 99999, friction = 0.8, restitution = 0, density = 1, controllable = false, collisionSound = "" }
		blockTable.materials.xmasSlingshotAmmunition = { strength = 5, defence = 5, friction = 0.7, restitution = 0.4, density = 4, controllable = false,
			collisionSound = "", destroyedSound = sounds.xmas_ornament_breaking_01 and "xmas_ornament_break" or nil }
		local w, h = _G.res.getSpriteBounds("", "SLING_SHOT_TREE_PART_1")
		blockTable.blocks.slingshotTreeBase = { type = "box", sprite = "SLING_SHOT_TREE_PART_1", material = "xmasSlingshot", z_order = -1, collision = false,
			width = w * physicsScale * 0.92, height = h * physicsScale * 0.92, destroyedScoreInc = 0 }
		local sizes = { nil, { 1.6, 1.3, 2 }, { 1.4, 1.2, 0.7 }, { 1.2, 1.1, 0.5 }, { 1, 1, 0.3 }, { 0.8, 0.9, 0.1 } }
		for i = 2, 6 do
			blockTable.blocks["slingshotTree_" .. i] = { type = "box", width = sizes[i][1], height = sizes[i][2], density = sizes[i][3], sprite = "SLING_SHOT_TREE_PART_" .. i,
				material = "xmasSlingshot", z_order = 1, collision = false, destroyedScoreInc = 0 }
		end
		blockTable.blocks.slingshotFakeBird = { type = "circle", radius = 0.1, sprite = "BIRD_RED", density = 1.0E-4, material = "xmasSlingshot", z_order = 5, collision = false }
		for n = 1, 3 do
			-- Friends picks the picture and the pieces of each bauble when it makes it; Classic's createObject takes them from the block, so there are three blocks
			blockTable.blocks["xmasSlingshotAmmunition" .. n] = { type = "circle", radius = 1, sprite = "TREESLING_AMMO" .. n, material = "xmasSlingshotAmmunition",
				particles = "xmasSlingshotAmmo" .. n, destroyedScoreInc = 0, z_order = 0 }
			-- particles/xmasSlingshotAmmo1..3.lua of Friends, in Classic's particle format
			particles["xmasSlingshotAmmo" .. n] = { sprites = { "TREESLING_PARTICLE" .. n .. "_1", "TREESLING_PARTICLE" .. n .. "_2", "TREESLING_PARTICLE" .. n .. "_3", "TREESLING_PARTICLE" .. n .. "_4" },
				sheet = "INGAME_BIRDS_1", minVel = -100, maxVel = 100, minAngleVel = -8, maxAngleVel = 8, minScaleBegin = 1.8, maxScaleBegin = 2.7,
				minScaleEnd = 0.3, maxScaleEnd = 0.5, lifeTime = 1.7, gravityX = 0, gravityY = 120 }
		end
		return haveSprites
	end

	local function exists(name) return objects.world ~= nil and objects.world[name] ~= nil end
	local function remove(name)
		if exists(name) then
			removeObject(name)
			objects.world[name] = nil
		end
	end
	local function play(s) if sounds[s] then _G.res.playAudio(s, 1, false) end end

	-- slingshot.position of Friends: where the first bird stood when the level began (Classic's levelStartPosition is worked out from it, see initLevel)
	local function spot()
		local first = getNextBird(1)
		local r = first ~= nil and objects.world[first] ~= nil and objects.world[first].radius or 0
		local py = _G.select(2, _G.res.getSpritePivot("", "SLING_SHOT_01_BACK"))
		local sh = _G.select(2, _G.res.getSpriteBounds("", "SLING_SHOT_01_BACK"))
		return levelStartPosition.x, levelStartPosition.y + (sh - py) * physicsScale - r - 0.2
	end

	local function joint(t) _G.pcall(createJoint, t) end

	-- slingshots.XmasTree.functionality.activate
	local function activate()
		if C.active or objects.world == nil or not load() then return end
		local x, y = spot()
		createObject(blockTable, "slingshotTreeBase", "slingshotTree_1", x, y - C.offsets[1])
		setBodyType("slingshotTree_1", "static")
		for i = 2, C.parts do
			local name, below = "slingshotTree_" .. i, "slingshotTree_" .. (i - 1)
			createObject(blockTable, name, name, x, y - C.offsets[i])
			setLinearDamping(name, 0.1)
			if i == C.parts then
				joint({ name = name .. "_weldJointTo_" .. below, type = 2, end1 = below, end2 = name, coordType = 2, x1 = 0, y1 = 0, x2 = 0, y2 = 0, collideConnected = false })
			else
				joint({ name = name .. "_revoluteJointTo_" .. below, type = 3, end1 = below, end2 = name, coordType = 2, x1 = 0, y1 = 0, x2 = 0, y2 = 0,
					lowerLimit = -0.3 * pi, upperLimit = 0.3 * pi, limit = true, collideConnected = false })
				local o = objects.world[name]
				-- Friends gives both springs the same name; here they are told apart so that the second does not take the first one's place
				for k, side in ipairs({ 1, -1 }) do
					joint({ name = name .. "_distanceJointTo_" .. below .. (k == 1 and "" or "_2"), type = 1, end1 = below, end2 = name, coordType = 1,
						x1 = o.x, y1 = o.y, x2 = o.x + side, y2 = o.y - 1, frequency = 18, dampingRatio = 7, collideConnected = false })
				end
			end
		end
		C.active = true
		play("xmas_slingshot_activation")
	end

	-- slingshots.XmasTree.functionality.deactivate
	local function deactivate()
		if not C.active then return end
		C.active = false
		local bird = currentBirdName ~= nil and objects.world ~= nil and objects.world[currentBirdName] or nil
		if bird ~= nil and bird.jointToSlingshot then
			_G.pcall(destroyJoint, bird.jointToSlingshot)
			bird.jointToSlingshot = false
		end
		remove("fakeBird")
		if bird ~= nil then setVisible(bird.name, true) end
		for i = 1, C.parts do remove("slingshotTree_" .. i) end
	end
	C.activate, C.deactivate = activate, deactivate

	-- the bird in the slingshot is tied to the fifth part, and the stand-in bird is made (the first local function of slingshot_xmas.lua)
	local function attach(bird, part)
		local name = part.name .. "_distanceJointTo_" .. bird.name
		joint({ name = name, type = 1, end1 = bird.name, end2 = part.name, coordType = 2, x1 = 0, y1 = 0, x2 = 0, y2 = 0, frequency = 0, dampingRatio = 0.1, collideConnected = false })
		bird.jointToSlingshot = name
		remove("fakeBird")
		createObject(blockTable, "slingshotFakeBird", "fakeBird", part.x + bird.radius, part.y)
		setBodyType("fakeBird", "kinematic")
		local sx, sy = getScale(bird.name)
		if sx ~= nil then setScale("fakeBird", sx, sy) end
		setVisible(bird.name, false)
	end

	-- updateIdle: the stand-in bird sits on the fifth part, turned with it, and wears the bird's picture
	local function follow()
		local part = objects.world[tip]
		local bird = currentBirdName ~= nil and objects.world[currentBirdName] or nil
		if bird == nil or part == nil then return end
		if not bird.jointToSlingshot and birdReady and not bird.shot then attach(bird, part) end
		local fake = objects.world.fakeBird
		if fake ~= nil then
			local a = getAngle(part.name)
			setPosition(fake.name, part.x + cos(a) * bird.radius, part.y + sin(a) * bird.radius)
			if bird.sprite ~= nil and bird.sprite ~= fake.sprite then
				fake.sprite = bird.sprite
				setSprite(fake.name, bird.sprite)
			end
		end
	end

	-- onBirdShot: the tie is cut, the bird shows again and the baubles fly (the second local function: the shot of the ammunition)
	local function shootAmmunition(bird, fraction)
		local force = -((getObjectDefinition(bird.name) or {}).slingshotLaunchForce or defaultForce) * physicsScale
		local angle = atan2(levelStartPosition.y - rubberBandPos.y, levelStartPosition.x - rubberBandPos.x)
		local spread = C.spread * pi / 180
		for i = 1, C.ammunition do
			local name = bird.name .. "_xmasBall_" .. i
			local n = random(1, 3)
			createObject(blockTable, "xmasSlingshotAmmunition" .. n, name, rubberBandPos.x + cos(angle - pi) * bird.radius, rubberBandPos.y + sin(angle - pi) * bird.radius)
			local o = objects.world[name]
			if o ~= nil then
				local impulse = force * o.mass * (0.85 + 0.15 * random())
				local a = angle - spread * 0.5 + spread * i / C.ammunition
				applyImpulse(name, impulse * fraction * cos(a), impulse * fraction * sin(a), o.x, o.y)
				setFilterMask(name, 0)
				pending[#pending + 1] = { name = name, left = 0.2 }
			end
		end
	end

	local nativeShoot = shootSelectedBird
	shootSelectedBird = function(displacement, ...)
		if not C.active or selectedBird == nil then return nativeShoot(displacement, ...) end
		local bird = selectedBird
		local fraction = rubberBandLength / rubberBandMaximumLength()
		if fraction < 0 then fraction = 0 elseif fraction > 1 then fraction = 1 end
		if bird.jointToSlingshot then _G.pcall(destroyJoint, bird.jointToSlingshot) end
		remove("fakeBird")
		setVisible(bird.name, true)
		local results = { nativeShoot(displacement, ...) }
		shootAmmunition(bird, fraction)
		nextBirdTimer = C.reloadDelay
		return _G.unpack(results)
	end

	-- no slingshot, rubber band or holder while the tree is up (Friends' XmasTree has no slingshotFront / slingshotBack); its own stretch and shot sounds
	local hidden = { SLING_SHOT_01_BACK = true, SLING_SHOT_01_FRONT = true, SLING_HOLDER = true }
	local nativeDraw = _G.res.drawSprite
	_G.res.drawSprite = function(sprite, ...)
		if C.active and hidden[sprite] and currentGameMode == updateGame then return end
		return nativeDraw(sprite, ...)
	end
	local nativeLine = drawLine2D
	drawLine2D = function(x1, y1, x2, y2, w, r, g, b, ...)
		if C.active and r == 48 and g == 23 and b == 8 and currentGameMode == updateGame then return end
		return nativeLine(x1, y1, x2, y2, w, r, g, b, ...)
	end
	local nativePlay = _G.res.playAudio
	_G.res.playAudio = function(name, ...)
		if C.active then
			if name == "slingshot_stretched" and sounds.xmas_slingshot_rubber_band then name = "xmas_slingshot_rubber_band"
			elseif sounds.xmas_slingshot_release_shot and audioGroups.bird_shot ~= nil then
				for _, s in ipairs(audioGroups.bird_shot) do if s == name then name = "xmas_slingshot_release_shot" break end end
			end
		end
		return nativePlay(name, ...)
	end

	-- the tree goes up on the first frame of play: when "levelLoaded" comes the game has not yet worked out where the slingshot stands
	local waiting = false
	engine.on("levelLoaded", function()
		C.active, pending, waiting = false, {}, C.enabled
	end)

	engine.on("update", function(dt)
		if objects.world == nil then return end
		if waiting and currentGameMode == updateGame then
			waiting = false
			activate()
		end
		if keyPressed ~= nil and keyPressed[C.key] and currentGameMode == updateGame and not modalOpen then
			if C.active then deactivate() else activate() end
		end
		local k = 1
		while k <= #pending do
			local p = pending[k]
			p.left = p.left - dt
			if p.left <= 0 or not exists(p.name) then
				if exists(p.name) then setFilterMask(p.name, 16777215) end
				_G.table.remove(pending, k)
			else
				k = k + 1
			end
		end
		if not C.active then return end
		if not exists(tip) then C.active = false return end
		follow()
		if dragStarted and currentBirdName then setSleeping(currentBirdName, false) end
	end)
end
