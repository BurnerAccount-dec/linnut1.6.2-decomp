-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 4. restart (3.3)
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

if screenwipe == nil then screenwipe = { skip = false, skipTime = 0 } end    -- (4.5) without 01_screen_wipe.lua: the restart still works, there is just no wipe to skip

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
