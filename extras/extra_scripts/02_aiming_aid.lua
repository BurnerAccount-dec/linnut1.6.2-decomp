-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 2. aiming aid
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

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
