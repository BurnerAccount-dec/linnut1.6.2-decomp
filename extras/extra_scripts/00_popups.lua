-- Part of extras/extra_scripts.lua (loaded by it, in the order of its list; delete this file or its line there to leave it out).
-- the pop-ups' look (4.1)
-- Every part shares the game's globals; the ones the pop-ups use (popupBox, popupButton) are made by 00_popups.lua, so keep that one when you use a part that shows a pop-up.

------------------------------------------------------------------ the pop-ups' look (4.1)
-- Every pop-up of this file (leaving a level, the level spawner, the Social levels) is a round-cornered panel in the green of the pause menu with its pale yellow rim,
-- and round buttons; drawRoundRect is the engine's native (an older exe without it gets square corners).
popupBox = function(x1, y1, x2, y2, radius)
	radius = radius or 24
	if _G.type(drawRoundRect) == "function" then
		drawRoundRect(x1 - 4, y1 - 4, x2 + 4, y2 + 4, radius + 4, 0, 0, 0, 0.22)                    -- a soft shadow
		drawRoundRect(x1 - 2, y1 - 2, x2 + 2, y2 + 2, radius + 2, 0.97, 0.95, 0.59, 1)               -- the pale yellow rim
		drawRoundRect(x1, y1, x2, y2, radius, 0.13, 0.42, 0.36, 1)                                    -- the pause menu's green
	else
		drawRect(0.13, 0.42, 0.36, 1, x1, y1, x2, y2, false)
		drawRectLines(x1, y1, x2, y2, 3, 248, 242, 151, 255)
	end
end
popupButton = function(x1, y1, x2, y2, r, g, b, selected)
	local radius = (y2 - y1) * 0.3
	if _G.type(drawRoundRect) == "function" then
		drawRoundRect(x1 - 2, y1 - 2, x2 + 2, y2 + 2, radius + 2, 1, 1, 1, selected and 1 or 0.85)
		drawRoundRect(x1, y1, x2, y2, radius, r, g, b, 1)
		drawRoundRect(x1 + 2, y1 + 2, x2 - 2, (y1 + y2) / 2, radius - 1, 1, 1, 1, 0.16)              -- the glossy upper half of the game's buttons
	else
		drawRect(r, g, b, 1, x1, y1, x2, y2, false)
		drawRectLines(x1, y1, x2, y2, 2, 255, 255, 255, 255)
	end
end
