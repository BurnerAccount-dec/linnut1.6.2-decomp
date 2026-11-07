-- Level editor for Angry Birds Space. The retail exe has the editor's natives (blockEditorTable, saveLevel, createObject ...) but no editor scripts,
-- so the engine brings this one. It wraps the game's own update() and draw(): F2 switches it on while a level is running, F2 again plays on.
-- Run once into the script environment by script_host.cpp (EnsureSpaceEditor); nothing happens until F2 is pressed.
local env = ...
local G = env._G

local editor = {
    active = false, selected = nil, dragging = false, dragDX = 0, dragDY = 0,
    palette = nil, index = 1, counter = 0, message = "", messageTime = 0, wasPhysics = true, saved = false,
}
env.spaceEditor = editor

local function say(text) editor.message = text; editor.messageTime = 3 end

local function buildPalette()
    local list = {}
    local blocks = env.blockTable and env.blockTable.blocks
    if blocks then
        for name, def in G.pairs(blocks) do
            if G.type(def) == "table" and def.sprite and not name:find("MENU") and not name:find("^BLOCK_COMIC") then list[#list + 1] = name end
        end
    end
    table.sort(list)
    editor.palette = list
    if editor.index > #list then editor.index = 1 end
end

-- how far from the centre of an object a click still hits it (physics units)
local function reach(obj)
    local def = env.blockTable and env.blockTable.blocks[obj.definition]
    if not def then return 1.5 end
    local r = def.radius or math.max(def.width or 0, def.height or 0) / 2
    if not r or r <= 0 then r = 1.5 end
    return r
end

local function pick(px, py)
    local best, bestR = nil, nil
    for name, obj in G.pairs(env.objects.world) do
        if G.type(obj) == "table" and obj.x and obj.y and obj.definition then
            local r = reach(obj)
            local d = math.sqrt((obj.x - px) ^ 2 + (obj.y - py) ^ 2)
            if d <= r + 1 and (bestR == nil or r < bestR) then best, bestR = name, r end
        end
    end
    return best
end

local function setActive(on)
    editor.active = on
    if on then
        editor.wasPhysics = env.isPhysicsEnabled and env.isPhysicsEnabled() or true
        env.setPhysicsEnabled(false)
        if not editor.palette then buildPalette() end
        say("Editor on: left drag moves, N places, Q/E turn, Delete removes, Ctrl+S saves")
    else
        env.setPhysicsEnabled(editor.wasPhysics ~= false)
        editor.selected, editor.dragging = nil, false
    end
end

local function editorUpdate(dt)
    local kp, kh = env.keyPressed, env.keyHold
    local px, py = env.screenToPhysicsTransform(env.cursor.x, env.cursor.y)
    if editor.messageTime > 0 then editor.messageTime = editor.messageTime - dt end

    -- camera: arrow keys pan, the wheel zooms
    local pan = 600 * dt
    local sx, sy = env.screen.x or 0, env.screen.y or 0
    local dx = (kh.RIGHT and pan or 0) - (kh.LEFT and pan or 0)
    local dy = (kh.DOWN and pan or 0) - (kh.UP and pan or 0)
    if dx ~= 0 or dy ~= 0 then env.setTopLeft(sx + dx, sy + dy) end
    if env.cursor.wheel and env.cursor.wheel ~= 0 and not kh.SHIFT then
        local ws = (env.worldScale or 1) * (env.cursor.wheel > 0 and 1.1 or 1 / 1.1)
        env.worldScale = ws
        env.setWorldScale(ws)
    end

    -- the palette: Shift + wheel, or PageUp / PageDown
    if (env.cursor.wheel and env.cursor.wheel ~= 0 and kh.SHIFT) or kp.PAGEUP or kp.PAGEDOWN then
        local step = (kp.PAGEDOWN or (env.cursor.wheel or 0) < 0) and 1 or -1
        editor.index = (editor.index - 1 + step) % #editor.palette + 1
        say("Block: " .. editor.palette[editor.index])
    end

    -- selection and dragging
    if kp.LBUTTON then
        editor.selected = pick(px, py)
        editor.dragging = editor.selected ~= nil
        if editor.selected then
            local o = env.objects.world[editor.selected]
            editor.dragDX, editor.dragDY = o.x - px, o.y - py
        end
    end
    if editor.dragging and kh.LBUTTON and editor.selected and env.objects.world[editor.selected] then
        env.setPosition(editor.selected, px + editor.dragDX, py + editor.dragDY)
        editor.saved = false
    elseif not kh.LBUTTON then
        editor.dragging = false
    end

    local sel = editor.selected and env.objects.world[editor.selected]
    if sel and (kp.Q or kp.E) then
        local a = (sel.angle or 0) + (kp.E and 1 or -1) * (kh.SHIFT and 0.0174533 or 0.0872665)
        env.setRotation(editor.selected, a)
        editor.saved = false
    end
    if sel and (kp.DELETE or kp.BACKSPACE) then
        env.destroyObject(editor.selected, true)
        editor.selected, editor.dragging = nil, false
        editor.saved = false
    end

    -- N places the chosen block at the cursor, D copies the selected one
    if kp.N or (kp.D and sel) then
        local def = kp.N and editor.palette[editor.index] or sel.definition
        editor.counter = editor.counter + 1
        local name = def .. "_ed" .. editor.counter
        while env.objects.world[name] do editor.counter = editor.counter + 1; name = def .. "_ed" .. editor.counter end
        local ok = env.createObject(env.blockTable, def, name, px, py, kp.D and sel.angle or 0)
        if ok ~= nil or env.objects.world[name] then editor.selected = name; editor.saved = false; say("Placed " .. name) end
    end

    if kh.CONTROL and kp.S then
        local path = (env.levelFolder or "levels/") .. (env.levelName or "Level")
        env.saveLevel(path)
        editor.saved = true
        say("Saved " .. path)
    end
    if kp.F2 then setActive(false) end
end

local function wrap()
    local gameUpdate, gameDraw = env.update, env.draw
    if G.type(gameUpdate) ~= "function" or G.type(gameDraw) ~= "function" or editor.wrapped then return end
    editor.wrapped = true
    env.update = function(dt, time)
        if editor.active then
            editorUpdate(dt)
            return
        end
        if env.keyPressed.F2 and env.objects and env.objects.world and env.blockTable and env.levelName and env.levelName ~= "MainMenu" then
            setActive(true)
            return
        end
        return gameUpdate(dt, time)
    end
    env.draw = function()
        gameDraw()
        if not editor.active then return end
        env.setFont(env.FONT_BASIC)
        env.setRenderState(0, 0, 1, 1, 0)
        local sel = editor.selected and env.objects.world[editor.selected]
        if sel then
            local sxp, syp = env.physicsToScreenTransform(sel.x, sel.y)
            local r = reach(sel) * 20 * (env.worldScale or 1)
            if r < 12 then r = 12 end
            env.drawRectLines(sxp - r, syp - r, sxp + r, syp + r, 2, 255, 220, 0, 255)
            env.res.drawString("", editor.selected, sxp, syp - r - 4, "BOTTOM", "HCENTER")
        end
        local def = editor.palette and editor.palette[editor.index] or "?"
        env.res.drawString("", "EDITOR  block: " .. def .. (editor.saved and "" or "  (unsaved)"), 8, 8, "TOP", "LEFT")
        if editor.messageTime > 0 then env.res.drawString("", editor.message, 8, 34, "TOP", "LEFT") end
    end
end
editor.install = wrap
wrap()
