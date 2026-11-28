-- (4.5) The theme's background and foreground (drawBackgroundNative / drawForegroundNative) drawn by Lua instead of the engine, with the engine's own rules
-- (GamePlatform::DrawBackground and DrawForeground), so a mod can change any of it. Add "native_drawing/backgrounds.lua" to the list in extra_scripts.lua.
-- While nativeBackground.enabled is true the engine's drawing is switched off (tuning["background.skip"] / ["foreground.skip"]) and these functions draw in
-- the "drawBackground" / "drawForeground" events, at the same place in the frame. Set it to false (lua nativeBackground.enabled = false) to give it back.
--
-- What the engine does, and what is done here the same way:
--   background: for each layer of the theme, back to front (getThemeLayers()), the camera's x times the layer's speed is the shift; a looping layer repeats its
--     sprite from the left edge of the screen to the right, a fixed layer (no looping) is drawn once; everything at the layer's scale times the world scale,
--     the layer's dy moving it up or down by screen pixels and dx sideways.
--   foreground: the theme's ground colour from the foot of the first layer down to the bottom of the screen, then each layer repeated the same way at its scale.
-- Not done here (the engine keeps drawing those itself when a theme has them, see `special` below): the rectangle and tinted layers of Angry Birds 4.0, Space's
-- screen layers, the named foreground layers of Star Wars and the water of Piglantis, and the animated theme sprites some Seasons themes add to a layer.
-- Per-repeat changes made with setBackgroundSprite / setForegroundSprite do not apply here: change the functions below instead.

nativeBackground = {
	enabled = true,
	background = true,     -- false: the engine draws the background
	foreground = true,     -- false: the engine draws the foreground
}

do
	local type, ipairs, floor, fmod = _G.type, _G.ipairs, _G.math.floor, _G.math.fmod
	local function trunc(v) if v >= 0 then return floor(v) else return -floor(-v) end end

	-- the theme that is set (setTheme is wrapped to remember its name, for the theme's ground colour)
	local themeName = nil
	if type(setTheme) == "function" then
		local setThemeNative = setTheme
		setTheme = function(name, ...)
			themeName = name
			return setThemeNative(name, ...)
		end
	end

	-- the size and pivot of a sprite or composite sprite: width, height, pivot x, pivot y (nil when there is no such picture)
	local function extent(sprite)
		local x1, y1, x2, y2 = _G.res.getCompoSpriteBounds("", sprite)
		if x1 == nil or (x2 - x1) <= 0 then return nil end
		return x2 - x1, y2 - y1, -x1, -y1
	end

	local function anySpecial(layers)
		for _, l in ipairs(layers) do if l.special and not l.hidden then return true end end
		return false
	end

	nativeBackground.drawBackground = function()
		local camX, camY, worldScale, screenW, screenH = getCamera()
		-- the cranes of Classic's fourth episode: the fixed layers follow one another, the first at episode4BGCranes.startX / 16 sprite widths
		local startX = type(episode4BGCranes) == "table" and episode4BGCranes.startX or 0
		local place, firstFixed = 0, true
		for _, layer in ipairs(getThemeLayers(0)) do
			local w, h, pivotX = extent(layer.sprite)
			if not layer.hidden and w ~= nil then
				local s = layer.scale
				local inv = 1 / s
				local shift = layer.speed * (camX + layer.offset)
				local ty = -camY * inv + layer.dy / (s * worldScale)
				local sx = s * worldScale
				if layer.looping then
					local m = fmod(shift, w * s)
					local x0 = -(m + w * s) * inv
					local tx = x0 - trunc(x0) + layer.dx / (s * worldScale)
					local right = screenW * inv / worldScale + pivotX
					setRenderState(tx, ty, sx, sx, 0, 0, 0, 1)
					local x = trunc(x0)
					while x <= right do
						_G.res.drawSprite(layer.sprite, x, 0)
						x = x + w
					end
				else
					if layer.fixedX then
						place = place + (firstFixed and startX * 0.0625 * w or w * 0.5)
						firstFixed = false
					else
						place = w * 0.5
					end
					local tx = place - trunc(place) + layer.dx / (s * worldScale)
					setRenderState(tx, ty, sx, sx, 0, 0, 0, 1)
					_G.res.drawSprite(layer.sprite, place - shift * inv, 0)
				end
			end
		end
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
	end

	nativeBackground.drawForeground = function()
		local camX, camY, worldScale, screenW, screenH = getCamera()
		local layers = getThemeLayers(2)
		-- the ground colour, from the foot of the first layer (scaled back by the 1.5 the engine uses) to the bottom of the screen
		local height = 0
		if layers[1] ~= nil then
			local w, h, px, py = extent(layers[1].sprite)
			if w ~= nil then height = (h - py) * layers[1].scale / 1.5 end
		end
		local theme = themeName and type(blockTable) == "table" and type(blockTable.themes) == "table" and blockTable.themes[themeName] or nil
		local gc = theme and type(theme.groundColor) == "table" and theme.groundColor or { r = 0, g = 0, b = 0 }
		drawRect((gc.r or 0) / 255, (gc.g or 0) / 255, (gc.b or 0) / 255, 1, 0, -(camY - height * 1.5) * worldScale, screenW, screenH + 1, false)
		for _, layer in ipairs(layers) do
			local w, h, px = extent(layer.sprite)
			if not layer.hidden and w ~= nil then
				local c = layer.scale
				local shift = layer.speed * (camX + layer.offset)
				local m = fmod(shift, c * w)
				local x0 = -(m + c * w) / c
				local tx = x0 - trunc(x0) + layer.dx / (worldScale * c)
				-- offY: the layer's own offsetY, posY: what setThemeForegroundOffsetY added (Classic 3.x)
				local ty = -camY / c + layer.offY + layer.posY / c + layer.dy / (worldScale * c)
				setRenderState(tx, ty, worldScale * c, worldScale * c, 0, 0, 0, 1)
				local right = screenW / (worldScale * c) + px
				local x = trunc(x0)
				while x <= right do
					_G.res.drawSprite(layer.sprite, x, 0)
					x = x + w
				end
			end
		end
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
	end

	-- the switch: every frame, so that a theme the engine draws its own way (special layers) is left to the engine
	local function on(which)
		local layers = getThemeLayers(which == "background" and 0 or 2)
		return nativeBackground.enabled and nativeBackground[which] and not anySpecial(layers)
	end
	engine.on("drawBackgroundBefore", function()
		tuning["background.skip"] = on("background") and 1 or 0
	end)
	engine.on("drawBackground", function()
		if tuning["background.skip"] == 1 then nativeBackground.drawBackground() end
	end)
	engine.on("drawForegroundBefore", function()
		tuning["foreground.skip"] = on("foreground") and 1 or 0
	end)
	engine.on("drawForeground", function()
		if tuning["foreground.skip"] == 1 then nativeBackground.drawForeground() end
	end)
end
