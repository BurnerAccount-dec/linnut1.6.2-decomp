-- ENGINE-EPISODES-FILE (the engine looks for this line: a game's own episodes.lua, as in 2.0.0 and later, is left alone)
-- episodes.lua for the menus from before 2.0.0, where the episodes are written out in gamelogic.lua: Angry Birds Classic 1.6.2 / 1.6.3.1 and Seasons 2.3.0 and
-- before (also the Android and iPhone builds with that menu). Put this file next to gamelogic.lua (data/scripts/); for an .apk or .ipa, next to the .lua file that
-- is dragged onto the engine with the game. It is read once at start; leave out whatever should stay as it is. Everything the episode system does is in this one
-- file: the table you edit is at the top, and below the line "the rest of this file" is the code that makes the menu follow it (nothing to change there).
--
-- The old menu has six episode pages, and each page keeps the worlds of one episode:
--   episode 1: worlds 1-3 (pack1-pack3, 21 levels each)    episode 2: worlds 4-5 (pack4-pack5, 21 levels each)
--   episode 3: worlds 6-8      episode 4: worlds 9-11      episode 5: worlds 12-14 (21 levels each)      episode 6: worlds 15-17 (15 levels each)
--
-- episodes[n].worlds   the levels of the worlds of episode n, in order. "folder" is the folder under data/levels/ the levels are read from.
--                      A world may hold fewer levels than the page has places: the rest stay empty.
-- episodes[n].tile     the art of the tile on the episode screen: 1 to 6 (the picture of that stock episode, with its name)
-- episodes.order       the order of the tiles on the episode screen, as the numbers of the episodes: { 2, 1, 3, 4, 5, 6 } puts episode 2 first.
--
-- Seasons (2.3.0 and before, the menu with the two seasons): the same file, with the levels of each pack by the pack's name. The packs are
--   pack1 Christmas 2010 (Xmas)    pack2 Valentine's    pack3 St. Patrick's    pack4 Easter    pack5 Summer    pack6, pack7 Mooncake
--   pack8, pack9 Halloween 2011    pack10 Christmas 2011    pack11 Year of the Dragon    pack12, pack13, pack14 Halloween 2010    pack15 Cherry Blossom
-- episodes.packs   { pack2 = { "LevelX_39", "LevelV_3" } } makes pack2 hold those levels (fewer or more than before; the level files stay in that pack's folder)
--
-- This copy changes nothing: every line is a comment.
episodes = {
	-- packs = { pack2 = { "LevelX_39", "LevelV_3", "LevelX_44" } },      -- Seasons only

	-- order = { 1, 2, 3, 4, 5, 6 },

	-- [1] = {
	--	tile = 1,
	--	worlds = {
	--		{ folder = "pack1", levels = { "Level1", "Level57", "Level53" } },     -- a world of three levels
	--		{ folder = "pack2", levels = { "Level52", "Level34" } },
	--	},
	-- },
}

-- style = "popup"  (Angry Birds 1.6.2 / 1.6.3.1) draws the episode screen with pop-up boxes (names as text, four episodes and the Golden Eggs, the way the mobile
-- versions draw it) instead of picture tiles. The boxes grow a little under the cursor. "tile" and "order" only apply to the picture tiles. Leave it out for the tiles.
-- In pop-up style each episode can also have a name, the title of its box:  episodes[2] = { name = "2. My Pack" }
-- episodes = { style = "popup" }

------------------------------------------------------------------ the rest of this file: makes the menu follow the table above
if _G.type(episodes) == "table" and _G.type(createMenuPages) == "function" then
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
		if _G.type(episodes.packs) == "table" and _G.type(levelOrder) == "table" then   -- Seasons: the levels of a pack by its name
			for name, levels in _G.pairs(episodes.packs) do
				local list = levelOrder[name]
				if _G.type(list) == "table" then
					for i = #list, 1, -1 do list[i] = nil end
					for i = 1, #levels do list[i] = levels[i] end
				end
			end
		end
		for s = 1, (_G.type(levelOrder) == "table" and _G.type(levelOrder.packs) == "table") and 6 or 0 do
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
				if title then title.text = episodes[n].name if title.lines ~= nil then title.lines = { episodes[n].name } end end   -- the drawn text is in lines
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
