-- Angry Birds Island HD (Hatchery) tile manager.
--
-- The iOS game draws the Hatchery with a native "TileManager" (createHatcheryMap, createNewTileObject,
-- screenToTileWorld ...). The iOS binary is the only place those natives exist, and its VFP arithmetic does not
-- disassemble reliably, so this is a re-implementation from the scripts' own use of the calls and from the map
-- data (a Tiled isometric map of 70x70 tiles, 196x97 pixels each). initializeTileManager(w, h, size) receives
-- size = (tilewidth - 3) * sin(45 deg) = 136.5: the side of one tile in world units. The view is the usual
-- 2:1 isometric projection of that square grid.
--
-- Run by the engine inside the game's environment, so `gamelua` is that environment.

local env = ...
local floor, sqrt, sin, cos, min, max, abs = math.floor, math.sqrt, math.sin, math.cos, math.min, math.max, math.abs
local C45 = 0.70710678118654752

local tm = nil            -- the tile manager state (nil until initializeTileManager)

local function screenSize()
    return env.screenWidth or 1024, env.screenHeight or 768
end

-- ---- drawing ------------------------------------------------------------------------------------------------
-- The engine's render state scales and moves everything by (position + translation) * scale, i.e. about the screen's
-- origin, not about the sprite. To draw a sprite with its pivot at (x, y), scaled and turned about that pivot, the state's
-- translation is (x / sx, y / sy) and its pivot is the sprite's own pivot (drawn at (0, 0)).
local function blit(name, x, y, sx, sy, angle, alpha)
    alpha = alpha or 1
    if sx == 0 or sy == 0 or alpha <= 0 then return end
    if sx == 1 and sy == 1 and angle == 0 and alpha == 1 then
        res.drawSprite("", name, x, y)
        return
    end
    local px, py = res.getSpritePivot("", name)
    env.setRenderState(x / sx, y / sy, sx, sy, angle, px, py, alpha)
    res.drawSprite("", name, 0, 0)
    env.setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
end

-- ---- projection ---------------------------------------------------------------------------------------------
-- world (square grid, units of `size` per tile) <-> unscaled projection <-> screen
local function project(wx, wy)
    return (wx - wy) * C45, (wx + wy) * C45 * 0.5
end

local function unproject(px, py)
    local a, b = px / C45, py / (C45 * 0.5)       -- a = wx - wy, b = wx + wy
    return (a + b) * 0.5, (b - a) * 0.5
end

local function worldToScreen(wx, wy)
    local sw, sh = screenSize()
    local px, py = project(wx, wy)
    local cx, cy = project(tm.camX, tm.camY)
    return sw * 0.5 + (px - cx) * tm.scale, sh * 0.5 + (py - cy) * tm.scale
end

local function screenToWorld(x, y)
    local sw, sh = screenSize()
    local cx, cy = project(tm.camX, tm.camY)
    return unproject((x - sw * 0.5) / tm.scale + cx, (y - sh * 0.5) / tm.scale + cy)
end

local function clampCamera()
    tm.camX = max(0, min(tm.w * tm.size, tm.camX))
    tm.camY = max(0, min(tm.h * tm.size, tm.camY))
end

-- ---- animations ---------------------------------------------------------------------------------------------
-- hatcheryAnimations[name] = { anchor = {x, y}, frames = { {time, x, y, scaleX, scaleY, rotation, easing}, ... } }
-- time is in seconds (the last frame's time is the length); x moves the sprite right, y UP in pixels (a jump is positive y); scale and rotation turn
-- about the anchor, a point of the sprite given as fractions of its size from the top left (it may lie outside the sprite).
-- easing: 1 = ease out (decelerates), -1 = ease in (accelerates), 0/nil = linear (applied to the segment that starts at that frame)
local animations = {}

local function ease(t, e)
    if e == 1 then return 1 - (1 - t) * (1 - t) end      -- eases out: fast at the start, slow at the end
    if e == -1 then return t * t end                     -- eases in
    return t
end

local function sampleAnimation(anim, t)
    local f = anim.frames
    local n = #f
    if n == 0 then return 0, 0, 1, 1, 0 end
    if t <= f[1].time then local a = f[1]; return a.x or 0, a.y or 0, a.scaleX or 1, a.scaleY or 1, a.rotation or 0 end
    for i = 1, n - 1 do
        local a, b = f[i], f[i + 1]
        if t <= b.time then
            local k = (t - a.time) / max(b.time - a.time, 1e-6)
            k = ease(k, a.easing)
            local function mix(p, q, d) p = p or d; q = q or d; return p + (q - p) * k end
            return mix(a.x, b.x, 0), mix(a.y, b.y, 0), mix(a.scaleX, b.scaleX, 1), mix(a.scaleY, b.scaleY, 1), mix(a.rotation, b.rotation, 0)
        end
    end
    local a = f[n]
    return a.x or 0, a.y or 0, a.scaleX or 1, a.scaleY or 1, a.rotation or 0
end

local function animationLength(anim)
    local f = anim.frames
    return (#f > 0 and f[#f].time) or 0
end

function env.createAnimation(id, def)
    animations[id] = def
end

-- ---- tile objects -------------------------------------------------------------------------------------------
-- an object: { id, sprites = { {name, ox, oy, sx, sy, angle} }, w, h (tiles), worldX, worldY, placed, selected,
--              anims = { {id, loop, speed, time} }, renderQueue, type, collision, selectable, children = { object } }
local objects = {}
local nextId = 1
local placed = {}       -- list of placed objects (not the ones inside a container)

local function snapPosition(o, wx, wy)
    local s = tm.size
    return (floor(wx / s) + 0.5 * (o.w or 1)) * s, (floor(wy / s) + 0.5 * (o.h or 1)) * s
end

-- the zero based tile indices an object centred at (wx, wy) covers
local function footprint(o, wx, wy)
    local s = tm.size
    local w, h = o.w or 1, o.h or 1
    local i0 = floor(wx / s - w * 0.5 + 0.5)
    local j0 = floor(wy / s - h * 0.5 + 0.5)
    return i0, j0, i0 + w - 1, j0 + h - 1
end

function env.initializeTileManager(w, h, size)
    tm = { w = w, h = h, size = size, scale = 1, camX = w * size * 0.5, camY = h * size * 0.5,
           base = {}, decorations = {}, defs = {} }
    objects, placed, nextId = {}, {}, 1
end

function env.deinitializeTileManager()
    tm = nil
    objects, placed = {}, {}
end

function env.createNewTileObject(params)
    local id = nextId
    nextId = nextId + 1
    local o = { id = id, sprites = {}, w = params.width or 1, h = params.height or 1, anims = {}, selected = false,
                renderQueue = params.renderQueue or 0, type = params.type, static = params.static,
                collision = params.collision, selectable = params.selectable, worldX = 0, worldY = 0, children = {} }
    for i, s in ipairs(params.sprites or {}) do o.sprites[i] = { name = s, ox = 0, oy = 0, sx = 1, sy = 1, angle = 0 } end
    objects[id] = o
    return id
end

local function leaveContainers(o)
    for _, p in pairs(objects) do
        for i = #p.children, 1, -1 do if p.children[i] == o then table.remove(p.children, i) end end
    end
end

function env.removeTileObjectFromLocation(id)
    local o = objects[id]
    if not o or not o.placed then return end
    o.placed = false
    for i = #placed, 1, -1 do if placed[i] == o then table.remove(placed, i) break end end
end

function env.removeTileObject(id)
    local o = objects[id]
    if not o then return end
    env.removeTileObjectFromLocation(id)
    leaveContainers(o)
    objects[id] = nil
end

function env.addTileObjectToLocation(id, wx, wy, snap)
    local o = objects[id]
    if not o or not tm then return end
    if snap == false then o.worldX, o.worldY = wx, wy else o.worldX, o.worldY = snapPosition(o, wx, wy) end
    if not o.placed then
        o.placed = true
        placed[#placed + 1] = o
    end
end

function env.setTileObjectSprites(id, sprites)
    local o = objects[id]
    if not o then return end
    o.sprites = {}
    for i, s in ipairs(sprites or {}) do o.sprites[i] = { name = s, ox = 0, oy = 0, sx = 1, sy = 1, angle = 0 } end
end

function env.setTileObjectType(id, t) if objects[id] then objects[id].type = t end end

function env.addSpriteToTileObject(id, name)
    local o = objects[id]
    if o then o.sprites[#o.sprites + 1] = { name = name, ox = 0, oy = 0, sx = 1, sy = 1, angle = 0 } end
end

-- sprite indices are zero based in the scripts
function env.removeSpriteFromTileObject(id, index)
    local o = objects[id]
    if o and o.sprites[index + 1] then table.remove(o.sprites, index + 1) end
end

function env.setSpriteParameters(id, index, ox, oy, sx, sy, angle)
    local o = objects[id]
    local s = o and o.sprites[index + 1]
    if s then s.ox, s.oy, s.sx, s.sy, s.angle = ox or 0, oy or 0, sx or 1, sy or 1, angle or 0 end
end

function env.addAnimationToTileObject(id, params)
    local o = objects[id]
    if o then o.anims[#o.anims + 1] = { id = params.id, loop = params.loop, speed = params.speed or 1, time = 0 } end
end

function env.removeAnimationFromTileObject(id, animId)
    local o = objects[id]
    if not o then return end
    for i = #o.anims, 1, -1 do if o.anims[i].id == animId then table.remove(o.anims, i) end end
end

function env.removeAllAnimationsFromTileObject(id)
    if objects[id] then objects[id].anims = {} end
end

function env.setObjectSelected(id, on)
    if objects[id] then objects[id].selected = on and true or false end
end

-- an egg or a bird in a nest is drawn with the nest, at its place (and does not stand on the map by itself)
function env.addObjectToContainerObject(childId, parentId)
    local child, parent = objects[childId], objects[parentId]
    if not child or not parent then return end
    env.removeTileObjectFromLocation(childId)
    leaveContainers(child)
    parent.children = { child }          -- a nest holds one thing: a bird takes the place of the egg it hatched from
end

-- ---- camera and coordinate conversion -------------------------------------------------------------------------
function env.setTileMapScale(s) if tm then tm.scale = max(0.1, min(4, s)) end end

function env.setTileCameraPosition(x, y)
    if not tm then return end
    tm.camX, tm.camY = x, y
    clampCamera()
end

function env.getTileCameraPosition()
    if not tm then return 0, 0 end
    return tm.camX, tm.camY
end

-- dragging by (dx, dy) screen pixels moves the camera the other way
function env.moveTileCameraPosition(dx, dy)
    if not tm then return end
    local sw, sh = screenSize()
    local ax, ay = screenToWorld(sw * 0.5 - dx, sh * 0.5 - dy)
    tm.camX, tm.camY = ax, ay
    clampCamera()
end

-- with snap (the third argument) the position becomes the centre of the tile it is in
function env.screenToTileWorld(x, y, snap)
    if not tm then return 0, 0 end
    local wx, wy = screenToWorld(x, y)
    if snap and snap ~= 0 and snap ~= false then
        local s = tm.size
        wx, wy = (floor(wx / s) + 0.5) * s, (floor(wy / s) + 0.5) * s
    end
    return wx, wy
end

function env.tileWorldToScreen(wx, wy) if not tm then return 0, 0 end return worldToScreen(wx, wy) end

-- centre of the tile with the given (zero based) indices
function env.getWorldCoordinatesForTileMapIndexes(ix, iy)
    return (floor(ix) + 0.5) * tm.size, (floor(iy) + 0.5) * tm.size
end

-- Tiled isometric object coordinates are in units of the tile height
function env.convertFromTILEDCoordinates(x, y)
    local th = tm.tileHeight or 97
    return x / th * tm.size, y / th * tm.size
end

-- ---- the map -----------------------------------------------------------------------------------------------
function env.createHatcheryMap(map, defs)
    tm.defs = defs
    tm.tileHeight = map.tileHeight
    tm.base = map.layers.Base and map.layers.Base.data or {}
    tm.decorations = {}
    tm.blocked = {}
    local deco = map.layers.Decoration and map.layers.Decoration.objects or {}
    for _, d in ipairs(deco) do
        local def = defs[tostring(d.gid)]
        local wx, wy = env.convertFromTILEDCoordinates(d.x, d.y)
        if def and def.sprites and def.sprites[1] then
            tm.decorations[#tm.decorations + 1] = { name = def.sprites[1], wx = wx, wy = wy, depth = wx + wy, queue = def.renderQueue or 0 }
            if def.collision == true then
                local ix, iy = floor(wx / tm.size), floor(wy / tm.size)
                tm.blocked[iy * tm.w + ix + 1] = true
            end
        end
    end
    -- the view starts over the land (the middle of the tiles that are not water or cliff)
    local sx, sy, n = 0, 0, 0
    for idx, gid in ipairs(tm.base) do
        local def = gid ~= 0 and defs[tostring(gid)]
        if def and def.collision ~= true then
            local i, j = (idx - 1) % tm.w, floor((idx - 1) / tm.w)
            sx, sy, n = sx + i, sy + j, n + 1
        end
    end
    if n > 0 then tm.camX, tm.camY = (sx / n + 0.5) * tm.size, (sy / n + 0.5) * tm.size end
end

-- the mouse wheel zooms the map about the cursor (the same limits as setTileMapScale)
local function wheelZoom()
    local c = env.cursor
    if not tm or not c or not c.wheelTriggered or not c.wheel or c.wheel == 0 then return end
    local sw, sh = screenSize()
    local wx, wy = screenToWorld(c.x, c.y)
    tm.scale = max(0.1, min(4, tm.scale * (c.wheel > 0 and 1.15 or 1 / 1.15)))
    local px, py = project(wx, wy)
    tm.camX, tm.camY = unproject(px - (c.x - sw * 0.5) / tm.scale, py - (c.y - sh * 0.5) / tm.scale)
    clampCamera()
end

function env.updateHatcheryMap(dt)
    wheelZoom()
    for _, o in pairs(objects) do
        for _, a in ipairs(o.anims) do
            local def = animations[a.id]
            local len = def and animationLength(def) or 0
            a.time = a.time + dt * a.speed
            if len > 0 then
                if a.loop then a.time = a.time % len elseif a.time > len then a.time = len end
            end
        end
    end
end

local function baseDef(ix, iy)
    if ix < 0 or iy < 0 or ix >= tm.w or iy >= tm.h then return nil end
    local gid = tm.base[iy * tm.w + ix + 1]
    if not gid or gid == 0 then return nil end
    return tm.defs[tostring(gid)], gid
end

function env.getStaticObjectID(wx, wy, layer)
    if not tm then return 0 end
    local ix, iy = floor(wx / tm.size), floor(wy / tm.size)
    if ix < 0 or iy < 0 or ix >= tm.w or iy >= tm.h then return 0 end
    return tm.base[iy * tm.w + ix + 1] or 0
end

local function inScreen(x, y, pad)
    local sw, sh = screenSize()
    return x > -pad and x < sw + pad and y > -pad and y < sh + pad
end

-- one object at a world position: every sprite is moved, scaled and turned by the object's animations, each about its own anchor
local function drawObject(o, wx, wy)
    local x, y = worldToScreen(wx, wy)
    if not inScreen(x, y, 500) then return end
    local k = tm.scale
    local count = #o.sprites
    for index, s in ipairs(o.sprites) do
        -- what the object holds (an egg, a bird) sits between the nest's back and its front rim, the last sprite
        if index == count and index > 1 and #o.children > 0 then
            for _, c in ipairs(o.children) do drawObject(c, wx, wy) end
        end
        local w, h = res.getSpriteBounds("", s.name)
        local px, py = res.getSpritePivot("", s.name)
        -- the sprite's own place: a point p of the sprite (from its pivot) goes to (tx, ty) + turn(angle) * scale * p
        local a, d, rot, tx, ty = s.sx, s.sy, s.angle or 0, s.ox, s.oy
        for _, an in ipairs(o.anims) do
            local def = animations[an.id]
            if def then
                local fx, fy, fsx, fsy, fr = sampleAnimation(def, an.time)
                local anchor = def.anchor
                local ax = (anchor and anchor[1] or 0.5) * w - px
                local ay = (anchor and anchor[2] or 0.5) * h - py
                local dx, dy = (tx - ax) * fsx, (ty - ay) * fsy
                local c, sn = cos(fr), sin(fr)
                tx = ax + c * dx - sn * dy + fx
                ty = ay + sn * dx + c * dy - fy
                a, d, rot = a * fsx, d * fsy, rot + fr
            end
        end
        blit(s.name, x + k * tx, y + k * ty, k * a, k * d, rot, 1)
    end
    if count <= 1 then for _, c in ipairs(o.children) do drawObject(c, wx, wy) end end
end

-- the tile art is drawn a little larger when the map is scaled, so no seams open between tiles
function env.drawHatcheryMap()
    if not tm then return end
    local sw, sh = screenSize()
    -- base tiles, back to front along the diagonal; only the visible window of the grid
    local x0, y0 = screenToWorld(-200, -200)
    local x1, y1 = screenToWorld(sw + 200, -200)
    local x2, y2 = screenToWorld(-200, sh + 200)
    local x3, y3 = screenToWorld(sw + 200, sh + 200)
    local s = tm.size
    local minI = max(0, floor(min(x0, x1, x2, x3) / s) - 1)
    local maxI = min(tm.w - 1, floor(max(x0, x1, x2, x3) / s) + 1)
    local minJ = max(0, floor(min(y0, y1, y2, y3) / s) - 1)
    local maxJ = min(tm.h - 1, floor(max(y0, y1, y2, y3) / s) + 1)
    local k = tm.scale
    local tileScale = (k == 1) and 1 or k * 1.012
    for sum = minI + minJ, maxI + maxJ do
        for i = max(minI, sum - maxJ), min(maxI, sum - minJ) do
            local j = sum - i
            local def = baseDef(i, j)
            local name = def and def.sprites and def.sprites[1]
            if name then
                local x, y = worldToScreen((i + 0.5) * s, (j + 0.5) * s)
                blit(name, x, y, tileScale, tileScale, 0, 1)
            end
        end
    end
    -- decorations and objects share one depth sort (the render queue first)
    local list = {}
    for _, d in ipairs(tm.decorations) do
        local x, y = worldToScreen(d.wx, d.wy)
        if inScreen(x, y, 400) then list[#list + 1] = { queue = d.queue, depth = d.depth, deco = d, x = x, y = y } end
    end
    for _, o in ipairs(placed) do
        list[#list + 1] = { queue = o.renderQueue or 0, depth = o.worldX + o.worldY, obj = o }
    end
    table.sort(list, function(a, b)
        if a.queue ~= b.queue then return a.queue < b.queue end
        return a.depth < b.depth
    end)
    for _, e in ipairs(list) do
        if e.deco then blit(e.deco.name, e.x, e.y, k, k, 0, 1)
        else drawObject(e.obj, e.obj.worldX, e.obj.worldY) end
    end
end

-- an object drawn once at a world position without being placed (the object being moved)
function env.drawTileObjectToWorld(id, wx, wy)
    local o = objects[id]
    if o and tm then drawObject(o, wx, wy) end
end

-- the tiles an object covers, drawn with an indicator sprite (green: it can be dropped here, red: it cannot)
function env.drawIndicatorForObject(id, wx, wy, sprite)
    local o = objects[id]
    if not o or not tm or not sprite then return end
    local s, k = tm.size, tm.scale
    local i0, j0, i1, j1 = footprint(o, wx, wy)
    for j = j0, j1 do
        for i = i0, i1 do
            local x, y = worldToScreen((i + 0.5) * s, (j + 0.5) * s)
            blit(sprite, x, y, k, k, 0, 1)
        end
    end
end

-- a screen-space sprite hung over a world location; ratioX/ratioY crop the sprite for progress bars
function env.drawHUDSpriteToTileWorldLocation(wx, wy, name, ratioX, ratioY)
    if not tm then return end
    local x, y = worldToScreen(wx, wy)
    ratioX = ratioX or 1
    ratioY = ratioY or 1
    if ratioX < 1 or ratioY < 1 then
        local w, h = res.getSpriteBounds("", name)
        local px, py = res.getSpritePivot("", name)
        res.setClipRect(x - px, y - py, w * ratioX, h * ratioY)
        res.drawSprite("", name, x, y)
        local sw, sh = screenSize()
        res.setClipRect(0, 0, sw, sh)
    else
        res.drawSprite("", name, x, y)
    end
end

-- ---- selection and placement ---------------------------------------------------------------------------------
-- the rectangle on the screen an object covers (all its sprites)
local function screenRect(o, wx, wy)
    local x, y = worldToScreen(wx, wy)
    local k = tm.scale
    local l, t, r, b
    for _, s in ipairs(o.sprites) do
        local w, h = res.getSpriteBounds("", s.name)
        local px, py = res.getSpritePivot("", s.name)
        if w and w > 0 then
            local left = x + k * (s.ox - px * s.sx)
            local top = y + k * (s.oy - py * s.sy)
            local right, bottom = left + k * w * s.sx, top + k * h * s.sy
            if left > right then left, right = right, left end
            if top > bottom then top, bottom = bottom, top end
            l, t, r, b = min(l or left, left), min(t or top, top), max(r or right, right), max(b or bottom, bottom)
        end
    end
    return l, t, r, b
end

local function hitTest(o, wx, wy, cx, cy)
    local l, t, r, b = screenRect(o, wx, wy)
    if not l then return false end
    -- a child (egg, bird) in a nest counts as part of it
    for _, c in ipairs(o.children) do
        local cl, ct, cr, cb = screenRect(c, wx, wy)
        if cl then l, t, r, b = min(l, cl), min(t, ct), max(r, cr), max(b, cb) end
    end
    return cx >= l and cx <= r and cy >= t and cy <= b
end

function env.doesCursorIntersectTileObject(id, wx, wy, cx, cy)
    local o = objects[id]
    if not o or not tm then return false end
    if not cx then local c = env.cursor; cx, cy = c and c.x or 0, c and c.y or 0 end
    return hitTest(o, wx or o.worldX, wy or o.worldY, cx, cy)
end

function env.selectObjectVisibleAtPixel(x, y)
    local best, bestQ, bestD
    for _, o in ipairs(placed) do
        if o.selectable == true and hitTest(o, o.worldX, o.worldY, x, y) then
            local q, d = o.renderQueue or 0, o.worldX + o.worldY
            if not best or q > bestQ or (q == bestQ and d > bestD) then best, bestQ, bestD = o, q, d end
        end
    end
    if not best then return -1, 0, 0 end
    return best.id, best.worldX, best.worldY
end

local function tileOf(wx, wy) return floor(wx / tm.size), floor(wy / tm.size) end

local function covers(o, i, j)
    local i0, j0, i1, j1 = footprint(o, o.worldX, o.worldY)
    return i >= i0 and i <= i1 and j >= j0 and j <= j1
end

function env.selectTileObjectAtLocation(wx, wy)
    local i, j = tileOf(wx, wy)
    local best, bestQ
    for _, o in ipairs(placed) do
        if o.selectable == true and covers(o, i, j) and (not best or (o.renderQueue or 0) >= bestQ) then best, bestQ = o, o.renderQueue or 0 end
    end
    return best and best.id or -1
end

-- the object of the given type whose area contains the location (an egg needs an empty nest below it)
function env.getObjectOfType(wx, wy, t)
    local i, j = tileOf(wx, wy)
    for _, o in ipairs(placed) do
        if o.type == t and covers(o, i, j) then return o.id end
    end
    return -1
end

-- the area the object would take at (x, y) is on the map, on ground that can be built on, and free of other solid objects
function env.isObjectAreaFree(id, x, y)
    local o = objects[id]
    if not o or not tm then return false end
    local wx, wy = snapPosition(o, x, y)
    local i0, j0, i1, j1 = footprint(o, wx, wy)
    for j = j0, j1 do
        for i = i0, i1 do
            if i < 0 or j < 0 or i >= tm.w or j >= tm.h then return false end
            local def = baseDef(i, j)
            if not def or def.collision == true then return false end
            if tm.blocked[j * tm.w + i + 1] then return false end
        end
    end
    for _, other in ipairs(placed) do
        if other ~= o and other.collision == true then
            local a0, b0, a1, b1 = footprint(other, other.worldX, other.worldY)
            if not (a1 < i0 or a0 > i1 or b1 < j0 or b0 > j1) then return false end
        end
    end
    return true
end

-- ---- composite birds ----------------------------------------------------------------------------------------
-- drawCompoObjectLua(x, y, angle, scale, items): items = { {sprite, x, y, scale, angle}, ... } laid out about (x, y) in screen space
function env.drawCompoObjectLua(x, y, angle, scale, items)
    angle, scale = angle or 0, scale or 1
    local c, s = cos(angle), sin(angle)
    for _, it in ipairs(items or {}) do
        local ix, iy = (it.x or 0) * scale, (it.y or 0) * scale
        local sc = scale * (it.scale or 1)
        blit(it.sprite, x + c * ix - s * iy, y + s * ix + c * iy, sc, sc, angle + (it.angle or 0), 1)
    end
end

-- ---- misc natives the Hatchery calls ---------------------------------------------------------------------------
-- (the egg painter canvas, loadPersistentFileWrapper and checkForPersistentFile are in the engine)
function env.drawMenuParticlesInAdvance() end
function env.startAnalyzingCanvasColors() end
function env.stopAnalyzingCanvasColors() end
function env.drawHatcheryBackground() end
