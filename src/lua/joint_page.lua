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
