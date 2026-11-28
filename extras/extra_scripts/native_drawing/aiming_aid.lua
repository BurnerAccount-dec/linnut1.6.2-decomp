-- (4.5) The aiming aid's moving dots (Star Wars / Space style, and 02_aiming_aid.lua in Classic) drawn by Lua instead of the engine, with the engine's own rules
-- (GamePlatform::UpdateAimingAid and DrawAimingAidDots, the retail routines 0x48d750 and 0x45d570). Add "native_drawing/aiming_aid.lua" to the list in extra_scripts.lua.
-- While aimingAidDots.enabled is true the engine leaves its dots out (tuning["aim.skip"]) and aimingAidDots.draw runs in the "drawAim" event, at the same place.
--
-- The rules: the path is the engine's simulation of the bird (simulateAimingAid / updateBirdTrajectoryTable), a table of points in metres that
-- getAimingAidPoints() returns. While the aid is on, a new dot starts at the beginning of the path every spawnTime seconds (simulationAimSpawnTime, 0.6) and
-- every dot moves along the path by speed points a second (simulationAimSpeed, 4); a dot at x (in points along the table) is drawn at the Catmull-Rom spline
-- between points floor(x) + 1 and floor(x) + 2, scaled by 1 - x / (points - 3) + 0.2 times the sprite's scale (setAimingAidSprite), and it is gone at the
-- end of the path. The dots that are on the way when the aid goes off finish their run.

aimingAidDots = {
	enabled = true,
	dots = {},          -- { x = position along the table, s = scale }
	timer = 0,
}

do
	local floor, ipairs = _G.math.floor, _G.ipairs
	local function spline(pts, x)                      -- 0x45d380: Catmull-Rom between the points idx + 1 and idx + 2 (0-based idx)
		local n = #pts
		local idx = floor(x)
		if idx < 0 then idx = 0 end
		if idx > n - 4 then idx = n - 4 end
		local t = x - floor(x)
		local t2, t3 = t * t, t * t * t
		local p0, p1, p2, p3 = pts[idx + 1], pts[idx + 2], pts[idx + 3], pts[idx + 4]
		local function cr(a, b, c, d) return 0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3) end
		return cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y)
	end

	engine.on("update", function(dt)
		if not aimingAidDots.enabled then return end
		local pts, scale, sprite, speed, spawn, on = getAimingAidPoints()
		local len = #pts - 3
		local dots = aimingAidDots.dots
		local i = 1
		while i <= #dots do
			local d = dots[i]
			d.x = d.x + speed * dt
			d.s = 1 - d.x / len + 0.2
			if d.x >= len then _G.table.remove(dots, i) else i = i + 1 end
		end
		aimingAidDots.timer = aimingAidDots.timer - dt
		if not on or aimingAidDots.timer > 0 or len <= 0 then return end
		local guard = 0
		while aimingAidDots.timer <= 0 and guard < 8 do
			guard = guard + 1
			aimingAidDots.timer = aimingAidDots.timer + spawn
			dots[#dots + 1] = { x = 0, s = 1.2 }
		end
	end)

	aimingAidDots.draw = function()
		local pts, scale, sprite = getAimingAidPoints()
		if sprite == "" or #pts < 4 then return end
		local w, h = _G.res.getSpriteBounds("", sprite)
		local px, py = _G.res.getSpritePivot("", sprite)
		local camX, camY, worldScale = getCamera()
		setRenderState(-camX, -camY, worldScale, worldScale, 0, 0, 0, 1)
		for _, d in ipairs(aimingAidDots.dots) do
			local x, y = spline(pts, d.x)
			local ds = d.s * scale
			_G.res.drawSprite(sprite, x * 20 - px * ds, y * 20 - py * ds, "TOP", "LEFT", w * ds, h * ds)
		end
		setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
	end

	engine.on("drawGameBefore", function() tuning["aim.skip"] = aimingAidDots.enabled and 1 or 0 end)
	engine.on("drawAim", function() if aimingAidDots.enabled then aimingAidDots.draw() end end)
	engine.on("levelLoaded", function() aimingAidDots.dots, aimingAidDots.timer = {}, 0 end)
end
