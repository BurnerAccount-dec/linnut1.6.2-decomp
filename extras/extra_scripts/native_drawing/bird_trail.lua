-- (4.5) The bird trail (the white dots behind a flying bird and the puffs where it hit something or used its ability) drawn by Lua instead of the engine,
-- with the engine's own rules (the drawTrajectory part of drawGameNative). Add "native_drawing/bird_trail.lua" to the list in extra_scripts.lua.
-- While birdTrail.enabled is true the engine leaves its dots out (tuning["trajectory.skip"]) and birdTrail.draw runs in the "drawTrail" event, which comes
-- at the same place: after the blocks, before the birds, in the world's render state.
--
-- How the engine keeps the trail, done here the same way: the game's scripts call startNewTrajectory() when a bird is shot, then addToTrajectory(i, x, y)
-- and addPuffToTrajectory(i, x, y) (i is 1 to 3: the bird and the two extra Blue Birds). There are two sets of three lists; each new trajectory takes the other
-- set and clears it, so the last two shots stay on the screen. The dots of a list go through TRAIL_WHITE_1, _2 and _3 and start again; a puff is BIRD_SPECIAL.
-- setCustomTrajectorySprites(dot1, dot2, dot3, puff) changes the sprites of the trajectory that is being drawn. Positions are world pixels.
-- The three functions are wrapped (they still reach the engine, so save states keep the trail), and birdTrail.sets holds what was recorded.

birdTrail = {
	enabled = true,
	sets = { { {}, {}, {} }, { {}, {}, {} } },        -- sets[1 or 2][list 1..3] = { {x=, y=, puff=true or nil}, ... }
	sprites = { {}, {} },                              -- the custom sprites of each set (setCustomTrajectorySprites): [1..3] dots, [4] puff
	current = 1,
}

do
	local type, ipairs = _G.type, _G.ipairs
	local nativeStart, nativeAdd, nativePuff, nativeSprites = startNewTrajectory, addToTrajectory, addPuffToTrajectory, setCustomTrajectorySprites
	startNewTrajectory = function(...)
		birdTrail.current = birdTrail.current == 1 and 2 or 1
		birdTrail.sets[birdTrail.current] = { {}, {}, {} }
		birdTrail.sprites[birdTrail.current] = {}
		if nativeStart then return nativeStart(...) end
	end
	addToTrajectory = function(i, x, y, ...)
		local list = birdTrail.sets[birdTrail.current][_G.math.floor(i)]
		if list then list[#list + 1] = { x = x, y = y } end
		if nativeAdd then return nativeAdd(i, x, y, ...) end
	end
	addPuffToTrajectory = function(i, x, y, ...)
		local list = birdTrail.sets[birdTrail.current][_G.math.floor(i)]
		if list then list[#list + 1] = { x = x, y = y, puff = true } end
		if nativePuff then return nativePuff(i, x, y, ...) end
	end
	setCustomTrajectorySprites = function(a, b, c, d, ...)
		birdTrail.sprites[birdTrail.current] = { a, b, c, d }
		if nativeSprites then return nativeSprites(a, b, c, d, ...) end
	end
	engine.on("levelLoaded", function()
		birdTrail.sets = { { {}, {}, {} }, { {}, {}, {} } }
		birdTrail.sprites = { {}, {} }
	end)

	local function exists(sprite) return sprite ~= nil and sprite ~= "" and (type(spriteExists) ~= "function" or spriteExists(sprite)) end
	-- the engine draws set 1's three lists, then set 2's; in each list the dots first, then the puffs
	birdTrail.draw = function()
		local camX, camY, worldScale = getCamera()
		setRenderState(-camX, -camY, worldScale, worldScale, 0, 0, 0, 1)
		for s = 1, 2 do
			local custom = birdTrail.sprites[s]
			for i = 1, 3 do
				local n = 0
				for _, p in ipairs(birdTrail.sets[s][i]) do
					if not p.puff then
						local k = n % 3 + 1
						local sprite = exists(custom[k]) and custom[k] or "TRAIL_WHITE_" .. k
						if not exists(sprite) then sprite = "SMOKE_TRAIL_" .. k end          -- Angry Birds 1.0 (iPhone)
						if exists(sprite) then _G.res.drawSprite(sprite, p.x, p.y) end
						n = n + 1
					end
				end
				for _, p in ipairs(birdTrail.sets[s][i]) do
					if p.puff then
						local sprite = exists(custom[4]) and custom[4] or "BIRD_SPECIAL"
						if exists(sprite) then _G.res.drawSprite(sprite, p.x, p.y) end
					end
				end
			end
		end
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
	end

	engine.on("drawGameBefore", function() tuning["trajectory.skip"] = birdTrail.enabled and 1 or 0 end)
	engine.on("drawTrail", function() if birdTrail.enabled then birdTrail.draw() end end)
end
