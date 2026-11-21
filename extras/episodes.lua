-- episodes.lua for Angry Birds Classic 1.6.2 / 1.6.3.1 (the menu from before 2.0.0, where the episodes are written out in gamelogic.lua).
-- Put this file next to gamelogic.lua (data/scripts/). It is read once at start; leave out whatever should stay as it is.
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
-- episodes = { style = "popup" }
