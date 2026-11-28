-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 8. level spawner (3.7, folders and preview 3.8, placing 3.8.1)
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

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
