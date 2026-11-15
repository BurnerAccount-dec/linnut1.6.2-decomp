-- The `motion` library (3.3): move physics objects any way you like, and play the rewind history back any way you like.
-- Run once into the script environment by script_host.cpp, before any game script. Nothing happens until a script calls it.
-- Everything is plain Lua on top of setPosition, setRotation, setVelocity and the history natives getRewindDepth / getRewindState, so it can be read, copied and changed.
-- Coordinates are physics units, the ones objects.world[name].x / .y and setPosition use; angles are radians.
local env = ...
local G = env._G
local pairs, ipairs, type, tostring = G.pairs, G.ipairs, G.type, G.tostring
local sin, cos, sqrt, pow, abs, floor, min, max, pi = G.math.sin, G.math.cos, G.math.sqrt, G.math.pow or function(a, b) return a ^ b end, G.math.abs, G.math.floor, G.math.min, G.math.max, G.math.pi

local motion = { jobs = {}, nextId = 1 }
env.motion = motion

------------------------------------------------------------------ easing functions: t from 0 to 1 in, progress out (it may leave 0..1: back, elastic)
local E = {}
motion.easing = E
E.linear = function(t) return t end

local function bounceOut(t)
	local n, d = 7.5625, 2.75
	if t < 1 / d then return n * t * t
	elseif t < 2 / d then t = t - 1.5 / d return n * t * t + 0.75
	elseif t < 2.5 / d then t = t - 2.25 / d return n * t * t + 0.9375
	else t = t - 2.625 / d return n * t * t + 0.984375 end
end
local c1, c4 = 1.70158, 2 * pi / 3
local ins = {   -- the "in" form of each family
	Quad = function(t) return t * t end,
	Cubic = function(t) return t * t * t end,
	Quart = function(t) return t * t * t * t end,
	Quint = function(t) return t * t * t * t * t end,
	Sine = function(t) return 1 - cos(t * pi / 2) end,
	Expo = function(t) return t == 0 and 0 or pow(2, 10 * t - 10) end,
	Circ = function(t) return 1 - sqrt(1 - t * t) end,
	Back = function(t) return (c1 + 1) * t * t * t - c1 * t * t end,
}
local outs = {   -- families that are defined by their "out" form
	Elastic = function(t) if t == 0 or t == 1 then return t end return pow(2, -10 * t) * sin((t * 10 - 0.75) * c4) + 1 end,
	Bounce = bounceOut,
}
for name, f in pairs(ins) do
	E["in" .. name] = f
	E["out" .. name] = function(t) return 1 - f(1 - t) end
end
for name, f in pairs(outs) do
	E["out" .. name] = f
	E["in" .. name] = function(t) return 1 - f(1 - t) end
end
local families = {}
for name in pairs(ins) do families[#families + 1] = name end
for name in pairs(outs) do families[#families + 1] = name end
for _, name in ipairs(families) do
	local i, o = E["in" .. name], E["out" .. name]
	E["inOut" .. name] = function(t) if t < 0.5 then return i(t * 2) / 2 end return 0.5 + o(t * 2 - 1) / 2 end
end

-- E.steps(n): n flat steps. E.bezier(x1, y1, x2, y2): the CSS cubic-bezier(). E.spring(stiffness, damping): a damped spring that settles at 1.
E.steps = function(n) return function(t) return floor(t * n) / n end end
E.bezier = function(x1, y1, x2, y2)
	local bx = 3 * x1; local cx = 3 * (x2 - x1) - bx; local ax = 1 - bx - cx
	local by = 3 * y1; local cy = 3 * (y2 - y1) - by; local ay = 1 - by - cy
	return function(t)
		if t <= 0 then return 0 elseif t >= 1 then return 1 end
		local s = t
		for _ = 1, 8 do   -- Newton: the curve parameter whose x is t
			local d = (3 * ax * s + 2 * cx) * s + bx
			if abs(d) < 1e-6 then break end
			s = s - (((ax * s + cx) * s + bx) * s - t) / d
		end
		return ((ay * s + cy) * s + by) * s
	end
end
E.spring = function(stiffness, damping)
	stiffness, damping = stiffness or 12, damping or 0.5
	return function(t) return 1 - cos(t * stiffness) * pow(2.718281828, -t * damping * stiffness) end
end

-- an easing can be a name ("outBounce"), a function(t), a table {x1, y1, x2, y2} (a bezier), or nil (linear)
local function easeOf(spec)
	if spec == nil then return E.linear end
	if type(spec) == "function" then return spec end
	if type(spec) == "string" then
		local f = E[spec]
		if f == nil then G.print("motion: no easing named " .. spec .. ", using linear") return E.linear end
		return f
	end
	if type(spec) == "table" and spec[4] ~= nil then return E.bezier(spec[1], spec[2], spec[3], spec[4]) end
	return E.linear
end
motion.ease = easeOf

------------------------------------------------------------------ putting an object somewhere
local function lerp(a, b, p) return a + (b - a) * p end

local function currentOf(name)
	local o = env.objects and env.objects.world and env.objects.world[name]
	if o == nil then return nil end
	return o.x, o.y, o.angle or 0
end

-- places an object: any of x, y, angle can be nil (kept). Its speed is set to vx, vy (0 when left out) so physics does not fight the move.
local function place(name, x, y, a, vx, vy)
	local cx, cy, ca = currentOf(name)
	if cx == nil then return false end
	if x ~= nil or y ~= nil then env.setPosition(name, x or cx, y or cy) end
	if a ~= nil then env.setRotation(name, a) end
	env.setVelocity(name, vx or 0, vy or 0)
	return true
end

-- motion.teleport(name, x, y [, angle [, keepSpeed]]): an instant move. Speed is kept when keepSpeed is true, else the object stops.
function motion.teleport(name, x, y, angle, keepSpeed)
	local cx, cy, ca = currentOf(name)
	if cx == nil then return false end
	local speed, vx, vy = 0, 0, 0
	if keepSpeed and env.getVelocity then speed, vx, vy = env.getVelocity(name) end
	return place(name, x, y, angle, vx, vy)
end

------------------------------------------------------------------ jobs: everything that moves is a job, and all jobs run from one update handler
-- release: what happens to the speed at the end: "stop" (default), "keep" (the speed from before the job), "throw" (the speed of the last frames of the move), or {vx, vy}
local function finish(job, completed)
	for _, name in ipairs(job.names) do
		if motion.byName[name] == job then motion.byName[name] = nil end
		local rel = job.release
		if env.objects and env.objects.world and env.objects.world[name] ~= nil then
			if rel == "keep" and job.speeds[name] then env.setVelocity(name, job.speeds[name][1], job.speeds[name][2])
			elseif rel == "throw" and job.last[name] then env.setVelocity(name, job.last[name][1], job.last[name][2])
			elseif type(rel) == "table" then env.setVelocity(name, rel[1], rel[2]) end
		end
	end
	motion.jobs[job.id] = nil
	if completed and job.onDone then
		local ok, err = G.pcall(job.onDone, job.names[1], job)
		if not ok then G.print("motion onDone: " .. tostring(err)) end
	end
end
motion.byName = {}

local function newJob(kind, names, o)
	local job = { id = motion.nextId, kind = kind, names = names, t = -(o.delay or 0), time = o.time, loop = o.loop, yoyo = o.yoyo, realTime = o.realTime,
		release = o.release or "stop", onDone = o.onDone, onUpdate = o.onUpdate, speeds = {}, last = {}, pass = 0, o = o }
	motion.nextId = motion.nextId + 1
	for _, name in ipairs(names) do
		local old = motion.byName[name]
		if old ~= nil then finish(old, false) end   -- a new job on an object replaces the old one
		motion.byName[name] = job
		if env.getVelocity then
			local _, vx, vy = env.getVelocity(name)
			job.speeds[name] = { vx or 0, vy or 0 }
		end
	end
	motion.jobs[job.id] = job
	return job
end

local function listOf(names)
	if type(names) == "string" then return { names } end
	if type(names) == "function" then   -- a filter: every object it says yes to
		local out = {}
		for name, obj in pairs(env.objects.world) do if names(name, obj) then out[#out + 1] = name end end
		return out
	end
	return names
end

-- Catmull-Rom through a list of points {{x, y [, angle]}, ...}: p from 0 to 1 along the whole line
local function along(points, p)
	local n = #points
	if n == 1 then return points[1][1], points[1][2], points[1][3] end
	local f = max(0, min(1, p)) * (n - 1)
	local i = min(floor(f), n - 2)
	local u = f - i
	local function pt(k) k = max(1, min(n, k)) return points[k] end
	local p0, p1, p2, p3 = pt(i), pt(i + 1), pt(i + 2), pt(i + 3)
	local function cr(a, b, c, d) return 0.5 * ((2 * b) + (-a + c) * u + (2 * a - 5 * b + 4 * c - d) * u * u + (-a + 3 * b - 3 * c + d) * u * u * u) end
	local a
	if p1[3] ~= nil and p2[3] ~= nil then a = lerp(p1[3], p2[3], u) end
	return cr(p0[1], p1[1], p2[1], p3[1]), cr(p0[2], p1[2], p2[2], p3[2]), a
end

-- motion.to(name, props): move an object with a tween. props (all optional except that something must say where to):
--   x, y, angle        where it ends (absolute);  dx, dy, dangle  how far it moves from where it is;  from = {x =, y =, angle =}  where it starts (default: where it is)
--   time               seconds (default 1);  delay  seconds to wait;  ease  see below;  easeX, easeY, easeAngle  a different easing for one axis
--   points             {{x, y [, angle]}, ...}: follow a smooth line through these points (the first one is where it starts when `from` is left out)
--   path               function(p, obj) -> x, y [, angle]: ANY movement. p is the eased progress (0 to 1, more or less with an elastic easing); obj is objects.world[name]
--   loop               true or a number of repeats;  yoyo  true: every second run goes back
--   release            "stop" (default), "keep", "throw" or {vx, vy}, see above
--   realTime           true: count real seconds, not game seconds (so slow motion does not slow the move)
--   onUpdate(p, name)  every frame;  onDone(name)  when it has finished (not when it was stopped)
-- ease is a name from motion.easing ("linear", "inQuad", "outCubic", "inOutBack", "outBounce", "outElastic", ... every family has in, out and inOut), a function(t),
-- or {x1, y1, x2, y2} for a bezier. Add your own: motion.easing.wobble = function(t) return t + sin(t * 20) * 0.05 end, then ease = "wobble".
-- It returns the job. Use a table of names, or a function(name, obj) that says yes to the objects you want, to move several at once (they get the same props).
function motion.to(names, props)
	props = props or {}
	names = listOf(names)
	local job = newJob("to", names, props)
	job.time = props.time or 1
	job.ease = easeOf(props.ease)
	job.easeX, job.easeY, job.easeA = easeOf(props.easeX or props.ease), easeOf(props.easeY or props.ease), easeOf(props.easeAngle or props.ease)
	job.start = {}
	for _, name in ipairs(names) do
		local cx, cy, ca = currentOf(name)
		local from = props.from or {}
		local sx, sy, sa = from.x or cx, from.y or cy, from.angle or ca
		if sx ~= nil then
			job.start[name] = { sx, sy, sa,
				props.x or (props.dx and sx + props.dx) or sx, props.y or (props.dy and sy + props.dy) or sy, props.angle or (props.dangle and sa + props.dangle) or sa }
		end
	end
	return job
end

-- motion.drive(name, fn [, opts]): fn(t, dt, obj) is called every frame with the seconds since the start and returns x, y, angle (nil keeps that part). Return false to stop. opts: delay, release, realTime, onDone.
function motion.drive(names, fn, opts)
	opts = opts or {}
	local job = newJob("drive", listOf(names), opts)
	job.fn = fn
	return job
end

-- motion.rewind(frames [, opts]): put objects back along the history, as a movement. The history is the one rewind() uses (the last 600 frames, setRewindLimit changes that).
-- opts: names (a name, a table of names, or a function(name, obj); default every object), time (seconds, default 0.5), ease (any easing: the easing decides how fast the history goes by,
-- so "inOutQuad" is a soft rewind, "outBounce" bounces against the old state, a function that goes 0 -> 1 -> 0.5 plays it back and forth), from (the history frame it starts at, 0 = now, default 0),
-- to (default `frames`: how far back it ends; `to` smaller than `from` plays history forward again), release (default "keep": the speed the object had in the history frame it ends on), onDone, delay, realTime.
-- It returns the job, or nil when there is no history.
function motion.rewind(frames, opts)
	opts = opts or {}
	local depth = env.getRewindDepth and env.getRewindDepth() or 0
	frames = min(frames or depth, depth)
	local from, to = opts.from or 0, opts.to or frames
	from, to = max(0, min(depth, from)), max(0, min(depth, to))
	if depth <= 0 or from == to then return nil end
	local names
	if opts.names == nil then names = listOf(function() return true end) else names = listOf(opts.names) end
	local wanted = {}
	for _, n in ipairs(names) do wanted[n] = true end
	-- the history keeps changing while this runs (every frame is recorded), so the part that is needed is copied now
	local lo, hi = min(from, to), max(from, to)
	local track = {}
	for k = lo, hi do track[k] = env.getRewindState(k) end
	local o = G.setmetatable({}, { __index = opts })
	o.release = opts.release or "keep"
	local job = newJob("scrub", names, o)
	job.time = opts.time or 0.5
	job.ease = easeOf(opts.ease)
	job.track, job.from, job.to, job.lo, job.hi = track, from, to, lo, hi
	return job
end

-- motion.rewindObject(name, frames [, opts]) is motion.rewind for one object
function motion.rewindObject(name, frames, opts)
	opts = opts or {}
	local o = G.setmetatable({ names = { name } }, { __index = opts })
	return motion.rewind(frames, o)
end

-- motion.stop(nameOrJob [, release]): ends the move of an object (or one job) where it is; the job's onDone is not called. release overrides the job's own.
function motion.stop(what, release)
	local job = type(what) == "table" and what or motion.byName[what]
	if job == nil then return false end
	if release ~= nil then job.release = release end
	finish(job, false)
	return true
end
function motion.stopAll() for _, job in pairs(motion.jobs) do finish(job, false) end end
function motion.isMoving(name) return motion.byName[name] ~= nil end

------------------------------------------------------------------ the frame
local function frameAt(job, name, f)   -- the state of `name` at history position f (a fraction of a frame is a blend of two)
	f = max(job.lo, min(job.hi, f))
	local a = floor(f)
	local b = min(job.hi, a + 1)
	local sa, sb = job.track[a] and job.track[a][name], job.track[b] and job.track[b][name]
	if sa == nil then return sb end
	if sb == nil or a == b then return sa end
	local u = f - a
	return { x = lerp(sa.x, sb.x, u), y = lerp(sa.y, sb.y, u), angle = lerp(sa.angle, sb.angle, u), vx = lerp(sa.vx, sb.vx, u), vy = lerp(sa.vy, sb.vy, u) }
end

local function stepJob(job, dt)
	job.t = job.t + dt
	if job.t < 0 then return end
	local gone = {}
	if job.kind == "drive" then
		for _, name in ipairs(job.names) do
			local obj = env.objects.world[name]
			if obj == nil then gone[#gone + 1] = name
			else
				local ok, x, y, a = G.pcall(job.fn, job.t, dt, obj)
				if not ok then G.print("motion.drive: " .. tostring(x)) finish(job, false) return end
				if x == false then finish(job, true) return end
				local ox, oy = obj.x, obj.y
				place(name, x, y, a)
				if dt > 0 then job.last[name] = { (obj.x - ox) / dt, (obj.y - oy) / dt } end
			end
		end
		return
	end
	local raw = job.time > 0 and job.t / job.time or 1
	local done = false
	if raw >= 1 then
		job.pass = job.pass + 1
		if job.loop == true or (type(job.loop) == "number" and job.pass <= job.loop) then
			job.t = job.t - job.time
			raw = job.t / job.time
		else raw = 1 done = true end
	end
	local runBack = job.yoyo and job.pass % 2 == 1 and not done
	local p = runBack and 1 - raw or raw
	if job.yoyo and done and job.pass % 2 == 0 then p = 0 end
	local progress = job.ease(p)
	for _, name in ipairs(job.names) do
		local obj = env.objects.world[name]
		if obj == nil then gone[#gone + 1] = name
		else
			local ox, oy = obj.x, obj.y
			local x, y, a
			if job.kind == "scrub" then
				local f = job.from + (job.to - job.from) * progress
				local s = frameAt(job, name, f)
				if s ~= nil then x, y, a = s.x, s.y, s.angle end
			elseif job.o.path ~= nil then
				local ok, px, py, pa = G.pcall(job.o.path, progress, obj)
				if not ok then G.print("motion path: " .. tostring(px)) finish(job, false) return end
				x, y, a = px, py, pa
			elseif job.o.points ~= nil then
				x, y, a = along(job.o.points, progress)
			else
				local s = job.start[name]
				if s ~= nil then
					x, y, a = lerp(s[1], s[4], job.easeX(p)), lerp(s[2], s[5], job.easeY(p)), lerp(s[3], s[6], job.easeA(p))
				end
			end
			place(name, x, y, a)
			if dt > 0 then job.last[name] = { (obj.x - ox) / dt, (obj.y - oy) / dt } end
			if job.onUpdate then G.pcall(job.onUpdate, progress, name) end
		end
	end
	for _, name in ipairs(gone) do   -- an object that was removed drops out of the job
		for i, n in ipairs(job.names) do if n == name then G.table.remove(job.names, i) break end end
		if motion.byName[name] == job then motion.byName[name] = nil end
	end
	if #job.names == 0 then finish(job, true) return end   -- every object of the job is gone: nothing is left to move, so it counts as done
	if done then
		if job.kind == "scrub" and job.release == "keep" then   -- the speeds of the history frame it ended on
			for _, name in ipairs(job.names) do
				local s = frameAt(job, name, job.to)
				if s ~= nil then job.speeds[name] = { s.vx or 0, s.vy or 0 } end
			end
		end
		finish(job, true)
	end
end

env.engine.on("update", function(dt, realDt)
	if env.objects == nil or env.objects.world == nil then return end
	local list = {}
	for _, job in pairs(motion.jobs) do list[#list + 1] = job end   -- a copy: a job that ends may start another
	for _, job in ipairs(list) do
		if motion.jobs[job.id] ~= nil then stepJob(job, job.realTime and (realDt or dt) or dt) end
	end
end)
env.engine.on("levelLoaded", function() motion.stopAll() end)
