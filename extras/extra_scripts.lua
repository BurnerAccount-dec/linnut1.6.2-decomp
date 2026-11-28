-- Extra scripts for Angry Birds Classic 1.6.2 / 1.6.3.1, loaded by the engine when data/scripts/extra_scripts.lua exists next to gamelogic.lua
-- (or by hand: loadLuaFile(scriptPath .. "/extra_scripts.lua", "", false, false) at the end of gamelogic.lua).
-- (4.5) Each feature is a file of its own in the extra_scripts folder beside this file; this file only loads them, in the order of the list below.
-- Copy the folder together with this file. To leave a feature out, delete its line here (or its file); to add one of your own, put a .lua file in the
-- folder and its name in the list. The parts:
--   00_popups.lua             the look of the pop-ups (round panels in the green of the pause menu): needed by 07, 08 and 09
--   01_screen_wipe.lua        the Star Wars screen wipe between a menu and a level
--   02_aiming_aid.lua         the Star Wars aiming aid: while the bird is pulled back, the whole flight path is drawn as dots
--   03_laser_mod.lua          (3.3) the laser mod: a piercing laser and slow motion for the Big Brother Bird, a heat-wave GLSL shader on the background, an anti-gravity sensor zone
--   04_restart.lua            (3.3) restart: the restart button first moves every object back to where the level began, then restarts without the screen wipe
--   05_shape_blocks.lua       (3.4) blocks of any outline (star, wedge, chevron) and blocks with holes
--   07_leave_popup.lua        (3.6) leaving a level: a pop-up to leave with or without keeping the state of the level; the level's music stops in the menus
--   08_level_spawner.lua      (3.7) F1: another level's blocks, pigs and joints put into the level that is being played
--   09_social_levels.lua      (3.9) F12: the levels of a stand-in Angry Birds Social server
--   10_stella_abilities.lua   (4.1.2) Stella's abilities for the Classic birds: the ricochet, the scream, the drop, the dash and the teleport
--   11_christmas_slingshot.lua (4.5) F6: the Christmas tree slingshot of Angry Birds Friends 2.0.0 (its pictures and sounds are copied from the Friends apk, see the file)
--   native_drawing/*.lua      (4.5) not in the list: the engine's own drawing (backgrounds, foregrounds, the bird trail, the aiming aid dots) written in Lua, for mods that
--                             want to change it; see the top of each of those files
-- Loading it a second time does nothing.

if extraScripts ~= nil or _G.type(loadLevel) ~= "function" or _G.type(update) ~= "function" then
	return
end
extraScripts = true

extraScriptFiles = {
	"00_popups.lua",
	"01_screen_wipe.lua",
	"02_aiming_aid.lua",
	"03_laser_mod.lua",
	"04_restart.lua",
	"05_shape_blocks.lua",
	"07_leave_popup.lua",
	"08_level_spawner.lua",
	"09_social_levels.lua",
	"10_stella_abilities.lua",
	"11_christmas_slingshot.lua",
}

do
	-- the folder this file was loaded from (a file dropped on the exe is not in scriptPath)
	local folder = scriptPath
	local info = _G.debug and _G.debug.getinfo and _G.debug.getinfo(1, "S")
	if info and _G.type(info.source) == "string" and _G.string.sub(info.source, 1, 1) == "@" then
		folder = _G.string.match(_G.string.sub(info.source, 2), "^(.*)[/\\][^/\\]*$") or folder
	end
	local loaded, missing = {}, {}
	for _, name in _G.ipairs(extraScriptFiles) do
		local path = folder .. "/extra_scripts/" .. name
		local f = _G.io and _G.io.open and _G.io.open(path, "rb")
		if f or not (_G.io and _G.io.open) then
			if f then f:close() end
			local ok, err = _G.pcall(loadLuaFile, path, "", false, false)
			if ok then loaded[#loaded + 1] = _G.string.gsub(name, "%.lua$", "") else print("extra_scripts.lua: " .. name .. ": " .. _G.tostring(err)) end
		else
			missing[#missing + 1] = name
		end
	end
	print("extra_scripts.lua loaded (version 24: " .. _G.table.concat(loaded, ", ") .. ")" .. (#missing > 0 and (", not found: " .. _G.table.concat(missing, ", ")) or "")
		.. (_G.type(simulateAimingAid) == "function" and "" or "; this engine build is too old for the aiming aid, rebuild it"))
end
