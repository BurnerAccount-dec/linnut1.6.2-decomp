-- The level editor's joint window for the 1.6.x games that make the 1.6.0 joints (motor, limits, anchors) but ship no joint window
-- of their own (Angry Birds 1.6.2: Shift+J only made a plain distance joint). The window is the one of Angry Birds Seasons 2.3.0
-- (menus/item.lua, page.lua, overlayPage.lua and editorjointPage.lua, built into the engine, see lua_api.cpp). They run in a table
-- of their own, so Seasons' Page, TextItem and the other menu classes do not replace the game's; only EditorJointPage is handed over.
-- The engine runs this file among the game's globals when EditorJointPage is missing (script_host.cpp, EnsureClassicEditor).
local env = _G.getfenv(1)
-- while the files load, their classes stay in this table; afterwards a global the window sets (selectedObjects, levelSaved) is the game's
local loading = true
local own = _G.setmetatable({}, {__index = env, __newindex = function(t, k, v) if loading then _G.rawset(t, k, v) else env[k] = v end end})
own.this = own
-- `gamelua` for these files: Seasons' menu classes first (the window asks for gamelua.RectItem ...), then the game's globals
own.gamelua = _G.setmetatable({}, {__index = function(t, k) local v = _G.rawget(own, k) if v ~= nil then return v end return env[k] end, __newindex = env})
local function load(name) loadLuaFileToObject(scriptPath .. "/menus/" .. name, own, "") end
load("item.lua")
load("page.lua")
_G.rawset(own.gamelua, "page", {Page = own.Page})
load("editorjointPage.lua")
loading = false
EditorJointPage = own.EditorJointPage

-- The window's type button only knew the five joints of the 1.6.x editors and went back to "Distance" after "Destroy-attached".
-- It now goes on with the engine's own types (docs/SCRIPTING.md, "Extra joint types"): 6 pulley, 7 gear, 8 mouse, 9 line / wheel,
-- 10 friction, 11 rope, and only then back to distance. The window's code is compiled, so this wraps its entry points.
local Page = own.EditorJointPage
local extraTexts = { "Pulley Joint", "Gear Joint (of two joints)", "Mouse Joint (pull to a point)", "Line / Wheel Joint", "Friction Joint", "Rope Joint" }
local onEntry, update = Page.onEntry, Page.update
function Page.onEntry(self, ...)
	local r = onEntry(self, ...)
	if self.jointTexts then for i, t in _G.ipairs(extraTexts) do self.jointTexts[5 + i] = t end end
	return r
end
-- a gear couples one revolute / prismatic joint of each body: take the first one of each
local function findJoint(body, other)
	for name, j in _G.pairs(env.objects.joints) do
		if (j.type == 3 or j.type == 4) and (j.end1 == body or j.end2 == body) and name ~= other then return name end
	end
end
function Page.update(self, ...)
	local editor = env.editor
	local nj = editor and editor.newJoint
	local before = nj and nj.type
	if nj and nj.type == 7 then
		nj.joint1 = findJoint(nj.end1)
		nj.joint2 = nj.joint1 and findJoint(nj.end2, nj.joint1)
	end
	local ok, err = _G.pcall(update, self, ...)
	nj = editor and editor.newJoint
	if nj and before and nj ~= nil and nj.type ~= before then            -- the type button was pressed
		local t = nj.type
		if before == 5 and t == 1 then t = 6 elseif before >= 6 and t == 12 then t = 1 end
		nj.type = t
		if t == 7 then nj.joint1 = findJoint(nj.end1); nj.joint2 = nj.joint1 and findJoint(nj.end2, nj.joint1) end
		if self.items then
			self.items.type.text = self.jointTexts[t] or ""
			if t ~= 4 then self.items.x.visible = false; self.items.y.visible = false end
			self.items.collide.visible = (t ~= 5)
		end
	end
	if not ok then
		-- OK was pressed for a joint the engine could not make (a gear without two joints to couple): drop it instead of failing
		if editor and editor.newJoint then editor.newJoint = nil end
		if self.items and self.items.ok then self.items.ok.visible = false end
		_G.print("[joint window] " .. _G.tostring(err))
	end
end
