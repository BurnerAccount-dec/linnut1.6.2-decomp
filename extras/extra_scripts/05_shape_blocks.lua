-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- 5. shape blocks (3.4): a star and a wedge in the wood group; with holes (3.6)
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

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
