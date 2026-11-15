# Scripting reference

For people who write or change game scripts (the Lua files in the game's `data/scripts`) or add their own to a game running on this engine. The engine runs the game's own scripts and provides the native functions they call; this page lists what it adds on top, how the natives behave, and what is special in each game. Run options and debugging are in [`docs/USAGE.md`](USAGE.md), what changed when in [`CHANGELOG.md`](../CHANGELOG.md).

Contents: [Contact callbacks](#enter-and-exit-collision) · [Angry Birds Star Wars](#angry-birds-star-wars) · [Functions added in 2.4](#new-script-functions-in-24-modding) · [Extra commands from 1.8](#extra-script-commands-new-in-18) · [Full reference of the engine-added functions](#engine-added-script-functions-full-reference) · [All natives](#native-function-summary)

## Enter and exit collision

A sensor (an object made with `setAsSensor`, or one of Space's gravity fields) collides with nothing: other bodies pass through it, but Box2D still reports when a body starts and stops overlapping it. The engine turns those reports into two optional global Lua functions. Define them in your gamelogic (or any script) and they are called; leave them out and nothing happens. The format below follows the comment style of the 1.6.2 `gamelogic.lua`.

```lua
-- enterCollision / exitCollision
--
-- Called by the engine, once per contact, when a body begins or ends touching a SENSOR.
-- Both receive the two object names, as keys of objects.world.
--
-- IMPORTANT: the order of nameA and nameB is Box2D's fixture order, not "sensor first".
-- Always test both names. Sensors never block anything and never cause damage by themselves.
--
-- Solid-vs-solid contacts do NOT come here; those still go through blockCollision.
-- (In Angry Birds Space the gravity fields are handled by the engine itself first; your
-- function is called in addition.)

function enterCollision(nameA, nameB)
	-- a small helper so the order does not matter
	local zone, other
	if string.find(nameA, "^zone_") then zone, other = nameA, nameB
	elseif string.find(nameB, "^zone_") then zone, other = nameB, nameA
	else return end

	local obj = objects.world[other]
	if obj == nil then return end   -- it may already be destroyed

	-- e.g. a trigger zone that pops pigs
	if string.find(other, "^pig_") then
		destroyObject(other, true)
	end
end

function exitCollision(nameA, nameB)
	-- the mirror of enterCollision: the body has left the sensor
		-- check objects.world[...] before using a name: the body may be gone
end
```

Tips for getting the most out of them:

* **Count what is inside.** Keep a table, `inside[zone] = (inside[zone] or 0) + 1` in `enterCollision` and `- 1` in `exitCollision`, and act only when it goes 0 → 1 or 1 → 0. A body that touches the sensor with two fixtures reports once per fixture pair, so counting is safer than a flag.
* **Test names with patterns.** Names are plain strings, so give your zones a prefix (`zone_`, `wind_`, `warp_`) and match with `string.find(name, "^wind_")`.
* **Do work, then return.** Both functions run inside the physics step. Moving, scaling, tinting, playing sounds, changing gravity (`setObjectGravity`) and `destroyObject` are fine; do not loop or wait.
* **Read the other object.** `objects.world[other]` has `x`, `y`, `angle`, `definition`; `getLinearVelocity(other)` gives the speed, which is useful for "only fast things trigger".
* **Build effects out of existing natives.** Wind zone: in `enterCollision` call `setObjectGravity(other, 0, -5)`, in `exitCollision` call `resetObjectGravity(other)`. Teleporter: `setPosition(other, tx, ty)` on enter. Score pad: add to the score table and play a sound on enter.
* **Compare with the retail chain.** Space's own gamelogic only defines `blockCollision(...)`, which fires the object's `onCollisionEnter` actions with `{name, obj, other, force, wasDamageDone, dot, resultDamage}` and stops when an action sets `continueExec = false`; `onCollisionExit` exists on objects too. `enterCollision` / `exitCollision` are the lower-level, engine-wide version of the same idea and work in Classic, Seasons, Rio and Space alike.

## Angry Birds Star Wars

Star Wars (PC) runs through the same script host as Angry Birds Space (`space_` natives) plus its own set (`starWars_`). Its natives are registered as `native_<name>` and the scripts call them bare or through `gamelua.`; the resource natives sit in a `ResourceManager` table (`native_createAudio` and the others are the `res.<name>` functions). Everything below is what the retail executable does, ported from the decompile, so a script behaves the same here as in the original game.

### Object parameters: `setObjectParameter(name, id, value)`

| id | Meaning in Star Wars |
|---|---|
| 1 | draw with the birds (`drawWithBirds`) |
| 2 | 0 makes the body static, anything else dynamic |
| 5 | draw scale (x and y); 17 and 18 set scale x and scale y alone |
| 6, 7 | hit wobble amplitude / frequency multiplier (`bounceAmplitudeMultiplier`, `bounceFrequencyMultiplier` of a block) |
| 8 | draw the sprite mirrored (1) or not; 9 turns on enable flipping: the object mirrors itself towards its direction of travel when it moves faster than 3 sideways |
| 10 | draw the sprite upright whatever the body's angle |
| 11 | a bird-bird hit does not start this object's wobble |
| 12 | the object does not count for `hasMovingObjects` / `hasMovingObjectsZeroTolerance` |
| 15 | `disableImmovable` |
| 16 | ignore the classic gravity (the body's gravity scale is 0 for 1) |
| 19, 20 | flags the contact filter reads: 19 makes a kind 7 object collide again, 20 makes a damaging sensor skip the object |
| 21 | sensor kind: 2 gravitation, 3 stream, 4 kill sensor, 5, 6, 7 damaging sensors (5 and 7 are also removed past the level limits) |
| 22 | gravity on (a sensor pulls only while this is 1) |
| 25, 26, 27 | a sensor's minimum force, maximum force and radius |
| 33 | visible (1) |
| 34 | `stopOnExit`: the object is destroyed after it leaves a flagged sensor |
| 35 | height / extent of a box sensor |
| 36 | collision group index: two objects with the same index above 0 never collide |

Parameters the natives below set for you (the engine's own ids 200 and up, do not rely on them):

| Native | Sets |
|---|---|
| `native_setBlockCollisionEnabled(name, on)` | the object takes part in block collisions |
| `native_setIgnoresScore(name, on)` | hits on it score nothing |
| `native_setKeepOrientation(name, on)` | the object keeps its orientation in water |
| `native_setAffectedByForceMultipliers(name, on)` | the world's force multipliers (water, gravity) apply |
| `native_setIsWater(name, on)`, `native_setWaterDensity(name, d)` | a sensor is a pool, with a density the buoyancy compares to |
| `native_setControlled(name, on)` | the object is a bird (published with `xVel`, `yVel`, `angle`) |
| `native_setTimeSinceCollision(name, t)` | the bird's gravity timer: it counts up while it is 0 or more, a negative value stops it |
| `native_setMass(name, density)` | the density of every fixture of the body |

### Collision callbacks

* `birdCollision(a, b, force, damage, pointX, pointY, normalX, normalY)` — a bird hit another bird (`damage` is 0) or a block.
* `blockCollision(a, b, force, wasDamageDone, false, damage, pointX, pointY, normalX, normalY)` — two blocks, or a bird and a block. The fifth value is always false.
* `enterCollision(a, b)` when a body starts touching a sensor; `exitTriggerCollision(a, b)` when it stops touching one, and `exitCollision(a, b)` whenever any contact ends. The order of the two names is Box2D's, so test both (see [Enter and exit collision](#enter-and-exit-collision)).

The forces and damage follow the exe: force divided by 10 after `forceDamageMultiplier`, bird damage scaled by the bird's `damageMultiplier` and `powerupDamageMultiplier`, a HOMING bird that has not collided yet does a fifth, and the score added is `floor(damage) * scoreDamageMultiplier`. The game's config holds `forceDamageMultiplier` (1), `scoreDamageMultiplier` (5) and `defaultGravityForceMultiplier` (4).

### Water, gravity fields and kill sensors

* A gravity sensor made a pool with `native_setIsWater` pushes boxes up and pulls circles towards its centre. `setWaterForceMultiplier(m)` (2), `setBirdWaterDrag(d)` (0.4) and `setObjectWaterDrag(d)` (0) change the constants; the getters return them.
* A sensor with kind 4 starts a kill timer on every non-sensor that touches it; an object with parameter 34 does the same when it leaves a flagged sensor. The frame update counts the timers down and destroys the object (`strength` 0 and `deadBlocks`).
* Sensors with kind 5 or 7 are removed once they are more than 0.88 times a level limit away.

### What the frame update publishes

For every awake object, in `objects.world[name]`: `x`, `y`, `angle`, `sleeping`, `frozen` (outside the level limits set with `setLevelLimits`), `velocity` (the speed), and for birds, objects with material `propeller` and objects after `setRecordVelocity(name, true)` also `xVel`, `yVel`. Globals: `hasMovingObjects`, `hasAwakeObjects`, `hasMovingObjectsZeroTolerance`.

### Shapes and drawing

* `createLineShape(name, sprite, x, y, width, height, density, friction, restitution, collision, controllable, z_order)`: call `clearVertices()` and `addVertex(x, y)` for each point first. The body is a chain of edges, one for each pair of neighbouring points (Box2D 2.2). `objects.world[name].type` is `"line"`. Edges have no inside: bodies that start inside a closed outline are not pushed out, so use `createPolygon` for solid shapes.
* `renderGravityVisualsNative(sensor, screenX, screenY, zoom)`: draws the `gravityVisuals` list of a gravitation sensor. Circle entries `{sprite, x, y, scale}` are drawn eight times round the sensor; box entries `{sprite, x, y, pos}` along its length. Pass a non-zero `zoom` (the game's own script leaves it out, which draws nothing).
* The aiming aid: `enableAimingAid(on)`, `setAimingAidSprite(name [, scale])`, `updateBirdTrajectoryTable()`, `populateAimingAid()` and `clearAimingAid()`. The globals `simulationIterations` (50), `simulationTimeStepMultiplier` (3), `simulationStorePointsSampler` (1), `simulationAimSpeed` (4) and `simulationAimSpawnTime` (0.6) shape it.

### Mod ideas that work here

```lua
-- a bouncy pad: hits start a long, strong wobble
setObjectParameter("pad", 6, 3)      -- amplitude multiplier
setObjectParameter("pad", 7, 2)      -- frequency multiplier

-- a drain (a sensor): anything that touches it is destroyed
setObjectParameter("drain", 21, 4)

-- a ground of thin edges for a hand-made level
clearVertices()
addVertex(-10, 0); addVertex(0, 1); addVertex(10, 0)
createLineShape("ground", "", 0, 5, 1, 1, 0, 0.5, 0, true, false, 1)
```

## New script functions in 2.4 (modding)

Every one of these is native, so it works in every game and version the engine runs. The graphics setters take an optional **target** as their last argument (see "Graphics settings for one target").

### Objects and physics

| Function | What it does |
|---|---|
| `setScale(name, sx [, sy])`, `getScale(name)` | The scale the object's picture is drawn with: the same value `setObjectParameter(name, 5, v)` sets, without going through it. Leave `sy` out for a uniform scale. `getScale` returns `sx, sy` (nil if there is no such object). It scales the picture only; `resizeCircle` changes the shape (the Time Travel puffer bird calls both). |
| `setSpeedCap(name, maxSpeed [, maxVx, maxVy])`, `getSpeedCap(name)`, `clearSpeedCap(name)` | A limit for one object only. `maxSpeed` (units per second) **replaces** the global `setMaxTranslation` for that body, inside Box2D's solver: a capped object can go faster than the world's limit (if `maxSpeed` is higher) or be held lower (if it is lower), while every other object keeps the world's limit. `maxVx` / `maxVy` (optional, 0 or nil = none) clamp each axis after every step. `clearSpeedCap` gives the body back to the world's limit. |
| `getTouchingObjects(name)` | A table of the names of every object the body touches this very step (it walks the body's contact list; sensors and disabled contacts included). |
| `rayCast(x1, y1, x2, y2 [, all [, shape]])` | Box2D's own ray cast. Without `all`: `name, hitX, hitY, normalX, normalY, fraction, centreX, centreY` of the nearest object (nil if none). With `all = true`: a table of `{name, x, y, nx, ny, fraction, cx, cy}` for every object the segment crosses, nearest first. **`shape` (3.4) turns the ray into a shape cast**: the shape is swept along the segment (Box2D's time of impact, against every non-sensor fixture in the swept area) and `cx, cy` is where its centre is when it first touches (for a plain ray they equal the hit point); `fraction` 0 means it already overlaps. `shape` is a table: `{radius = r}` a circle; `{sides = n, radius = r}` a regular polygon of 3 to 128 sides (8 = an octagon, radius = centre to corner); `{w = w, h = h}` a box; `{points = {x1, y1, x2, y2, ...}}` any polygon around the centre, convex or concave, with as many points as you like (a concave one is split into convex pieces and the earliest touch counts); `angle = a` rotates it; `skip = "name"` leaves one object out (the caster). Physics units. |
| `fileExists(path)` | true if the file exists in the game folder or in the saves folder; nothing is loaded. |
| `res.setString(group, key, text)` | Sets one text of a text group and makes the group if it is new, so a script can inject wholly new text that `res.getString` and `drawString` then show. The console has the same: `text <group> <key> <text>` and `texts [group]`. |

Already there, nothing new needed:

* **Fixed rotation:** `setFixedRotation(name, true / false)` locks or unlocks tumbling on the live body, no fixtures are rebuilt.
* **Sensor (trigger volume):** `setAsSensor(name, true / false)` turns every fixture of the object into a sensor; the contact then goes to `enterCollision` / `exitCollision` instead of `blockCollision`, and nothing blocks the movement.
* **Visual rotation:** `setVisualRotation(angle [, target])` (new name) or the older `setSpriteRotation(name, angle)` turn the picture and leave the body alone.

### Audio

`res.setAudioClipPitch(name, p)` (speed and pitch together, like a record), `res.setAudioClipTempo(name, t)` (speed only, the pitch stays), `res.setAudioClipPan(name, -1 .. 1)`, `res.resetAudioClipEffects(name)`. They work on the voices of that clip playing now **and** are remembered for the name, so they can be set before `playAudio` and the next play starts with them. `res.seekAudioClip(name, seconds)` jumps inside a playing clip (custom loops, synchronised cues), `res.getAudioClipTime(name)` and `res.getAudioClipLength(name)` give seconds (-1 if nothing plays / unknown). `setAudioClipSpeed` and `setAudioSpeed` stay as they were.

### Graphics settings for one target

`setAlpha`, `setTint`, `resetTint`, `getTint`, `setSkew`, `setBend`, `setRenderZ`, `setBlur`, `resetBlur`, `setPixelate`, `resetPixelate` and the new `setBlendMode`, `setShader`, `setTintChannel`, `setVisualRotation` take a string as their **last** argument. Without it they change the render state like before (what the next sprites are drawn with). With it they are kept for that target and applied whenever it is drawn:

* an **object name** (`setAlpha(0.5, "pig_3")` is the same as `setObjectAlpha`, so you do not need that one any more; the others apply to its picture),
* a **sprite name** (every drawing of that sprite),
* a **composite sprite name** (all of its parts; give one part's sprite name to change only that part),
* `"background"`, `"foreground"`, `"trajectory"` (the dots, puffs and Space's aiming aid) and `"particles"`: what the engine draws itself.

`resetRenderOverrides(target)` forgets everything set for a target. Examples: `setTint(1, 0.3, 0.3, "background")`, `setBlendMode("add", "pig_3")`, `setShader("grayscale", "foreground")`, `setBlur(4, "TRAIL_WHITE_1")`.

#### Layout of a target (3.3)

The same targets also have a layout, set with their own functions (a target is required):

* `setOffset(dx, dy, target)` moves everything of the target by `dx`, `dy` pixels (world pixels for the trajectory, the aiming aid and objects). `getOffset(target)` returns `dx, dy`.
* `setLayoutScale(sx [, sy], target)` scales the sprites of the target, each about its own centre. `setLayoutScale(sx, sy, px, py, target)` scales their positions too, about the point `px`, `py`, so a whole group grows or shrinks as one.
* `setMirror(true | false, target)` mirrors the sprites left to right.
* `resetRenderOverrides(target)` forgets the layout together with the other settings.

Examples: `setOffset(0, 120, "background")`, `setLayoutScale(1.6, 1.6, 640, 360, "foreground")`, `setOffset(-60, -40, "trajectory")`. Anchors are resolved first, so offset and scale act on the sprite's top left corner and size; `engine.drawSprite`'s `dx`, `dy`, `sx`, `sy` and a target's layout add up.

* `setBlendMode(mode [, target])`: `"normal"`, `"add"`, `"multiply"`, `"screen"` (or 0 to 3). `setRenderState` takes it as its 20th value.
* `setShader(name [, target])`: `"none"`, `"grayscale"`, `"sepia"`, `"invert"`, `"chromatic"` (colour fringes), `"glow"`, or the name of a shader of your own (3.3, see "Shaders" below). `setRenderState` takes it as its 21st value.
* `setTintChannel("r" | "g" | "b" | "a", value [, target])`: one channel of the tint (or the alpha) and nothing else; above 1 brightens.
* All of them also work on the tiled "texture" blocks (the ground and static blocks): tint, blur, bend, pixelation, blend mode and the presets apply to the tile texture seen through the block's shape.
* `res.getSpritePixel(sprite, x, y)` returns `r, g, b, a` (0 to 255) of the pixel x right and y down from the sprite's top left corner (nil if outside); for collision masks, colour lookups and the like.
* `res.getCompoSprites(name [, "current" | "all"])`: for a composite sprite (or an object drawn as one) `"current"` (default) is a list of `{sprite, x, y, scale, angle}` for the parts it is made of now, `"all"` is a list of the different sprite names it uses.

### The console

New commands (also listed in "The console" below): `text <group> <key> <text>`, `texts [group]`, `gravity [x y]`, `maxtranslation <t>`, `scale <object> <sx> [sy]`, `speedcap <object> <speed>`, `blend <object> <mode>`, `shader <object> <name>`. Anything else typed is still run as Lua (`lua getTouchingObjects("pig_3")`), and since every native is a Lua function, anything the engine does can be changed from there.

### Single-folder builds and window

Space builds that keep everything in one folder are accepted: their `SettingsWrapper` global and the bit functions their scripts call are provided. The command prompt closes with the game when the game is closed normally; after a crash it stays open so the message can be read.

## Extra script commands (new in 1.8)

These are not in any of the supported games; the engine adds them so scripts can use them, the same way they use `physicsTimeStep`. Nothing changes for the original scripts.

### Transparency (alpha)

`setRenderState(tx, ty, sx, sy, angle, pivotX, pivotY, alpha)`: the 8th value is the transparency of everything drawn after it, from 0 (invisible) to 1 (opaque); out-of-range values are clamped. `setAlpha(a)` does the same. The value stays until you set it again, and a call with only 7 values does not reset it, so always put it back to 1 when you are done:

    setRenderState(-screen.left, -screen.top, worldScale, worldScale, 0, 0, 0, 0.4)   -- everything after this is 40% visible
    drawSprite("BLOCK_WOOD_1", x, y)
    setRenderState(-screen.left, -screen.top, worldScale, worldScale, 0, 0, 0, 1)     -- back to solid

Masked and tiled sprites (the ground blocks) use it as well.

### Transparency of one object

`setRenderState` cannot reach objects that `drawGameNative` draws (blocks, birds, pigs), so the engine has a native per-object value:

- `setObjectAlpha(name, alpha)`: from 0 (invisible) to 1 (solid, the default); out-of-range values are clamped. It stays on that object until you set it again, and it does not touch anything else that is drawn.
- `getObjectAlpha(name)`: the value, or -1 if there is no such object.

Use it with the name that `createObject` returned, or the keys of `objects.world`. For example, to make all ice (light) blocks of a Rio level see-through, and fade one block:

    for name, obj in _G.pairs(objects.world) do
        if _G.string.find(name, "BLOCK_LIGHT") then setObjectAlpha(name, 0.3) end
    end
    setObjectAlpha("BLOCK_ROCK_1X10_1_2", 0.6)

It applies to the whole object, composite sprites included, and works in every game.

### Collider types

Collider types decide which objects can hit which, as in the later official versions and the last Seasons (ghost blocks). The engine only provides the numbers and the commands; the names are a table in the game's own script. In those versions that table is `collider_types` in `blocks.lua`. The other games do not have it, so to use names there add the same table to your own `blocks.lua` (or `gamelogic.lua`) as a plain global array:

    collider_types = { basic = 0, eagle = 1, static = 2, bird = 3, pig = 4, ghost = 5, sticky = 6 }

and keep going with your own numbers (the rule table holds 16 types, 0 to 15):

    my_types = { ice_ghost = 7, ice_wall = 8 }

Commands:

- `setColliderType(objectName, type)` / `getColliderType(objectName)`: give an object a type (call it after the object exists, for example right after `createObject`). The type is a number 0 to 15.
- `setColliderRule(typeA, typeB, collides)` / `getColliderRule(typeA, typeB)`: change one pair (it works both ways round). `collides` is `true` or `false`.
- By default every type collides with every other, except type 5 (ghost), which only collides with 1 (eagle) and 3 (bird). The sticky type (6) is stored but has no special behaviour yet.

Example, in a script that has the table above:

    setColliderRule(my_types.ice_ghost, collider_types.bird, true)
    setColliderRule(my_types.ice_ghost, collider_types.pig, false)
    setColliderRule(my_types.ice_ghost, my_types.ice_wall, false)

    local name = createObject(blockTable, "BLOCK_LIGHT_1", nil, x, y)
    setColliderType(name, my_types.ice_ghost)

### `createPolygonSprite(newName, sourceSprite, points [, outlineWidth [, darken [, fill [, holes]]]])` (3.4)

A copy of `sourceSprite` cut to a polygon, for the sprite of a block that is not a box. `points` is `{ {x = 0.5, y = 0}, {x = 1, y = 1}, ... }`, 0 to 1 over the sprite (the numbers a block's `vertices` hold), any outline, concave too; the edge is anti-aliased. `outlineWidth` (pixels, default 0) darkens the edge by `darken` (default 0.55) so the cut has an edge; `fill = true` fills the holes of the source (the wood square is a frame) with its average colour. Returns the number of pixels inside, or nil when the sprite is unknown. Use the new name anywhere a sprite name goes.

### Shape blocks (extra_scripts.lua, 3.4)

`extras/extra_scripts.lua` (section 5, table `shapeBlocks`) adds three blocks to the wood group: `WoodBlock13` a five-pointed star (concave), `WoodBlock14` a wedge, `WoodBlock15` a chevron (concave). They are put into `blockTable.blocks` (a block of type `"polygon"` with `vertices` from 0 to 1), the wood group's `lastIndex` is raised so the level editor's W / Q keys reach them, and the sprites (and the four damage stages) are cut from the wood square with `createPolygonSprite`. A level that names them loads them; from a script, `createObject` makes one. Add your own by putting another entry in `shapeBlocks.blocks`: a name like `WoodBlock16` and a list of points.

### Blocks with holes (3.6)

A body can have holes that are really empty: the physics is made of the convex pieces that remain, so the body is lighter, things pass through the hole, and it collides along the hole's edge. The outline can be a box, a circle (a 32-sided polygon) or any polygon (concave too). A hole can be any shape, anywhere: fully inside, in the middle, across an edge or a corner (a hole over the top-left corner of a box takes just that corner), or outside (it does nothing); holes may overlap.

A shape is a table:

| Shape | Fields |
|---|---|
| box | `{type = "box", x =, y =, w =, h =, angle =}` (centre `x`, `y`; `angle` in radians; `size =` for a square) |
| circle | `{type = "circle", x =, y =, radius =, segments =}` (`segments` 6 to 64, default 16 for holes and 32 for an outline) |
| polygon | `{type = "polygon", points = { {x =, y =}, ... }, x =, y =}` (`x`, `y` move the points; a bare list of points is a polygon too; concave is fine) |

`createShapeWithHoles(name, sprite, x, y, outline, holes, density, friction, restitution, collision, controllable, zOrder)` makes the object: `outline` is one shape and `holes` a list of shapes, in world units about the object's origin. It writes `objects.world[name]` like `createPolygon` does (the mass is smaller by the holes' area).

```lua
-- a 4 x 4 block with a 2 x 2 hole in the middle and a round hole over its top-left corner
createShapeWithHoles("frame1", "BLOCK_WOOD_4X4_1", 10, -6,
    {type = "box", w = 4, h = 4},
    { {type = "box", x = 0, y = 0, w = 2, h = 2}, {type = "circle", x = -2, y = -2, radius = 0.8} },
    1, 0.5, 0.2, true, false, 5)
```

`registerHoles(sprite, holes [, vertices])` gives the holes to every block whose first sprite is `sprite`, whichever way it is made (a level file, `createObject`, `createBox`, `createCircle` or `createPolygon`). Here the shapes are 0 to 1 over the block (centre 0.5, like a block's `vertices`) and a polygon block passes its own `vertices` (0 to 1) so that the holes follow its outline. `createPolygonSprite(newName, source, points, outlineWidth, darken, fill, holes)` takes the same holes as its seventh argument and leaves them clear in the sprite, with the darkened edge along them.

`extras/extra_scripts.lua` (section 5, `shapeBlocks`) uses it: `WoodBlock16` is the wood square with a square hole in the middle (a `box`, so `createBox` makes it) and `WoodBlock17` is a wooden circle with three round cut-outs. An entry of `shapeBlocks.blocks` takes `holes` (and `type = "box"` when it has no `vertices`); the damage sprites are cut with the holes as well.

A block with holes is many fixtures; the contacts of its pieces with another body in one step count as one hit.

**Holes in every creation call (3.6).** After the usual arguments, `createBox`, `createCircle` and `createPolygon` take a table of shapes (the same shape specs, in world units about the object): `createBox("b", "SPRITE", x, y, 4, 4, 1, 0.5, 0.2, true, false, 5, { {type = "circle", x = 0.5, y = 0.5, radius = 0.6} })` (the table goes after the z order; with a circle's collision flag it is the twelfth argument). A block definition with `holes` (shapes from 0 to 1 over the block; a polygon block also has its `vertices`) gets them from `createObject`, from level files and from the editors; `extras/extra_scripts.lua` registers them (`registerHoles`) whenever the block table is loaded.

### Pop-ups: `modalOpen` and `modalInput` (3.6)

A script that sets the global `modalOpen = true` stops the game from getting keys and the mouse (the pause page, the birds and the menus see nothing pressed). While it is open `modalInput` holds `pressed = {KEY = true}` (keys that went down this frame), `down` and `clicked` (the left button) and `x`, `y` (the cursor), so the script can draw its own pop-up and read the answer. `extras/extra_scripts.lua` (section 7, table `exitPrompt`) uses it to ask whether to keep the state of a level when it is left. Set `modalOpen = false` to give the game its input back.

### Older joint call with the newer types (3.6)

`createJoint(name, end1, end2, type, coordType, x1, y1, x2, y2 [, extra])`: the table `extra` holds the fields of the types that came later (`groundX1`, `ratio`, `joint1`, `targetX`, `maxLength`, ...), so the nine positional values and the table of fields work in every game, together, and a level can hold both kinds of joints.

### Save states (3.5)

The engine remembers a level that was left half played, in every game. While a level runs (the game mode is `updateGame`) its state is written to `savestates.lua` in the save folder when a script (or the leave pop-up of `extras/extra_scripts.lua`) calls `saveStates.save()`; `saveStates.autosave` (seconds) writes it on its own while a level runs, and `saveStates.saveOnLeave = true` writes it when the level is left and when the game is closed. Both are off by default. When the same level is loaded again the state is put back in the first frame the level runs, also in a new run of the game: a dead bird stays dead, a cracked block stays cracked, a block that was moved stays where it was. A level is known by the path it was loaded from (`loadLevel(path)`). The code is `src/lua/save_states.cpp`; it needs nothing from the game's scripts except the names below, which the games share.

What is kept: every object in `objects.world` (place, angle, speed, at rest or not, and each number, string and flag in its table: strength, damage sprite, `startNumber`, ...); the script variables named in `saveStates.globals` and the tables in `saveStates.tables` (the score table, the camera); the objects that `saveStates.objectGlobals` point to (the flying bird); the camera function. Objects that are gone are removed again (and taken out of the tables in `saveStates.objectTables`). Objects the scripts made after the level began (the halves of a Blue Bird, debris) are not part of it. A restart, finishing a level and failing it clear the state.

The settings are the global table `saveStates`; the engine makes it with these values and a script may change any of them (at any time, from a game script, `extra_scripts.lua` or the console):

| Field | Default | What it does |
|---|---|---|
| `enabled` | `true` | `false`: nothing is saved or put back. |
| `skipLevels` | `{}` | Parts of level paths : these levels are never saved or put back (3.6). |
| `file` | `"savestates.lua"` | The file in the save folder. Delete it to forget every level. |
| `autosave` | `0` | Seconds (real time) between writes while a level runs; `0`: none. |
| `saveOnLeave` | `false` | `true`: the state is also written when the level is left and when the game is closed. |
| `restartWindow` | `2` | A level that is loaded again within this many seconds after it was left is a restart, not a visit: its state is cleared and it starts from the beginning. |
| `readyVariable` | `"levelStartPosition"` | The level counts as set up when this script variable exists and (for a table) has `x` not 0; `""`: at the first frame the level runs. |
| `globals` | the bird queue (`currentBirdIndex`, `currentBirdName`, `birdFired`, `birdReady`, `birdsShot`, ...), `score` and the timers (not the camera, since 3.6: add `worldScale`, `zoomLevel` and the `screen` table to `tables` to keep it) | Script variables (numbers, strings, flags) that are saved. Add the names of your own. |
| `tables` | `{"scoreTable"}` | Global tables whose plain values (down to three levels) are saved and merged back. |
| `objectGlobals` | `{"flyingBird", "selectedBird", "cameraTargetObject"}` | Variables that hold one of the level's objects (saved by name). |
| `objectTables` | `{"birds", "otherBirds", "levelGoals"}` | Tables keyed by object name that an object is taken out of when it is gone. |
| `cameras` | none (since 3.6) | Names of the functions `cameraFunction` can be, so it can be put back. |
| `skipWhen`, `skipWhenPositive` | `{"levelCompleted"}`, `{"levelFailedTimer"}` | Variables that, when true (or above 0), mean the level is over and nothing is kept. |

| Function | What it does |
|---|---|
| `saveStates.save()` | Writes the state of the level that is running now; true when it did. |
| `saveStates.apply()` | Puts the saved state of the loaded level back now; true when there was one. |
| `saveStates.has([path])` | True when a state is kept for the loaded level (or for `path`). |
| `saveStates.forget([path])` | Clears the state of the loaded level (or of `path`) and keeps it cleared until the next level load: a script that restarts a level calls this first (`extra_scripts.lua` does for its own restart). |
| `saveStates.clearAll()` | Clears every state. |

To keep one more variable of a game, or to start every level from the beginning:

```lua
table.insert(saveStates.globals, "myComboCounter")
-- or:
saveStates.enabled = false
```

### Achievements: adding and removing them (3.6)

The achievements the games list are a table, `achievements`, that the game's scripts (or the engine, from `achievements.lua` of the data) fill: the key is the achievement's name, the value its id (Game Center, Classic) or a table with the icon (`{icon = "SPRITE"}`, Angry Birds 2.0.0 and Seasons 4.x). `src/lua/achievements_page.lua` reads that table when the F7 page opens and each time a pop-up is made, so a script changes the list by changing the table. It loads only when the table has at least one entry, so a game with no list at all has no page to add to.

**Add one**, from `extras/extra_scripts.lua` or any script that runs after the game's scripts:

```lua
achievements["Block Party"] = "com.example.blockparty"      -- the id (or {icon = "ACHIEVEMENT_SPRITE"} where the game keeps tables)
abAchievementTexts = abAchievementTexts or {}
abAchievementTexts["Block Party"] = "Smash 20 blocks in one shot."    -- the how-to text the page shows (without it the page falls back to the text it works out from the game's data, or none)
```

An unlock is `unlockAchievement(id, "Block Party")` (the name is what is kept and shown); the Classic and Seasons scripts have `addToAchievementUnlockQueue("Block Party")`, which does the same through the game's queue (one pop-up every 3 seconds). The unlock is written to `settings.abAchievements["Block Party"]` (the time) and is shown once: the game's own pop-up where it has one (`BannerPopup`, `AchievementPopup`), otherwise a banner of the same make. The pop-up's icon is the table's `icon` (or `achievementPopUpPage.icons[name]` in Angry Birds HD 1.4.2); give a sprite that is loaded in the game, or the pop-up shows no icon. When the achievement counts something, count it in a variable and unlock it in an `engine.on("update", ...)` or `engine.on("contact", ...)` handler.

**Remove one:** `achievements["Block Party"] = nil` takes it off the page and stops its pop-up (`unlockAchievement` still keeps an entry for it in the settings). To take away an unlock the player already has, set `settings.abAchievements["Block Party"] = nil` (the games' own records, `settings.achievements[name]`, are cleared the same way) and save with `saveSettings()`. To hide every achievement, make the table empty before the game's menus are made; the page and the Game Center button then do nothing.

To see a new entry, press F7; the Test pop-up button (or T) shows the pop-up of a made-up achievement without saving anything.

### Polygon skin

`setPolygonRadius(r)` / `getPolygonRadius()`: the collision skin of boxes and polygons (every game since 3.6, shapes already in the world change too; default 0.1, the skin only decides when blocks count as touching, it does not push stacked blocks apart).

### Draw order and z

Everything the engine draws in a frame is first collected in one list and sent to the screen at the end of the frame, or when the clip rectangle changes. Nothing is drawn "now", so a script can place a sprite below something it already drew.

**What has a z.**

| Drawn by | z it has | Where it sits |
|---|---|---|
| a block, bird or other physics object (`drawGameNative`) | its `z_order` (from the level / block definition, or `setObjectZOrder`). In Rio and Space every object has one; in the earlier games it counts only when `setObjectZOrder` was used, otherwise it is 0 | the game draws them in its own groups (see below) |
| a sprite, `drawRect` or `drawLine2D` after `setRenderState(..., z)` with `z` not 0 | that `z` | placed by z, see the rule below |
| everything else: sprites with `z` 0, text, backgrounds, foregrounds, particles, trajectory dots, the hand cursor | none | in the order the script (or the engine) drew it; never moved |

The z of an object is read with `getObjectZOrder(name)` or `objects.world[name].z_order`, and `getObjectDrawOrder()` lists the objects in the order they are drawn (see "New in 2.2.3").

Because `z` is reset by `setRenderState` like the other render state values, set it right before the sprites that need it and set it back to 0 afterwards.

**The order of `drawGameNative`.** Space and Rio draw every physics object in one pass over its z layers, lowest first (the create natives file each object under `(int)z_order`). Space has the layers 0 to 169 (an object outside them is not drawn) and sorts the objects of one layer by name; Rio keeps the creation order inside a layer. The earlier games draw the objects in this order, whatever their z: static blocks with `z_order` below 5, the other blocks below 5, the trajectory dots and puffs, the birds (and objects that draw with the birds) below 5, and everything with 5 or more.

**The rule for a sprite with a z.** The sprite is put right after the last object or z sprite that has a `z` lower than or equal to its own, among those drawn so far. If there is none, it goes right before the first one with a higher `z`. When nothing with a `z` has been drawn yet, it stays where it is. An object or sprite drawn *later* with a lower `z` is put below it the same way, so the order does not depend on whether the script draws before or after `drawGameNative`. Equal `z` keeps the order they were drawn in. Sprites with `z` 0 are not moved by any of this.

Examples (Rio, a block of `z_order` 5 and a bird of 0 on screen):

```lua
drawGameNative()
setRenderState(0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 3)   -- z 3
res.drawSprite("SOME_SPRITE", x, y)    -- above the bird (0) and every block below 3, below the block of z 5
setRenderState(0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, -1)  -- z -1
res.drawSprite("SOME_SPRITE", x, y)    -- below all objects, above the background drawn before drawGameNative
setRenderState(0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0)   -- back to "in drawing order"
```

Because the objects are drawn in groups, an object of a higher `z` that sits in an earlier group (a block of `z_order` 4 is drawn before the birds) stays below a sprite of `z` 3 that was placed after the last object not higher than 3. If you need a sprite above or below one particular object, give both the exact `z_order` you want with `setObjectZOrder` and use a `z` between them.

**Limits.** A clip rectangle change (`setClipRect` and friends) and the end of the frame close the list: a sprite cannot be placed below something that was drawn before the clip changed. The hand cursor is always drawn last, on top. Test with `drawRect(r, g, b, a, x1, y1, x2, y2, true)` (the last argument keeps the render state, so the rect takes the `z` you set).

## Engine-added script functions: full reference

These functions do not exist in any of the original games. A script can use them in every game (unless the text says otherwise); a script that never calls them behaves exactly as before. They are registered in `src/lua/lua_api.cpp`. Every function that takes an object name accepts the names `objects.world` uses, and a name that does not exist never raises an error: the function returns `false` / `nil` (as written below) and does nothing.

### `createSilhouette(newName, sourceSprite, r, g, b, a [, outlineWidth])`

Makes a new sprite called `newName` out of the shape of an existing sprite. Every pixel that `sourceSprite` draws (its alpha is not 0) is painted in one colour and everything the source leaves clear stays clear, so a pig becomes a flat pig-shaped patch and a bird a bird-shaped shadow. The source can be any sprite a loaded sheet knows; the silhouette keeps its size and pivot, and works anywhere a sprite name goes (`setSprite`, `drawSprite`, an object's sprite, a compo part).

- `r, g, b, a`: the colour, each 0 to 1 (1, 1, 1 is white), or 0 to 255 when any value is above 1. The source pixel's own alpha is multiplied by `a`, so soft edges stay soft and `a = 0.5` gives a half see-through shadow. A value left out is 1.
- `outlineWidth` (default 0): 0 fills the whole shape; a number of pixels above 0 keeps only the border of the shape, that many pixels wide, measured inwards from every edge including the edges of holes. This gives an outline sprite to draw behind or over the original for a glow or a selection frame.
- Returns the number of pixels the shape has (it does not depend on `outlineWidth`), or `nil` when the source sprite cannot be found. The sprite is built when the call runs, so releasing the source's sheet later leaves the silhouette intact. Calling it again with the same `newName` replaces the earlier sprite.

### `predictCollision(nameA, nameB [, restitution [, aVx, aVy, bVx, bVy]])`

Works out what would happen if two objects met, from where they are and how they move right now. Nothing is simulated and nothing is changed, so a script can ask before a bird is launched or on every frame. It returns a table. If either name is not an object the table is `{ valid = false, collides = false, filtered = false }` and nothing else.

The hit is worked out the way Box2D's contact solver resolves one contact point, from the real shapes: the closest points of the two bodies' fixtures give the contact point and the normal (so a box corner, a polygon edge or a circle is found where it really is), the lever arms from each centre of mass to that point give the spin, a normal impulse gives the bounce and a friction impulse along the tangent (limited by the friction cone, `friction * normal impulse`) gives the slide, and both act on the bodies' linear and angular velocities through their masses and rotational inertias. `restitution` is optional: left out, the larger of the two fixtures' restitutions is used, as Box2D does, and Box2D's velocity threshold applies (a contact slower than 1 unit per second does not bounce). Static bodies do not move and have infinite mass. In Box2D 2.1 worlds (Classic, Seasons) the real shapes are not queried: the contact is taken on the line between the centres (`exactShapes = false`), with no spin or friction effect. It is still a one-contact estimate: a block that lands on several things at once, joints, other objects pushing and the solver's iterations are not part of it, so the numbers are very close for two bodies meeting and only indicative for a pile.

`aVx, aVy` and `bVx, bVy` are optional too: they replace the present linear velocity of A and of B for the estimate ("what if the bird came in at this speed?"). Any of them that is `nil` or left out keeps that body's current velocity, so `predictCollision("bird", "pig", nil, nil, nil, 0, 0)` tests the bird as it is against a pig held still. A static body always stays at rest.

| Field | Meaning |
| --- | --- |
| `valid` | both objects exist |
| `filtered` | the contact filter keeps the pair apart (a sensor, an object with collision off, Space's gravity and sensor rules); they never collide whatever their speeds |
| `collides` | not filtered and moving towards each other at the contact point, so a contact is coming |
| `exactShapes` | the contact comes from the real shapes (false: the line between the centres) |
| `distance`, `gap` | between the centres of mass; between the surfaces (negative when overlapping) |
| `contactX`, `contactY` | the point where they would touch (halfway between the two surfaces) |
| `nx`, `ny` | unit normal of the contact, from A to B; the tangent is `(ny, -nx)` |
| `relVx`, `relVy` | velocity of B's contact point as seen from A's, spin included |
| `normalSpeed`, `tangentSpeed` | those velocities along the normal (negative: approaching) and along the tangent, before the hit |
| `closingSpeed` | `-normalSpeed`: how fast the gap closes (negative: moving apart) |
| `timeToContact` | seconds until the gap closes at the present closing speed (0 if touching, -1 if the gap does not close) |
| `massA`, `massB` | masses (0 for a static body) |
| `restitution` | the value used |
| `friction` | the combined friction coefficient, `sqrt(frictionA * frictionB)` |
| `impulse`, `tangentImpulse` | size of the normal impulse of the hit; the friction impulse along the tangent (0 when nothing collides) |
| `aVx`, `aVy`, `bVx`, `bVy` | linear velocities after the hit (the present velocities when nothing collides) |
| `aAngVel`, `bAngVel` | angular velocities after the hit, radians per second (the present ones when nothing collides) |
| `energyLost` | kinetic energy, linear and spin, the hit takes out of the pair (what turns into damage, sound and heat) |

**Angles after the collision.** Every `aVx, aVy` / `bVx, bVy` / `aAngVel` / `bAngVel` in the table is a value *after* the hit, so the post-collision angles come straight from them: the direction A leaves in is `math.atan2(t.aVy, t.aVx)` and B's is `math.atan2(t.bVy, t.bVx)`; the angle a body will have turned to `dt` seconds later is about `getAngle(name) + t.aAngVel * dt` (spin is radians per second); and `t.aAngVel` itself is the post-collision rotation rate. The `relVx, relVy`, `normalSpeed` and `tangentSpeed` fields are the values *before* the hit.

**Speed.** `getVelocity(name)` returns `speed, vx, vy` in every game: the first value is the speed (as Rio's own function gives it) and the next two are the velocity components, so `local speed, vx, vy = getVelocity("bird")`. The speed of anything else is `math.sqrt(vx^2 + vy^2)`; after a predicted hit that is `math.sqrt(t.aVx^2 + t.aVy^2)` for A and `math.sqrt(t.bVx^2 + t.bVy^2)` for B. `objects.world[name].xVel` / `.yVel` hold the same components for every object (not only birds) in Classic, Seasons, Rio and the other games, where the retail exes only filled them in for birds. Angry Birds Space follows its own exe: it fills them (and `angle`) for birds and for any object the script flagged with `setRecordVelocity(name, true)`, which the Space scripts do themselves.

**Angles.** The table gives velocities and the normal rather than angles, because every angle follows from them with `atan2`. The direction an object moves in is `math.atan2(vy, vx)` (y points down in this game, so the angle runs clockwise on screen): use `relVx, relVy`, or an object's own velocity from `getVelocity`, before the hit and `aVx, aVy` / `bVx, bVy` after it. The direction of the contact is `math.atan2(ny, nx)`. The angle of incidence is the angle between the approach direction and the normal, `math.acos(closingSpeed / math.sqrt(relVx^2 + relVy^2))`, and how much an object is turned by the hit is `math.atan2(aVy, aVx) - math.atan2(avy, avx)` (wrap the result into -pi..pi). Spin is already an angle rate: `aAngVel` and `bAngVel`.

### Gravity of one object: `setObjectGravity`, `setObjectGravityScale`, `resetObjectGravity`, `getObjectGravity`

An object can have a gravity that is independent of the world's. This is not a force towards another body and does not explode: it is the same constant acceleration the world's gravity is, only with a value (or a direction) chosen for this one object.

- `setObjectGravity(name, gx, gy)` gives the object its own gravity: it accelerates by `(gx, gy)` world units per second squared instead of by the world's. The axes are the world's (the ones `getVelocity`, `setVelocity` and `setPosition` use). `(0, 0)` is weightless; the world's own value changes nothing; the opposite of the world's value turns the world upside down for this object (the ground becomes the sky and it falls the other way while everything else still falls the usual way); `(gx, 0)` is a wall that pulls sideways. Gravitation sensors still act on top of it, as they do on any body. A sleeping object is woken so it starts moving at once. Static bodies are not affected. Returns `true`, or `false` when there is no such object. The setting stays until `resetObjectGravity`.
- `setObjectGravityScale(name, scale)` gives the object the world's gravity multiplied by `scale` and keeps following the world if that changes. 1 is normal, 0 weightless, 0.2 floaty, 3 heavy, -1 exactly upside down. In a world whose gravity is 0 (Angry Birds Space) it changes nothing; use `setObjectGravity` there.
- `resetObjectGravity(name)` takes the object back to the world's gravity (undoing both calls above). Its speed stays as it is.
- `getObjectGravity(name)` returns `gx, gy, mode, scale`: the acceleration the object has from gravity now, where it comes from (`"world"`, `"vector"` or `"scale"`) and the factor of the last scale call (1 otherwise); `nil` when there is no such object.

Technically Box2D 2.2 applies the world's gravity to every body and has no per-body gravity, so the engine applies the difference as a force, `mass * (own - world)`, through the centre of mass each step: with the world's gravity the body then accelerates by exactly its own value and does not start to spin.

### Transparency, draw order and colliders

- `setObjectAlpha(name, alpha)` / `getObjectAlpha(name)`: the transparency of one object when `drawGameNative` draws it, 0 (invisible) to 1 (solid). `getObjectAlpha` returns -1 for an unknown object.
- `setObjectZOrder(name, z)` / `getObjectZOrder(name)`: the object's draw order in `drawGameNative` (the `z_order` of `createBlock`; higher is in front). `objects.world[name].z_order` always shows the current value. `getObjectZOrder` returns `nil` for an unknown object.
- `bringObjectToFront(name)` / `sendObjectToBack(name)`: move the object in front of / behind every other object and return its new `z_order`.
- `getObjectDrawOrder()`: the names of the objects `drawGameNative` draws, from the back to the front.
- `getRenderZ()` / `setRenderZ(z)`: the z of the render state on its own.
- `setColliderType(name, type)` / `getColliderType(name)` (-1: no such object) and `setColliderRule(typeA, typeB, collides)` / `getColliderRule(typeA, typeB)`: sort objects into collider types and decide which types collide with which.
- `setPolygonRadius(r)` / `getPolygonRadius()`: the skin of the boxes and polygons created afterwards (Angry Birds Rio's Box2D 2.2 setting).

See "Extra script commands (new in 1.8)" above for worked examples of the last three groups.

### `rewind([frames])`

Puts every physics object back `frames` frames (1 when left out or nil): position, angle, speed and spin, as they were before that frame's update. The engine keeps the last 600 frames; a new level starts with no history. Objects made since then stay where they are, destroyed ones do not come back, and the scripts' own tables (score, birds left) are not touched. Returns the number of frames it went back (0 when there is no history). The console has the same: `rewind [frames]`.

```lua
-- go back half a second (30 frames at 60 updates a second) when R is pressed
if keyPressed["R"] then rewind(30) end
```

### The history and the `motion` library (3.3)

`rewind(frames)` jumps. Three natives give a script the history itself, and the `motion` library (`src/lua/motion.lua`, loaded into every game before its scripts, so it works everywhere) uses them to turn the rewind, and any other movement, into something you shape yourself.

| function | what |
|---|---|
| `getRewindDepth()` | how many frames `rewind` can go back now |
| `getRewindState(back)` | a table `name -> {x, y, angle, vx, vy, w, awake}` for every physics object `back` frames ago (0 = now); `nil` when there is no such frame |
| `setRewindLimit(frames)` | how many frames are kept (default 600; a longer history costs memory, 0 turns it off) |

**Moving an object: `motion.to(names, props)`.** `names` is an object name, a table of names, or a function `function(name, obj) return true/false end` that picks them. `props`:

| prop | meaning |
|---|---|
| `x`, `y`, `angle` | where it ends (physics units, radians); `dx`, `dy`, `dangle` instead: how far it goes from where it is |
| `from = {x=, y=, angle=}` | where it starts (default: where it is) |
| `time`, `delay` | seconds (default 1 and 0) |
| `ease`, `easeX`, `easeY`, `easeAngle` | the tweening function, see below; the last three override `ease` for one axis |
| `points = {{x, y [, angle]}, ...}` | follow a smooth (Catmull-Rom) line through these points |
| `path = function(p, obj) return x, y [, angle] end` | **any movement**: `p` is the eased progress (0 to 1, more or less with an elastic or back easing), `obj` is `objects.world[name]`; circles, sine waves, orbits around another object, anything |
| `loop`, `yoyo` | `true` or a number of repeats; `yoyo` makes every second run go backwards |
| `release` | what happens to the speed at the end: `"stop"` (default), `"keep"` (the speed it had before), `"throw"` (the speed it was moving with at the end, so a tween can throw an object) or `{vx, vy}` |
| `realTime` | count real seconds, so slow motion does not slow the movement |
| `onUpdate(p, name)`, `onDone(name)` | every frame; at the end (not when the move was stopped) |

While a job runs the object's speed is set to 0 every frame, so physics does not fight the move; other objects still collide with it. A new job on an object replaces its old one. `motion.drive(names, function(t, dt, obj) return x, y, angle end [, opts])` calls your function every frame (return `false` to end), for movement that is not a function of time: homing, following the mouse, steering. `motion.teleport(name, x, y [, angle [, keepSpeed]])` is the instant move. `motion.stop(nameOrJob [, release])`, `motion.stopAll()`, `motion.isMoving(name)` end and ask. Jobs end when a level loads.

**Easing functions.** `ease` is a name from `motion.easing`, a function `function(t) return progress end` (t from 0 to 1), a table `{x1, y1, x2, y2}` (a CSS `cubic-bezier`), or nothing (linear). Names: `linear` and, for each of `Quad`, `Cubic`, `Quart`, `Quint`, `Sine`, `Expo`, `Circ`, `Back`, `Elastic`, `Bounce`, the three forms `in...`, `out...`, `inOut...` (`outBounce`, `inOutBack`, `inQuad`). More makers: `motion.easing.steps(n)`, `motion.easing.bezier(x1, y1, x2, y2)`, `motion.easing.spring(stiffness, damping)`. Your own: `motion.easing.wobble = function(t) return t + math.sin(t * 20) * 0.05 end`, then `ease = "wobble"`.

**The rewind as a movement: `motion.rewind(frames [, opts])`.** The objects go back along the recorded history over `opts.time` seconds (0.5), and the easing decides how fast the history goes by: `"inOutQuad"` is a soft rewind, `"outBounce"` bounces against the old state, a function that goes 0 to 1 and back to 0.5 plays the history there and back. `opts.names` limits it to some objects (same forms as above, or use `motion.rewindObject(name, frames, opts)`), `opts.from` and `opts.to` choose the history frames it runs between (0 = now; a `to` smaller than `from` plays the history **forward** again), `opts.release` is `"keep"` by default (the speed the object had in the history frame it ends on), `opts.delay`, `opts.onDone`, `opts.realTime`. It returns the job, or `nil` when there is no history. The part of the history that is used is copied when it starts, so the frames recorded during the movement do not change it.

```lua
-- R: everything goes back one second and bounces; T: only the selected bird goes back, soft
if keyPressed["R"] then motion.rewind(60, {time = 1, ease = "outBounce"}) end
if keyPressed["T"] then motion.rewindObject(selectedBird.name, 60, {time = 0.6, ease = "inOutCubic"}) end
-- a block that floats up and down forever, and a pig that is thrown along a tween
motion.to("WoodBlock6_1", {dy = -3, time = 2, ease = "inOutSine", yoyo = true, loop = true})
motion.to("SmallPiglette_7", {x = 20, y = -10, time = 0.4, ease = "outQuad", release = "throw"})
-- a block that circles another object
motion.drive("WoodBlock2_1", function(t) local c = objects.world.ground return c.x + math.cos(t) * 6, c.y - 8 + math.sin(t) * 6 end)
```

### `onFileDropped(path)` and the `fileDropped` event

A file dropped on the window calls `engine.on("fileDropped", fn)` handlers with its path, then the script function `onFileDropped(path)` if there is one; returning `true` from it ends there. Without it a level file (a `.lua` with a `world` table) loads as the current level through the game's `loadLevelInternal`, and any other `.lua` runs among the game's scripts. The console command `drop <file>` does the same as dropping the file.

### How the Space physics now follows the retail exe

- The gravitation sensors use one function for the real physics and for the aiming aid, so the two cannot disagree. Like the exe, a sensor's pull ignores the "gravity on" parameter (22), wakes a sleeping body inside the field every step, and is skipped only while the sensor's own body sleeps.
- The aiming aid (`updateBirdTrajectoryTable`) now follows what the exe does: it flies a copy of the bird on its own with the free-flight integrator (world gravity plus the sensors' force times the inverse mass, the velocity damping clamp `1 - h * damping` between 0 and 1, the maximum translation of 2 per step), without collisions, and stores a point every few steps.
- Box2D's block-solver condition number is 1000 for Space and 100 for Rio, as in each exe.

## Native function reference

Every function the engine registers for the scripts, with its arguments, what it returns and what it does. They are all in `src/lua/lua_api.cpp` (the `res` and `particles` tables and the platform services come from the same file) or, for the few that exist only to keep a game's scripts running, in `src/lua/space_natives.lua`. The names are the retail exes' names, so the original scripts call them unchanged.

**How to read the signatures**

- *Arguments are positional.* A number that is missing reads as `0`, a string that is missing as `""`, a boolean that is missing as `false`; an argument shown in `[brackets]` can be left out.
- *Units.* Positions, sizes and velocities of objects are in **world units** (the physics' metres); screen positions and the render state are in **pixels**; angles are **radians** (clockwise on screen, y grows downwards); times are in seconds.
- *Names.* An "object" is the name string you gave `createBox`, `createCircle`, `createPolygon` and the other creation functions. A function that takes a name and gets an unknown one does nothing (or answers `nil` / `0` / `false`, as noted).
- *Game.* Where a function only makes sense in one game it says so; "all games" is the default. `Rio` is Angry Birds Rio, `Space` Angry Birds Space, `SW` Angry Birds Star Wars.
- *Every one of them can be replaced from Lua*, see [Changing the engine from Lua](#changing-the-engine-from-lua) at the end of this chapter.

### Window, input and the engine's own state

| Function | What it does |
|---|---|
| `requestExit()` | Asks the engine to close the window at the end of the frame. |
| `print(...)` | Writes its arguments, separated by tabs, to the console window and the log. Strings and numbers are written as they are, booleans as `true` / `false`, any other value as its type name. (The standard Lua `print` is not used; scripts call this one, or `_G.print`.) |
| `printGlobals()` | Writes the name and type of every global the scripts have, one per line. |
| `throwException(text)` | Writes `script exception: <text>`; the engine does not stop. |
| `setBGColor(r, g, b)` | The colour (0 to 255) the window is cleared with before each frame. `getBGColor()` returns `r, g, b`. |
| `captureMouse(on)` / `isMouseCaptured()` | Keeps the mouse inside the window (for games that play with relative mouse movement). |
| `setFullScreenMode(on)` / `isInFullScreenMode()` | Switches between window and full screen / tells which one it is. |
| `setResolution(width, height)` | Resizes the window. The game is told through its `resolutionChanged` callback. `getFullScreenWidth()` and `getFullScreenHeight()` answer with the `screenWidth` / `screenHeight` globals (0 when unknown). |
| `setRetinaResolution(...)`, `enableSmoothZooming(...)`, `setGameParameters(...)` | Accepted and ignored (device settings of the phone versions). |
| `setIsMultitouchMouseWheelSimulationEnabled(on)` | With `on` the mouse wheel simulates a two-finger pinch zoom. `doesMouseClickSetsTouchCount()` tells whether a click raises the `touchcount` global (it does). |
| `setEditing(on)`, `setGameOn(on)` | Tell the engine the level editor / the game is running; the engine uses them to decide what it draws and which keys it listens to. |
| `getOSName()`, `getOSVersion()`, `getModel()`, `getManufacturer()` | `"Windows"`, `"6.1"`, `"PC"`, `"PC"`. `getHardwareModel()` returns the `deviceModel` of `config.lua` (`"windows"` by default). `getCurrentLocale()` returns `"en"`; `initLocales()` does nothing. |
| `getDeviceID()` | A string made from the machine's network adapters, adapters separated by `;`. The registration screens of the older games keep it in the save to notice a copied save. `verifyDeviceID(saved)` is true when any adapter of `saved` is also one of this machine's; `areDeviceIDsEqual(a, b)` compares two ids. |
| `getCurrentTime()` | `{year, month, day, hour, min, sec}` of the local clock. |
| `getTimeDifference(later, earlier)` | `{days, hours, minutes, seconds}` between two such tables. `getTImeDifferenceInSeconds(later, earlier)` (sic) answers the same in seconds. |
| `getClipboardText()` | An empty string. |
| `canOpenProgram(name)` / `openProgram(name)` | `false` / nothing. |
| `playVideo(path)`, `stopVideo()`, `isVideoPlaying()` | Plays an `.h264` cutscene full screen; scripts poll `isVideoPlaying()` for the end. |
| `performBitwiseOr(a, b)` (SW) | `(int)a | (int)b`. `performBitwiseAnd(a, b)` is the same for AND. |
| `isGameOverlayActive()` (SW) | Always `false`. |
| `clearScreen()`, `refreshCurrentLocale()`, `recoverRenderObjects()` (SW) | Accepted and ignored (they exist in the retail table). |

Example: a script that toggles full screen with F11 and prints what it did.

```lua
if keyPressed[122] then          -- F11
    setFullScreenMode(not isInFullScreenMode())
    print("full screen:", isInFullScreenMode())
end
```

### Loading and saving files

| Function | What it does |
|---|---|
| `loadLuaFile(path, name [, ...])` | Reads a script (plain, encrypted or compiled, the engine tells them apart) and runs it. With a `name` the file runs in a table of its own whose missing names are looked up in the globals, and the table becomes the global `name` (`loadLuaFile("scripts/cutscenes.lua", "cutscenes")` leaves a `cutscenes` table). With `""` it runs among the globals. A file that cannot be read or has an error prints `loadLuaFileToObject failed: <path>: <error>` and the call returns. |
| `loadLuaFileToObject(path, object, name)` | The same, but the table is stored as `object[name]` and falls back to `object` for names it does not define (the Rio and 2.0.0 menus use this). With `name = ""` the file runs inside `object` itself. |
| `loadLevel(path)` | Runs a level file in an empty environment and makes that table the global `loadedObjects` (it stays `nil` for a level that cannot be read). Raises the `levelLoaded` hook. `loadLevelEditor(path)` does the same for the editor (plain text copy); `loadLevelFile(path, date)` and `getLoadStatus()` are the Seasons daily-level calls: the level is local, so `getLoadStatus()` is `{status = 1}` at once. |
| `saveLevel(path)` | Writes the live level (the positions and angles objects have now) as a level file. `saveLevelForReload(name)` (Space) does that and also keeps it as `objects.previousLevel` for the return from a bonus level. |
| `saveLuaFile(name, tableName, persistent)` | Writes the global table `tableName` as `<tableName> = { ... }` to the file `name` in the user's save folder (`settings.lua`, `highscores.lua`). Saves are encrypted for the games that encrypted them; the file is written next to a `.tmp` first and the old one is kept as `.bak`. `savePersistentLuaFile(name, tableName)` does it for the persistent files. |
| `checkForLuaFile(path)` | True when a script of the game (or the save folder) exists. `fileExists(path)` (all games, 2.4) is the general version: a file next to the exe or in the save folder. |
| `checkDirectory(path)` / `createDirectory(path)` | Tests for / makes a folder in the save folder. |
| `readDataFile(path)` | The whole file as a string (decrypted like a script), or `nil`. `loadTextFileToString(path)` (SW) does the same; `fileExistsInAppData(path)` (SW) tests it. |
| `checkForPersistentFile(name)`, `loadPersistentFileWrapper(name)` | The Island HD saved world: tests for / loads `name` from the save folder and returns the table it defines. |

Example: keep your own table across runs.

```lua
myMod = myMod or {runs = 0}
if checkForLuaFile("mymod.lua") then loadLuaFile("mymod.lua", "") end
myMod.runs = myMod.runs + 1
saveLuaFile("mymod.lua", "myMod", true)
```

### Creating and removing objects

All of these write the object into `objects.world[name]` (and `objects.joints` for joints), so a script sees it at once.

| Function | What it does |
|---|---|
| `createBox(name, sprite, x, y, width, height, density, friction, restitution, collision, controllable, zOrder)` | A rectangle centred on `x, y`. `collision` (boolean, default true) lets other objects hit it, `controllable` marks a bird, `zOrder` is the draw order (higher is in front). |
| `createCircle(name, sprite, x, y, radius, density, friction, restitution, [collision,] controllable, zOrder)` | A circle. `collision` is only present in Rio and Space calls (11 arguments). |
| `createPolygon(name, sprite, x, y, width, height, density, friction, restitution, collision, controllable, zOrder)` | A polygon from the corners you gave with `clearVertices()` then `addVertex(x, y)`, relative to `x, y`. Since 3.4 the outline may be concave (a star, an L, a chevron) and have any number of points: Box2D only takes convex pieces of 8 points, so a concave outline is split into the fewest convex pieces (ear clipping, then merging), one fixture each on the one body; a convex outline of up to 8 points is made as before. The same goes for the polygon parts of compound objects. `createLineShape` (SW) takes the same arguments and builds a chain of edges from the vertices. |
| `clearVertices()`, `addVertex(x, y)` | Collect the corners for the next `createPolygon` / `createLineShape`. |
| `clearShapesArray()`, `addBoxToCompoundList(...)`, `addPolygonToCompoundList(...)`, `addCircleToCompoundList(...)`, `addLineToCompoundList(...)`, `setCompoundShapeLinearDamping(shapes, damping)`, `setCompoundShapeParameter(shapes, id, value)`, `createCompoundObject(name, x, y, _, density, friction, restitution, collision, controllable, zOrder)`, `finalizeCompoundObject()` | Build one body of several shapes: clear the list, add the parts, then create. `setCompoundShapeParameter` takes the ids of `setObjectParameter` and applies them when the body is made. |
| `createCompoundBodyFromTable(t)` (Space) | The same from a table: `name, x, y, angle, z_order, material` and `children` (one table per part with `x, y, angle` and `blockData`). |
| `setupCompoObject(name, items, scale)` | Makes an object draw as a composite: `items = {{sprite=, x=, y=, scale=, angle=}, ...}`. |
| `removeObject(name)` | Destroys the body and its joints and removes it from `objects.world` and `objects.joints`. Raises the `objectRemoved` hook first, so a handler can still read the object. |
| `setSprite(name, sprite)`, `setMaterial(name, material)`, `setTexture(name, texture)` | Change what an object is drawn with. |
| `setObjectDensity(name, density)`, `setRestitution(name, value)` | Change the body's density / bounciness (all its fixtures). `native_setMass(name, density)` is `setObjectDensity`. |
| `spriteExists(name)` | True when a sprite of that name is loaded. |

Example: drop a crate and remove it after two seconds.

```lua
createBox("crate1", "BLOCK_WOOD_1", 10, -5, 2, 2, 1, 0.5, 0.2, true, false, 5)
engine.on("update", function(dt)
    crateTimer = (crateTimer or 0) + dt
    if crateTimer > 2 and objects.world.crate1 then removeObject("crate1") end
end)
```

### Reading and moving objects

| Function | What it does |
|---|---|
| `setPosition(name, x, y)`, `setRotation(name, angle)` | Teleports / turns the body and updates `objects.world[name].x, .y, .angle`. |
| `setVelocity(name, vx, vy)`, `setAngularVelocity(name, w)` | Sets the speed. `getAngularVelocity(name)` returns it; `multiplyVelocity(name, factor)` scales the current velocity. |
| `getVelocity(name)` | `speed, vx, vy` (0, 0, 0 for a sleeping or unknown body). `getLinearVelocity(name)` (Space) returns `vx, vy`. |
| `getAngle(name)` | The body's angle. |
| `getLocalPoint(name, worldX, worldY)` / `getWorldPoint(name, localX, localY)` | Convert a point between the world and the body's own coordinates; each returns `x, y`. |
| `applyImpulse(name, ix, iy, px, py)` | An instant push `ix, iy` at the world point `px, py`. `applyForce(name, fx, fy, px, py)` (and `applyForceNative`) is a force for this step. |
| `setSleeping(name, sleeping)` | Puts a body to sleep or wakes it. `setActive(name, active)` takes it out of the simulation and back in. |
| `setFixedRotation(name, on)`, `setLinearDamping(name, d)`, `setAngularDamping(name, d)` | Box2D body settings. |
| `setAsSensor(name, on)` | Makes the body a sensor: it reports contacts but nothing collides with it. |
| `setCollisionEnabled(name, on)` | Turns the object's collisions on and off. |
| `setSpeedCap(name, maxSpeed [, maxVx, maxVy])`, `getSpeedCap(name)`, `clearSpeedCap(name)` (2.4) | A limit the object's velocity is brought back under after every physics step (0: no limit). |
| `setObjectGravity(name, gx, gy)`, `setObjectGravityScale(name, scale)`, `resetObjectGravity(name)`, `getObjectGravity(name)` (2.4) | Gravity of one object. See "Gravity of one object" above. |
| `getTouchingObjects(name)` (2.4) | `{names}` the body touches right now. |
| `getIntersectingObjects{x1, y1, x2, y2}` / `getIntersectingObjects(box)` | The objects whose shapes overlap a box (Seasons passes the sizes of the sides, Rio / Space signed offsets). Used by explosions. |
| `rayCast(x1, y1, x2, y2 [, all])` (2.4) | The nearest object the segment crosses: `name, hitX, hitY, normalX, normalY, fraction`, or `nil`; with `all = true` a list of such tables, nearest first. `getRayCastedObjects{x1=, y1=, x2=, y2=}` (Space) returns a flat list, six values per object. |
| `hasMovingObjects()` / `hasMovingObjectsZeroTolerance()` | True while any object (other than those flagged with parameter 12 in SW) moves faster than 0.5 / 0.001. The game waits for this to be false before it judges a level. |
| `setObjectAlpha(name, a)` / `getObjectAlpha(name)` | Transparency (0 invisible, 1 solid) used by `drawGameNative`; `getObjectAlpha` is -1 for an unknown object. |
| `setObjectZOrder(name, z)`, `getObjectZOrder(name)`, `bringObjectToFront(name)`, `sendObjectToBack(name)`, `getObjectDrawOrder()` | The draw order of `drawGameNative`. |
| `setColliderType(name, type)`, `getColliderType(name)`, `setColliderRule(typeA, typeB, collides)`, `getColliderRule(typeA, typeB)` | Collider classes: two objects collide only when the rule for their types allows it. |
| `setPolygonRadius(r)` / `getPolygonRadius()` | The skin radius of boxes and polygons made afterwards (Rio's Box2D 2.2). |
| `setScale(name, sx [, sy])`, `getScale(name)` | The scale an object's sprite is drawn with (the shape does not change). |
| `setSpriteRotation(name, angle)`, `setPivotOffset(name, x, y)`, `setTextureScale(name, s)`, `setRecordVelocity(name, on)`, `setObjectDecoration(name, sprite, amount, angleIncrement, scale)` (Space) | Sprite look settings the Space scripts use. |
| `setObjectParameter(name, id, value)` | The catch-all for per-object switches. The ids are listed with each game: see the table for SW above; for Seasons, Rio and Space the ids the scripts use are 5 (scale), 6 (use scale), 15 (`disableImmovable`) and the Rio bounce multipliers 6 and 7. |
| `setSensorGravityMask(name, mask)`, `setObjectGravityCategory(name, category)`, `setSensorMinimumAndMaximumForces(name, min, max)`, `setRevertGravity(name, on)`, `setRevertGravityWithMultiplier(name, on, a, b)` (Space, SW) | A gravity sensor pulls only objects whose category bit is in its mask; the forces are clamped to the minimum and maximum; "revert" makes it push instead of pull. |
| `setGravityForceMultiplier(m)`, `getGravityForceMultiplier()` | Scales the force every gravity sensor applies. |
| `setWaterForceMultiplier(m)`, `getWaterForceMultiplier()`, `setBirdWaterDrag(d)`, `setObjectWaterDrag(d)` (SW) | The buoyancy force and the drag in water. |
| `native_setBlockCollisionEnabled`, `native_setIgnoresScore`, `native_setKeepOrientation`, `native_setAffectedByForceMultipliers`, `native_setIsWater`, `native_setWaterDensity`, `native_setControlled`, `native_setTimeSinceCollision` (SW) | Per-object flags, see the table under "Object parameters". |

Example: make every bird that is launched fly at half gravity.

```lua
engine.on("objectCreated", function(name, kind)
    if string.find(name, "BIRD") then setObjectGravityScale(name, 0.5) end
end)
```

### Joints

| Function | What it does |
|---|---|
| `createJoint{name=, end1=, end2=, type=, coordType=, x1=, y1=, x2=, y2=, ...}` | Makes one joint. `type` 1 is a distance (spring) joint (`frequency`, `dampingRatio`, `collideConnected`), the others are revolute / prismatic / pulley / weld (types 6 to 11, which no editor offers, are in "Extra joint types" below); the extra fields depend on the type (`motor`, `motorSpeed`, `maxTorque`, `limit`, `lowerLimit`, `upperLimit`, `breakable`, `breakForce`, `isDrawn`, `jointPosX`, `jointPosY`). It writes `objects.joints[name]`. |
| `createJoints(list)` | `createJoint` for every entry of a level's joint table (an entry without `name` takes its key). |
| `setJointParameters{name=, motor=, motorSpeed=, maxTorque=, limit=, ...}` | Changes the joint's motor and limits. `setRevoluteJointSpeed(name, speed)` sets only the motor speed. |
| `checkJointLimits(name)`, `handleJointLimits(name, on)` | Make a limited joint apply its limits now. |
| `destroyJoint(name)` | Destroys the joint and clears `objects.joints[name]`. `removeJointsFromObject(name)` destroys all joints of an object, `removeBreakableJointsFromObject(name)` (Space) only the breakable ones. |

### Extra joint types: pulley, gear, mouse, line / wheel, friction, rope (3.5)

`createJoint` also takes the Box2D joints that no game editor offers. Types 1 to 5 are unchanged; the new ones are `type` 6 to 11. They are made with the same call, write the same `objects.joints[name]` table (with the values that were really used), are destroyed with `destroyJoint(name)` and with their bodies, and take `collideConnected` (default false). Positions are physics units, `x1, y1` and `x2, y2` are points **local to `end1` and `end2`** (as for a weld or revolute joint).

| `type` | Joint | Fields (defaults) | `setJointParameters` |
|---|---|---|---|
| 6 | **Pulley**: `end1` and `end2` hang from two fixed points; length1 + ratio x length2 stays constant, so one side goes down as the other comes up | `x1, y1, x2, y2` anchors on the bodies; `groundX1, groundY1, groundX2, groundY2` the fixed points in the world (default: 3 units above each anchor, i.e. towards -y); `ratio` (1) | none (fixed at creation) |
| 7 | **Gear**: couples two joints that already exist, which must be revolute (3) or prismatic (4): coordinate1 + ratio x coordinate2 stays constant | `joint1`, `joint2` the names of those joints; `ratio` (1, may be negative) | `ratio` |
| 8 | **Mouse**: a soft spring that pulls `end1`'s centre to a point (it needs no `end2`) | `targetX, targetY` (the body's position); `maxForce` (1000 x the body's mass); `frequency` (5); `dampingRatio` (0.7) | `targetX`, `targetY` (either one alone keeps the other), `maxForce`, `frequency`, `dampingRatio` |
| 9 | **Line** (Box2D 2.1: Classic, Seasons, Space): `end2` slides along an axis fixed in `end1`. **Wheel** (Box2D 2.2: Rio, Star Wars, Friends): the same with a suspension spring along the axis and a motor that turns it | `x1, y1` the anchor on `end1`; `worldAxisX, worldAxisY` the axis in the world (0, 1); line: `limit`, `lowerLimit`, `upperLimit` (0 to 1), `motor`, `motorSpeed`, `maxTorque` (the motor's force, 10000), `backAndForth`; wheel: `motor`, `motorSpeed`, `maxTorque`, `frequency` (2, 0 = no spring), `dampingRatio` (0.7) | `motor`, `motorSpeed`, `maxTorque`, and for the line `limit`, `lowerLimit`, `upperLimit`, for the wheel `frequency`, `dampingRatio` |
| 10 | **Friction**: top-down friction between the two bodies at `x1, y1` | `maxForce` (10), `maxTorque` (10) | `maxForce`, `maxTorque` |
| 11 | **Rope** (every game: Box2D 2.2 has it, and it was back-ported to the bundled 2.1.1): the anchors may come closer but not further apart than `maxLength` | `x1, y1, x2, y2` anchors; `maxLength` (the distance at creation) | none |

Notes. A gear is destroyed when either joint it couples goes (explicitly or because a body was removed), since Box2D would otherwise read a joint that no longer exists. The mouse joint pulls against a hidden fixed body that is made the first time one is created; a body that is not dynamic does not move. `createJoint` returns nothing and makes no joint when a body or a joint name is missing (or, for the gear, when a joint is not revolute or prismatic); check `objects.joints[name]` afterwards. Example, an elevator that drags a block 4 units up and later lets go:

```lua
local o = objects.world["WoodBlock6_3"]
createJoint{ name = "lift", end1 = "WoodBlock6_3", type = 8, targetX = o.x, targetY = o.y - 4 }
-- later: setJointParameters{ name = "lift", targetY = o.y }   or   destroyJoint("lift")
```

### World, physics and camera

| Function | What it does |
|---|---|
| `setWorldGravity(gx, gy)` / `getWorldGravity()` | The world's gravity in units per second squared (y positive is down). |
| `setPhysicsEnabled(on)` / `isPhysicsEnabled()` | Stops / resumes the physics step. |
| `setPhysicsSimulationScale(s)` | Kept for the scripts that call it (Classic sets it to 20 at start); it changes nothing in this engine. For slow motion set the global `physicsSpeedFactor` (physics only) or `gameSpeedFactor` (everything), e.g. `physicsSpeedFactor = 0.3`, and `nil` or 1 for normal speed. |
| `setLevelLimits(left, top, right, bottom)` | The area in which objects live; objects that leave it are removed by the scripts. SW publishes these as `leftLimitWorld` ... |
| `setTopLeft(x, y)` | The world position of the screen's top left corner. |
| `setWorldScale(s)`, `setMaxWorldScale(s)`, `resetMouseWheelScale(s)`, `setMaxTranslation(t)` | The zoom (pixels per world unit), its maximum, the zoom the mouse wheel returns to, and the largest distance the camera moves per frame. |
| `setThemeOffsetY(layer, y)`, `setStartingCameraValue(on)` | Used by the theme system to place the backgrounds (see "Backgrounds" in the README). |
| `setGameRenderingDisabled(b)` / `isGameRenderingDisabled()` (SW) | A flag the scripts keep; the engine does not read it. |
| `getAimingTime()` (Space) | `floor(simulationIterations) * objects.currentTimeStep`: how long the aiming aid simulates. |
| `setDeltaTimeMultiplier(m)` / `getDeltaTimeMultiplier()` (Space) | Stored in `g_deltaTimeMultiplier` for the scripts' own use. |
| `setCameraLimits(w)` (Space) | Stored in `g_cameraLimitWidth`. |

### Drawing

All drawing functions use the **render state** that `setRenderState` set last. `setRenderState(tx, ty, sx, sy, angle, pivotX, pivotY, alpha, skewX, skewY, bendX, bendY, z [, bendFunction])` sets a transform: everything drawn after is scaled by `sx, sy`, turned by `angle` about `pivotX, pivotY` and then moved by `tx, ty` (in the scaled units: the scripts divide a screen position by the scale). Values you leave out keep their defaults (`0, 0, 1, 1, 0, 0, 0, 1, 0, ...`). Reset it with `setRenderState(0, 0, 1, 1, 0, 0, 0, 1)` after drawing.

| Function | What it does |
|---|---|
| `drawRect(r, g, b, a, x1, y1, x2, y2, useRenderState [, pivotX, pivotY])` | A filled rectangle between two corners; colour 0 to 1 (a: 0 to 1; `drawLine2D` and `drawRectLines` take 0 to 255). With `useRenderState` false it is in screen pixels. |
| `drawRectLines(left, top, right, bottom, lineWidth, r, g, b, a)` | The four edges. |
| `drawLine2D(x1, y1, x2, y2, lineWidth, r, g, b, a [, pivotX, pivotY])` | One line. `res.drawTexturedLine2D(sprite, x1, y1, x2, y2, width [, r, g, b, a])` draws a sprite stretched along a line (Leia's beam). |
| `drawBoxNative(box, x, y, width, height [, vAnchor, hAnchor])` | A nine-patch box: `box` names the corner and edge sprites (`topLeft`, `top`, ...), used by the menus. |
| `drawUITextNative(text, x, y [, scaleX, scaleY [, angle [, scale]]])` | Draws a `Text` item of the UI components (`text.draw`). |
| `clipText(font, text, width)` | Breaks a text into lines that fit `width` pixels; the result is in the global `clippedText` (lines and the widest one). |
| `drawGameNative()` | Draws every object of `objects.world` in z order with their sprites. |
| `drawBackgroundNative(layerIndex)`, `drawForegroundNative()` | Draw the theme's background / foreground layers (`bgLayers` / `fgLayers` of the theme). |
| `drawParticlesNative()`, `drawParticlesFiltered(layer)`, `drawLevelParticlesNative(layer)`, `drawMenuParticlesNative()` | Draw the particles of a list. `layer` 1 is behind the blocks, 2 in front of the birds, 3 the notifications. |
| `updateBackgroundAndForegroundScrollingNative(dt, ...)`, `rotateThemeSprites(dt)`, `updateThemeSprite(dt)` | Advance the scrolling and rotating theme layers. |
| `createThemeSprite(...)`, `createThemeAnimation{...}`, `modifyThemeSprite(...)`, `removeThemeSprite(name, layer)`, `setThemeSprite(name, sprite, layer)` | The scattered decorations of a theme layer (clouds, stars). `createThemeAnimation` takes `layer, x, y, scaleX, scaleY, angle, name, spriteName, velX, velY, scaleSpeed, startAnimTimer, animDelay, startingDelay, isAnimation, bLoop, animation = {frame sprites}`. |
| `setTheme(name)` | Selects the theme (its textures, layers and sounds) for the level. |
| `drawTexturedRect`, `drawSome`, `drawLayer` | Accepted and ignored. |
| `drawString3D(group, text, x, y, z, tilt, turn, depth, alpha)` (SW) | One line of the credits crawl, drawn on a tilted plane. |
| `renderGravityVisualsNative(sensor, x, y, zoom)` (SW) | See the note below. |
| `startNewTrajectory()`, `addToTrajectory(index, x, y)`, `addPuffToTrajectory(index, x, y)`, `setCustomTrajectorySprites(dot, ...)`, `setNormalTrailSprite(sprite)`, `setSpecialTrailSprite(sprite)` | The dots drawn behind a flying bird. |
| `enableAimingAid(on)`, `setAimingAidSprite(sprite, scale)`, `populateAimingAid()`, `clearAimingAid()`, `updateBirdTrajectoryTable([name])`, `simulateAimingAid(bird, impulseX, impulseY [, points, startX, startY, forceX, forceY])` (Space, SW, Classic with `extras`; `forceX, forceY`: a force applied before every simulated step, like a script's `applyForce` each frame) | The aiming aid: `updateBirdTrajectoryTable` fills the path from the simulation, `populateAimingAid` lays out the dots, `clearAimingAid` removes them. The speed and spacing of the dots are the globals `simulationAimSpeed` and `simulationAimSpawnTime`. Since 3.3 the global `aimGravityZones = { {x = , y = , r = , gx = , gy = } }` (round areas with their own gravity, for a script's sensor zone) bends the arc inside them. |
| `drawSlingScopeNative(vx, vy, flyVerticalForce)` | The Sling Scope power-up's path: the aiming aid's simulation of the current bird launched from the band with that velocity and lift; shown while the game keeps calling it every frame. |
| `res.getClipRect()` | The clip rectangle set by `res.setClipRect` (x, y, w, h), or the whole screen. |
| `setAlpha(a [, target])`, `setBlendMode(mode [, target])`, `setShader(effect [, target])`, `setTint(r [, g, b] [, target])`, `getTint([target])`, `resetTint([target])`, `setTintChannel(channel, value [, target])`, `setSkew(x, y [, target])`, `setBend(x, y [, target])`, `setRenderZ(z [, target])`, `setBlur(amount [, target])`, `setPixelate(size [, target])`, `resetBlur`, `resetPixelate`, `setVisualRotation(angle [, target]), `createShader(name, source)`, `setShaderParam(name, slot, x, y, z, w)`, `getShaderError(name)`, `listShaders()`, `setOffset(dx, dy, target)`, `getOffset(target)`, `setLayoutScale(sx [, sy] [, px, py], target)`, `setMirror(on, target)`, `resetRenderOverrides(target)` (2.2 / 2.4 / 3.3) | Single values of the render state with their own functions; with a `target` (an object, sprite or sprite group name) the setting is kept for that target and every later draw of it uses it. See "Graphics settings for one target" above. |

**Pivot arguments (3.1).** `drawRect`, `drawLine2D`, `res.drawSprite`, `res.drawTexturedLine2D` (after its colour) and `res.drawString` accept two optional last arguments, `pivotX, pivotY`, a point in the same coordinates as the drawing. When both are numbers the scale (`sx, sy`) of the render state grows from that point: the point stays where it is and the rest of the picture spreads from it. If either one is `nil` or missing the call works as before (the scale grows from the origin of the render state). Without a scale other than 1 it changes nothing. Example: `setRenderState(0, 0, 2, 2, 0); drawRect(0, 200, 0, 150, 300, 300, 400, 400, true, 300, 300)` fills 300..500 on both axes, where without the pivot it would fill 600..800.


Note on `renderGravityVisualsNative`: the retail routine (0x415f90) reads four numbers, `sensor, x, y, zoom`. The game's script passes three, and the retail exe stops with an "Excepted type number at stack position 4" error for the missing fourth, so the retail game draws no rings from it; the static ring of a gravity field is drawn by the script's own `drawGravityBubble`. The engine does the same (the missing zoom reads as 0). A script of your own can pass a zoom and gets the rings.

### The `res` table (sprites, text, fonts and sound)

Reached as `res` or `_G.res`. Sprites live in *sheets* (`createSpriteSheet`); a function that takes a sprite also accepts the sheet's name in front (`res.drawSprite("SHEET", "SPRITE", x, y)`) and `nil` or `""` as "any sheet".

| Function | What it does |
|---|---|
| `setPath(folder)` | The folder the other `res` functions read from (`imagePath`, `audioPath`, ...). |
| `createSpriteSheet(file)` / `releaseSpriteSheet(file)` | Loads / frees a sprite sheet (the `.dat` and its picture). |
| `createCompoSpriteSet(file)` / `releaseCompoSpriteSet(file)` | The same for a file of composite sprites. |
| `drawSprite(sprite, x, y [, vAnchor, hAnchor [, width, height [, pivotX, pivotY]]])` | Draws a sprite at `x, y` using the render state. `vAnchor` is `"TOP"`, `"VCENTER"`, `"BOTTOM"` or `"VPIVOT"`, `hAnchor` `"LEFT"`, `"HCENTER"`, `"RIGHT"` or `"HPIVOT"` (the sprite's own pivot). `drawCompoSprite` is the same name for composites. |
| `getSpriteBounds(sprite)` | `width, height` in pixels (0, 0 when not loaded). `getSpritePivot(sprite)` returns `pivotX, pivotY`; `getCompoSpriteBounds(sprite)` returns `x1, y1, x2, y2`. |
| `getSpritePixel(sprite, x, y)` (2.4) | `r, g, b, a` of one pixel, or `nil`. |
| `getCompoSprites(name [, "all"])` (2.4) | The parts a composite is made of now (`{sprite, x, y, scale, angle}` each), or every sprite it uses. |
| `captureSprite(name)`, `keepLastFrame(on)` | Copies the finished frame into a sprite called `name` (the screen-wipe uses `CAPTURED_SCREENSHOT`). |
| `createBitmapFont(file)` / `useFont(name)` | Loads a font / makes it the current one. `getStringWidth(text)`, `getFontHeight()`, `getFontLeading()`, `getFontTracking()`, `getFontMaxAscending()`, `getFontMaxDescending()` measure the current font. |
| `drawString(group, textOrKey, x, y, vAnchor, hAnchor [, pivotX, pivotY])` | Draws text; with a `group` it looks the key up in that text group first. |
| `setClipRect(x, y, w, h)` | Limits drawing to a rectangle (screen pixels). |
| `createTextGroupSet(file)`, `loadLocale(file, name)`, `useLocale(name)`, `getString(group, key)`, `setString(group, key, text)` | The texts of the game: load a `TEXTS_*.dat`, choose the language, read or replace one text. |
| `createAudio(name, file)`, `playAudio(name [, volume, loop, track])`, `stopAudio(name)`, `stopAllAudio()`, `isAudioPlaying(name)` | Sound clips: define one, play it (looping or not, on a numbered track), stop it, ask. |
| `setTrackVolume(volume, track)` (note the order), `getTrackVolume(track)`, `setMasterVolume(v)`, `getMasterVolume()`, `setAudioSpeed(speed [, track])`, `getAudioSpeed([track])`, `pauseAudio([track])`, `resumeAudio([track])`, `isAudioPaused([track])`, `setAudioClipVolume(name, v)`, `setAudioClipSpeed(name, s)` (2.2) | Volume and speed of everything or one track / clip. |
| `setAudioClipPitch`, `setAudioClipTempo`, `setAudioClipPan`, `resetAudioClipEffects`, `seekAudioClip(name, seconds)`, `getAudioClipTime(name)`, `getAudioClipLength(name)` (2.4) | Pitch, tempo, pan and position of a clip; settings made before the clip plays are kept for it. |
| `createAudioOutput(rate, channels, bits)`, `startAudioOutput()`, `stopAudioOutput()` | Raw sound output (the engine ignores them). |
| `openURL(url)` | Opens the address in the browser. |

Example: draw a sprite's name, centred, over every selected object.

```lua
setRenderState(0, 0, 1, 1, 0)
res.useFont(fontBasic)
res.drawString("", "hello", screenWidth * 0.5, 40, "TOP", "HCENTER")
```

### The `particles` table

| Function | What it does |
|---|---|
| `particles.addParticles(type, amount, x, y, w, h, angle [, ignoreLimits])` | Emits `amount` particles of a particle definition (`particleTable`) in the rectangle `x, y, w, h`, turned by `angle`. The soft and hard limits (`setSoftLimit(soft, perFrame)`, `setHardLimit(n)`) cap how many exist; `ignoreLimits` bypasses them. |
| `particles.addLevelParticles(type, amount, x, y, w, h, angle, ignoreLimits)` | The same, into the level's list (drawn by `drawLevelParticlesNative`). |
| `particles.addParticlesWithScale(type, amount, x, y, w, h, angle, scaleBegin, scaleEnd)` (Space) | The call's scales replace the definition's. |
| `particles.addParticlesWithProperties(type, amount, x, y, w, h, angle, minVel, maxVel, minEmitterAngle, maxEmitterAngle)` (Space) | The call's velocities and emitter angles replace the definition's. |
| `particles.native_addParticlesWithMode{definitionName=, amount=, x=, y=, w=, h=, angle=, mode=}` (SW) | The theme-system form (mode 1 foreground, 2 background). |
| `themeSystem.spawnFGLayerParticles(id)` (SW) | Starts the foreground particle layer with that `spawnerId`. |
| `clearParticles()`, `clearParticlesNative()`, `clearParticlesWithTagNative()` | Remove all particles. |
| `clearParticleCache()` | Forgets the particle definitions the engine has read, so the next particles read `particleTable.particles` again (see "Particle tuning"). |
| `setParticleSystemLimits`, `enableInGameParticlesNative`, `updateMenuParticlesNative` | Accepted and ignored. |

### Engine additions

Also new in 2.9 and documented at the end of this chapter: `getCamera`, `getThemeLayers`, `setThemeLayer`, `clearParticleCache`, the `tuning` table and the rule hooks.

Documented in full above: `createSilhouette`, `predictCollision`, gravity of one object, transparency and draw order, colliders; `rayCast`, `getTouchingObjects`, `setSpeedCap`, `setScale`, `fileExists` (2.4).

### Services of a phone or console build (answered with constants)

These are in the original scripts' call lists but have nothing to do on a PC. They exist so the scripts run; they do nothing and return `nil` unless a value is shown.

- Registration and updates: `openRegistrationDialog`, `registerKey`, `requestLatestVersionNumber`, `checkForUpdates`, `callVersionUpdater`, `checkRegistrationResult` (`false`), `getUnlockRequestChecksum` (`""`).
- Crystal, Game Center, leaderboards, achievements: `activateCrystalUI`, `activateCrystalUIAtProfile`, `deactivateCrystalUI`, `showCrystalSplash`, `isCrystalSplashShowing` (`false`), `isCrystalUIShowing` (`false`), `userEnabledCrystal` (`false`), `avoidCrystalBackgroundActivity`, `initGameCenter`, `getLeaderboardScoresForPlayers`, `getLeaderboardScoresForRange`, `showLeaderboards`, `showAchievements`, `postHighscore`, `unlockAchievement`.
- Statistics: `logFlurryEvent`, `logFlurryEventWithParam`, `logFlurryEventWithParams`, `flushMedioEvents`, `logMedioAnonymousIDtoFlurry`, `logMedioEvent`.
- Ads and in-app purchases: `requestAd`, `showAd`, `hideAd`, `requestBannerAd`, `showBannerAd`, `hideBannerAd`, `requestInterstitialAd`, `requestAndShowInterstitialAd`, `showInterstitialAd`, `requestExpandableAd`, `showExpandableAd`, `stopAds`, `requestAdvertisement`... `showAdvertisement`, `hideAdvertisement`, `requestInterstitial`, `showInterstitial`, `requestVideoAd`, `requestAndShowVideo`, `showVideoAdvertisement`, `enableiAds`, `disableiAds`, `iAdsSupported` (`false`), `videoReady` (`false`), `iapBuyItem`, `iapGetItemAt`, `iapGetItemCount`, `iapInitItemPurchase`, `iapInitTimeOut`, `iapIsAllowed` (`false`), `native_isPaymentInitialized` (`false`), `native_initPaymentProvider`, `native_iapRestorePurchases`.
- Device: `goToTaskSwitcherLua`, `toggleNFC`, `setNotificationCallback`, `addNotificationAfter`, `removeNotification`, `hasLocationCapability` (`false`), `isWebViewLoading` (`false`), `native_isCameraSupported`, `native_hasFrontCamera` (`false`), `native_getDeviceOrientation` (`0`), `native_startQrScanner`, `native_stopQrScanner`, `native_setQrRecognizedCallback`, `native_startURLThread`, `getFontScale()` and `getUIScale()` (`1`), `getWindowState()` (`"FULL"`).
- Other SW calls with no PC effect: `toggleZoomSteps`, `setPhysicsScale`, `setFlashAnimation`, `updateThemeParticlesNative`, `loadLevelFromAppData`, `loadLuaFileFromAppDataToObject`, `native_loadTextFileToLuaTable`, `native_setZOrderRange`, `native_setAdditionalBirdGravity`, `native_setWaterColor`, `native_resizeRadius`, `native_onLevelInitComplete`, `native_refreshThemeSystem`, `native_resetThemeSystem`, `native_setThemeLayerOffset`, `native_setThemeLayerScale`, `native_drawSimulationTrajectory`, `native_getLevelLimits` (`0, 0, 0, 0`), `getLocale()` (`"en"`), `decodeBase64(s)`, `getJSONHash`, `GetJSONDiff`, `importJSONToLuaTable`, `setCurrentLevelPack(name)` / `getCurrentLevelPack()` (kept in `g_currentLevelPack`).

---

## Changing the engine from Lua

Everything above is an ordinary global (or a field of `res` / `particles`) in the scripts' environment, so a script can change any of it, and the engine gives you a few tools that make that safe. Nothing here needs a rebuild: put the code in a script of the game (or in the plain-text `dec` folder, see `docs/USAGE.md`) or run it from the console window.

### 1. Replace or wrap a native

Assign to the name. The scripts look the function up by name every time they call it, so the new function is used from then on:

```lua
local original = engine.original.setWorldGravity    -- always the engine's own function
function setWorldGravity(gx, gy)
    original(gx * 0.5, gy * 0.5)                    -- every level now has half the gravity
end
```

`engine.original` holds the engine's own version of every native (`engine.original.print`, `engine.original.createBox`, ...) and `engine.resOriginal` the functions of `res` (`engine.resOriginal.drawSprite`). They do not change when a script replaces the global, so a wrapper can always reach the real thing, and two mods that wrap the same function do not lose each other's code if each one wraps whatever the global is *now*:

```lua
local previous = createBox
function createBox(name, sprite, x, y, w, h, ...)
    return previous(name, sprite, x, y, w * 1.2, h * 1.2, ...)   -- bigger boxes
end
```

`engine.natives([includeRes])` returns the sorted list of native names (with `true`, the `res` functions too, as `"res.drawSprite"`), so you can list, test for or loop over them.

A function the engine calls on the *game's* side (the callbacks `update`, `draw`, `initialize`, `birdCollision`, ... and every function the game scripts define, such as `gamelua.addToTrajectory`) is replaced the same way: wrap the global.

### 2. React to what the engine does: `engine.on`

```lua
engine.on(event, handler)   -- adds a handler and returns it
engine.off(event, handler)  -- removes it (true if it was there)
engine.emit(event, ...)     -- raises one of your own events
```

The engine raises these events (the handlers run in the order they were added; an error in one is printed as `engine.on(<event>): ...` and the others still run):

| Event | Arguments | When |
|---|---|---|
| `"update"` | `dt, realDt` | after every frame's `update`; `dt` is the game time, `realDt` the real time |
| `"levelLoaded"` | `path` | after `loadLevel` has run a level file (the table is `loadedObjects`) |
| `"objectCreated"` | `name, kind` | after `createBox` / `createCircle` / `createPolygon` made an object (`kind` is `"box"`, `"circle"` or `"polygon"`); `objects.world[name]` already exists |
| `"objectRemoved"` | `name` | at the start of `removeObject`, so the object can still be read |

```lua
-- tint every pig red when the level has loaded, and log how many objects there are
engine.on("levelLoaded", function(path)
    print("loaded", path)
end)
engine.on("objectCreated", function(name, kind)
    if string.find(name, "PIG") then setTint(1, 0.4, 0.4, name) end
end)
```

While no handler is registered the hooks cost one table lookup.

### 3. Change numbers the engine reads from Lua

Several engine settings are plain globals that the engine reads when it needs them, so setting the global changes the behaviour at once:

| Global | Effect |
|---|---|
| `simulationAimSpeed`, `simulationAimSpawnTime` | speed and spacing of the aiming-aid dots (defaults 4 and 0.6) |
| `simulationIterations`, `objects.currentTimeStep` | length of the simulated flight of the aiming aid |
| `leftLimitWorld`, `rightLimitWorld`, `topLimitWorld`, `bottomLimitWorld` | the level's limits, read by the SW backgrounds |
| `g_useZeroAsThemeReferencePointX`, `g_useZeroAsThemeReferencePointY`, `g_useLowerCameraAsThemeReferencePoint` | where the SW theme layers take their reference camera |
| `screenWidth`, `screenHeight`, `deviceModel`, `showCursor`, `isLiteVersion`, `isBetaVersion`, `uniqueInstallationId` | what the engine tells the scripts about the window and the install |
| `native` | a table for the platform services of the original exe (the scripts test `native.apprater`); a script may put its own functions in it |

### 4. Replace a game script

- `AngryBirdsEngine.exe --decrypt` writes a game's scripts as plain text into a `dec` folder and `--use-dec` makes the engine read them instead of the game's own copies, so any script of any game can be edited with a text editor (see `docs/USAGE.md`).
- A file called `extra_scripts.lua` next to `gamelogic.lua` is loaded once the Classic scripts are up (see `extras/`); that is the place for a mod that wraps natives and game functions.
- `AB_EXEC=<lua> AB_EXEC_FRAME=<n>` runs a snippet on frame `n`, handy for trying a change before putting it in a file.

### 5. What is not (yet) changeable from Lua

The rules that the original exes compiled in, and that no original script ever reached, are still C++: the collision damage and wobble formulas of the SW, Space and Rio physics, the contact filter, the Box2D step itself, the drawing order inside `drawGameNative` and the particle emitter maths. You can change their *inputs* (the object parameters, `setSpeedCap`, `setWorldGravity`, `setObjectGravity`, colliders, density and restitution), their *results* (wrap `birdCollision`, `blockCollision`, `removeObject`) and *when* they run (`setPhysicsEnabled`, `setPhysicsSimulationScale`), and you can replace the native that triggers them. Anything else you need, tell us in an issue: the plan is to open each of those as a global the engine reads, the way `simulationAimSpeed` already works.

---

## Modding guide: how to change what the engine does

This chapter is the long version of "Changing the engine from Lua". Each recipe has the idea in pseudocode first and the real Lua after it. Run the Lua from a script of the game, from `extra_scripts.lua`, or with `AB_EXEC` while testing.

### The three levers

1. **Replace a native** (`engine.original`): change what a function does.
2. **Listen to an event** (`engine.on`): run your code when the engine does something.
3. **Set a tuning value** (`tuning`): change a number the engine uses inside its own code.

Pick the lever in this order: a tuning value if there is one, an event if you only need to react, a replaced native if you need to change the result.

### Lever 3: the `tuning` table

`tuning` is an empty global table. The engine looks a name up in it each time it needs the number. A name that is not set keeps the built-in value, so an empty table changes nothing, and you can set a value at any time (also in the middle of a level).

| Name | Default | Used by | Effect |
|---|---|---|---|
| `sw.forceDivisor` | 10 | Star Wars collisions (0x42f9d0) | The force of a hit is divided by this. Smaller means more damage everywhere. |
| `sw.homingDamage` | 0.2 | Star Wars birds | Fraction of the damage a HOMING bird does before its first collision. |
| `sw.wobbleFactor` | 0.02 | Star Wars blocks and birds | Wobble amplitude per unit of force. |
| `sw.wobbleMax` | 0.1 | Star Wars blocks and birds | Largest wobble amplitude. |
| `sw.removeLimit` | 0.88 | Star Wars sensors of kind 5 and 7 | They are removed when they pass this fraction of a level limit. |
| `rio.wobbleFactor` | 0.02 | Rio hits | Wobble amplitude per unit of momentum difference. |
| `rio.wobbleMax` | 0.1 | Rio hits | Largest wobble amplitude. |
| `zoom.stepIn` | 0.2 | mouse wheel, zoom of 1 or more | Zoom change per wheel notch. |
| `zoom.stepOut` | 0.1 | mouse wheel, zoom below 1 | Zoom change per wheel notch. |
| `zoom.duration` | 0.5 | mouse wheel | Seconds a zoom change takes. |

**Physics names** (read every physics step, all games; set them as `tuning["water.band"] = 3`):

| Name | Default | Effect |
|---|---|---|
| `solver.velocityIterations`, `solver.positionIterations` | the game's own (10 and 10 in most) | Box2D solver iterations per step. Higher is stiffer and slower. |
| `solver.linearSlop` | 0.05 (Space 0.005) | Allowed overlap before Box2D pushes bodies apart (Box2D 2.2 builds: Rio, Space, Star Wars). |
| `solver.timeToSleep` | 0.25 (Space 0.5) | Seconds a body must be still before it sleeps. |
| `solver.sleepTolerance` | 0.1 (Space 0.5) | How slow counts as still. |
| `water.buoyancy` | 0.1 | Star Wars water: buoyancy per unit of mass and density difference. |
| `water.band` | 1.5 | Depth, in world units, of the top layer where the buoyancy fades out. |
| `water.keepOrientationOffset` | 0.25 | Where an upright-keeping object is pushed from (the righting force). |
| `water.boxPushFactor` | 0.25 | The same for boxes, scaled by their aspect ratio. |
| `water.pushSpeedLimit` | 15 | Objects sinking faster than this many world units per second get no buoyancy push. |
| `gravity.revertFactor` | -0.1 | Strength of a "revert gravity" sensor, relative to a normal one. |

**Hit wobble, flipping and "is anything moving"** (Rio, Star Wars):

| Name | Default | Effect |
|---|---|---|
| `wobble.duration` | 1 | Seconds a hit wobble takes to die out. |
| `wobble.maxScale` | 4 | Largest wobble scale (amplitude times the object's bounce multiplier). |
| `wobble.cutoff` | 0.01 | The wobble stops when it falls below this. |
| `wobble.frequency`, `wobble.baseFrequency` | 100, 5 | How fast the wobble shakes: `time * (amplitude * frequency + object bounce frequency * baseFrequency)`. |
| `flip.speed` | 3 | Objects with "enable flipping" turn to face their direction when they move sideways faster than this. |
| `moving.tolerance` | 0.5 | `hasMovingObjects()` counts an object as moving above this speed. |
| `moving.zeroTolerance` | 0.001 | The same for `hasMovingObjectsZeroTolerance()`. |
| `gravity.birdDecay` | 1.3 | How fast a bird's extra pull from gravity sensors fades after its first collision (per second). |

The dot in a name is part of the name: write `tuning["sw.forceDivisor"] = 5`.

Pseudocode and Lua, "make every hit do twice the damage and wobble more":

```
set sw.forceDivisor to half of its default
set sw.wobbleMax to 0.3
```
```lua
tuning["sw.forceDivisor"] = 5
tuning["sw.wobbleMax"]    = 0.3
```

Other numbers that scripts already control and the engine reads: the config values `forceDamageMultiplier` (1) and `scoreDamageMultiplier` (5) in the game's config table or as globals, plus the globals in section 3 of "Changing the engine from Lua". Per-level values are `setWorldGravity`, `setLevelLimits`, `setPhysicsSimulationScale` and the object parameters of `setObjectParameter`.

### Lever 2: all the events

| Event | Arguments | Notes |
|---|---|---|
| `update` | `dt, realDt` | every frame |
| `levelLoaded` | `path` | after the level table is in `loadedObjects` |
| `objectCreated` | `name, kind` | `kind` is `box`, `circle` or `polygon` |
| `objectRemoved` | `name` | raised before the object is gone |
| `contact` | `nameA, nameB` | raised when two objects begin to touch, before the engine's damage rules run (Star Wars, Rio, Space and Classic) |

Your own events work the same way: `engine.emit("myEvent", 1, 2)` calls every `engine.on("myEvent", fn)` handler.

Recipe: "count pigs killed and show it" (pseudocode):

```
on objectRemoved(name):
    if name starts with "PIG": kills = kills + 1
on update:
    draw kills at the top left
```
```lua
local kills = 0
engine.on("objectRemoved", function(name)
    if string.find(name, "PIG") then kills = kills + 1 end
end)
engine.on("update", function(dt)
    setRenderState(0, 0, 1, 1, 0)
    res.drawString("", "Pigs: " .. kills, 10, 10, "TOP", "LEFT")
end)
```

Recipe: "bullet time when a bird hits something":

```
on contact(a, b):
    if a or b is a bird: set physicsSpeedFactor 0.3, remember the time
on update(dt):
    if slow and 1 second passed: set physicsSpeedFactor back
```
```lua
local slowLeft = 0
engine.on("contact", function(a, b)
    if string.find(a, "BIRD") or string.find(b, "BIRD") then
        physicsSpeedFactor = 0.3; slowLeft = 1
    end
end)
engine.on("update", function(dt, realDt)
    if slowLeft > 0 then
        slowLeft = slowLeft - realDt
        if slowLeft <= 0 then physicsSpeedFactor = nil end
    end
end)
```

### Lever 1: replacing natives

Rule: wrap the global, call the original, then change the arguments or the result.

```
newFunction(args):
    change args
    result = original(args)
    change result
    return result
```

Recipe: "no gravity in every level":
```lua
local orig = engine.original.setWorldGravity
function setWorldGravity(gx, gy) orig(0, 0) end
```

Recipe: "all blocks are twice as bouncy":
```lua
local prev = createBox
function createBox(name, sprite, x, y, w, h, density, friction, restitution, ...)
    return prev(name, sprite, x, y, w, h, density, friction, (restitution or 0) * 2, ...)
end
```

Recipe: "log every native call" (pseudocode: for each name in the list, replace the global by a function that prints the name and calls the original):
```lua
for _, name in ipairs(engine.natives()) do
    local f = engine.original[name]
    if name ~= "print" and type(f) == "function" then
        _G[name] = function(...) engine.original.print("call", name); return f(...) end
    end
end
```

Recipe: "replace a whole subsystem, for example your own aiming dots": replace `populateAimingAid`, `clearAimingAid` and `updateBirdTrajectoryTable` with Lua functions that fill your own table, and draw it from `engine.on("update", ...)` with `res.drawSprite`.

### Changing the game scripts themselves

The game's own functions (`update`, `birdCollision`, `blockCollision`, `shootSelectedBird`, `initialize` and the rest of what `gamelogic.lua` defines) are globals too, so the same wrap works:

```
newBirdCollision(a, b, force, damage, ...):
    if my rule applies: return          -- skip the original
    original(a, b, force, damage, ...)  -- or run it
```
```lua
local origBC = birdCollision
function birdCollision(a, b, force, damage, ...)
    if force < 2 then return end        -- ignore soft taps
    return origBC(a, b, force, damage, ...)
end
```

For bigger changes decrypt the scripts with `--decrypt` and edit the plain files (see `docs/USAGE.md`).

### What can still not be reached from Lua

Be aware of these limits; they are the real list, not a promise:

- The Box2D code itself, the contact filter rule, and the drawing order inside `drawGameNative` are compiled in (their numbers, such as solver iterations and sleep times, are in `tuning`).
- The sprite wobble curve and some drawing constants are not in `tuning` yet.
- The collision damage arithmetic of Classic, Seasons, Rio and Space is C++; you change it through its inputs, through `contact`, or by wrapping `birdCollision` and `blockCollision`.

If you want one of these opened up, say which and it can be added as a `tuning` name in the same way.

### Backgrounds and foregrounds from Lua (parallax, sine waves, your own layers)

Theme layers are data you can read and change, and the engine tells you when it draws them.

| Function / event | What it does |
|---|---|
| `getCamera()` | `x, y, worldScale, screenWidth, screenHeight`: the world position of the screen's top left corner, the zoom (pixels per world unit) and the window size. |
| `getThemeLayers([which])` | A list of layer tables, back to front. `which` 0 (default) is the background, 1 the foreground of the new theme system (Star Wars). Each table has `index, sprite, speed, scale, scaleX, scaleY, offset, offX, offY, dx, dy, velX, velY, angle, looping, hidden`. The tables are copies: change a field and hand it back with `setThemeLayer`. |
| `setThemeLayer(index, fields [, which])` | Changes the fields you name (`index` counts from 1). `dx` and `dy` move the layer by that many **screen pixels** on top of its normal parallax, so they are the easiest handle for effects. `hidden = true` stops the engine drawing the layer. `sprite`, `speed`, `scale`, `offX` and `offY` change what and where it is. |
| `engine.on("drawBackgroundBefore", f)` / `"drawBackground"` | Raised before and after the engine draws the background layers. Draw your own layers in `drawBackground` (they appear on top of the engine's, behind the blocks). |
| `engine.on("drawForegroundBefore", f)` / `"drawForeground"` | The same for the foreground. |
| `tuning["background.skip"] = 1` / `tuning["foreground.skip"] = 1` | The engine draws no background / foreground at all; you draw it all in the events. |

#### Recipe 1: make the existing layers bob on a sine wave, deeper layers moving more

```
each frame:
    t = t + dt
    for every theme layer i (back to front):
        layer.dy = sin(t * speed + i * phase) * amplitude * i
```
```lua
local t = 0
engine.on("update", function(dt)
    t = t + dt
    for i, layer in ipairs(getThemeLayers()) do
        setThemeLayer(i, {dy = math.sin(t * 2 + i) * 12 * i})   -- 12 px per layer, a different phase for each
    end
end)
```
Use `dx` the same way for a sideways sway. To make the wave depend on the camera (so it ripples as the player scrolls) read it with `getCamera()`:
```lua
local cx = getCamera()
setThemeLayer(i, {dy = math.sin(cx * 0.02 + i) * 20})
```

#### Recipe 2: your own parallax layer, each element on its own sine wave

```
on drawBackground:
    read the camera
    for each element k of my layer:
        x = k * spacing - camera.x * parallaxFactor * worldScale     -- far layers move less than the camera
        y = baseY + sin(time + k * phase) * amplitude                -- the sine wave
        draw the sprite at x, y
```
```lua
local t = 0
engine.on("update", function(dt) t = t + dt end)

engine.on("drawBackground", function()
    local camX, camY, ws, sw, sh = getCamera()
    setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
    local spacing, parallax = 160, 0.3
    local first = math.floor(camX * parallax * ws / spacing)
    for k = first, first + math.ceil(sw / spacing) + 1 do
        local x = k * spacing - camX * parallax * ws
        local y = sh * 0.55 + math.sin(t * 1.5 + k * 0.7) * 25
        res.drawSprite("CLOUD_1", x, y, "VCENTER", "HCENTER")   -- any sprite name of the loaded sheets
    end
end)
```
`getThemeLayers()[i].sprite` tells you a sprite name that is loaded for the current theme. To replace the engine's background completely, also set `tuning["background.skip"] = 1`, and `"foreground.skip"` for the foreground.

#### Recipe 3: swap a layer's picture, speed or visibility

```lua
setThemeLayer(2, {speed = 0.8})        -- moves with the camera nearly 1:1
setThemeLayer(3, {hidden = true})      -- gone
setThemeLayer(1, {scale = 1.2})        -- bigger
```

Notes: the layers are rebuilt when a level (theme) loads, so set your changes after `levelLoaded` (or every frame, as in recipe 1). `dx` / `dy` are applied by Classic, Seasons, Rio, Space and Star Wars backgrounds and by the new-system foreground; the old foreground (the rolling ground strip of Classic) is not a layer list yet, so use `foreground.skip` and draw it in `drawForeground` if you want to change it.

### Discovering every tuning name

`engine.tuningDefaults` is a table of every `tuning` name with its built-in value, so a script (or the console) can list them all or reset one:

```lua
for name, default in pairs(engine.tuningDefaults) do print(name, default, tuning[name]) end
tuning["water.band"] = nil          -- back to the built-in value
```

### Which games the layer functions were checked on

`getThemeLayers`, `setThemeLayer`, `getCamera` and the draw events work in every game that draws a theme: they were checked in Angry Birds Star Wars (a Hoth level: the layers bob with a sine wave, and the engine background can be replaced) and on Classic 1.6.2's background loop (`dy = -200` moves the whole background 200 pixels up), which Seasons and Rio use too. Space uses the same layer routine as Star Wars. The menus of Classic and Seasons draw their backdrop with the scripts' own sprite calls, not with theme layers, so there you change the backdrop by wrapping the menu's draw function.

### Rule hooks: functions the engine calls so Lua can decide

These are plain functions you put in the `engine` table. While one is not defined the engine uses its own rule, so nothing changes. Define it to take over the decision.

| Function | Called | Return |
|---|---|---|
| `engine.shouldCollide(nameA, nameB)` | every time the physics asks whether two objects may collide (in every game, after the engine's own rule) | `true` forces a collision, `false` makes the two pass through each other, `nil` keeps the engine's rule |
| `engine.strengthChange(name, other, oldStrength, newStrength)` | after a hit has worked out the strength a block is left with (Classic, Seasons, Rio, Space and Star Wars) | a number replaces `newStrength` (zero or less destroys the block, more than zero keeps it alive), `nil` keeps it |

Pseudocode and Lua: "birds pass through ice, and wood takes half the damage":

```
shouldCollide(a, b):
    if one of them is a bird and the other is ice: return false
    otherwise: return nil          -- the engine decides
strengthChange(block, bird, old, new):
    if block is wood: return old - (old - new) / 2
```
```lua
function engine.shouldCollide(a, b)
    if (string.find(a, "BIRD") and string.find(b, "ICE")) or (string.find(b, "BIRD") and string.find(a, "ICE")) then
        return false
    end
end
function engine.strengthChange(name, other, old, new)
    if string.find(name, "WOOD") then return old - (old - new) / 2 end
end
```

`shouldCollide` runs for every pair the broad-phase finds, so keep it short (no printing, no loops over all objects) or the game slows down.

### Drawing hooks for objects and the whole game scene

| Function / event | When | Effect |
|---|---|---|
| `engine.drawObject(name)` | before the engine draws each physics object, every frame | return `false` and the object is not drawn (draw it yourself in `drawGame` if you want a different picture); any other result draws it as usual |
| `engine.on("drawGameBefore", f)` | before the engine draws the blocks, birds and pigs | draw behind everything in the scene; the render state is the world camera, as the engine uses it |
| `engine.on("drawGame", f)` | after the scene is drawn | draw on top; the render state is reset to screen pixels, so use `getCamera()` to place things at world positions |

The order in which objects are drawn is `z_order`: set `objects.world[name].z_order` through `setObjectZOrder(name, z)`, or `bringObjectToFront` / `sendObjectToBack`.

Recipe: "pigs wear a hat" (pseudocode, then Lua):

```
on drawGame:
    camera = getCamera()
    for every object whose name starts with PIG:
        screen x = (object.x * 20 - camera.x) * camera.scale
        screen y = (object.y * 20 - camera.y) * camera.scale
        draw HAT at (x, y - 30)
```
```lua
engine.on("drawGame", function()
    local cx, cy, ws = getCamera()
    setRenderState(0, 0, 1, 1, 0, 0, 0, 1)
    for name, o in pairs(objects.world) do
        if string.find(name, "PIG") then
            res.drawSprite("HAT_1", (o.x * 20 - cx) * ws, (o.y * 20 - cy) * ws - 30, "VCENTER", "HCENTER")
        end
    end
end)
```
(`HAT_1` stands for any sprite name that exists in the game's sprite sheets; objects live in world units and the engine draws them at 20 pixels per unit.)

### More tuning names (Classic, Seasons, Rio and Space damage)

| Name | Default | Effect |
|---|---|---|
| `classic.blockForce` | 0.1 | Block against block: the force of a hit is the momentum difference times this. |
| `classic.blockScore` | 10 | Points per whole point of damage when two blocks hit. |
| `classic.birdForceDivisor` | 10 | A bird's hit force is speed times mass divided by this (smaller means more damage). |
| `classic.legacyVelocityFactor` | 1.75 | How much of its speed a bird that breaks a block keeps, on the legacy collision path. |

### Particle tuning

| Name | Default | Effect |
|---|---|---|
| `particle.amountScale` | 1 | Multiplies how many particles every `addParticles` / `addLevelParticles` call makes (the soft and hard limits still apply). |
| `particle.speedScale` | 1 | Multiplies the starting speed of new particles. |
| `particle.lifeScale` | 1 | Multiplies how long new particles live. |
| `particle.sizeScale` | 1 | Multiplies the size of new particles (both the start and the end scale). |
| `particle.gravityScale` | 1 | Multiplies the gravity of every particle definition, also for particles that already exist. |

Everything else about a particle (velocities, angles, lifetime, gravity, scales, sprites, animation) comes from its entry in the game's particle table (`particleTable` in `particles.lua`); the definitions live in `particleTable.particles[name]` and are ordinary Lua. The engine reads a definition the first time a particle of that type is made and keeps it, so after you edit one (for example `particleTable.particles.SMOKE_1.lifeTime = 3`) call `clearParticleCache()`; the next particles of every type then read the table again.

### The old-style foreground strip (Classic, Seasons, Rio): layer list 2

`getThemeLayers(2)` / `setThemeLayer(index, fields, 2)` give the rolling ground strip of the older games, which is not a theme layer: each entry has `sprite`, `speed`, `scale`, `offset`, `dx`, `dy` and `hidden`. `dy = -150` on the first entry lifts the dirt layer 150 pixels and shows the one behind it; `hidden = true` removes a layer; `sprite` swaps its picture. (The colour the engine fills below the strip is the theme's ground colour; to change that, hide the layers and draw your own in `drawForeground`.)

### More events

| Event | Arguments | When |
|---|---|---|
| `physicsStep` | `dt` | just before each physics step (the world steps at a fixed rate, more than once per frame when the game runs slow) |
| `physicsStepEnd` | `dt` | right after each physics step and its speed caps |
| `contactEnd` | `nameA, nameB` | when two objects stop touching |
| `contact` | `nameA, nameB` | when two objects begin to touch (documented above) |

Recipe, "count how long a bird stays on the ground" (pseudocode):

```
on contact(bird, ground): start counting
on contactEnd(bird, ground): stop counting
on physicsStep(dt): if counting, total = total + dt
```
```lua
local counting, total = false, 0
engine.on("contact",    function(a, b) if string.find(a .. b, "BIRD") and string.find(a .. b, "GROUND") then counting = true end end)
engine.on("contactEnd", function(a, b) if string.find(a .. b, "BIRD") and string.find(a .. b, "GROUND") then counting = false end end)
engine.on("physicsStep", function(dt) if counting then total = total + dt end end)
```

### Replacing the damage itself: `engine.damage`

| Function | Called | Return |
|---|---|---|
| `engine.damage(attacker, victim, force, damage)` | when a bird hits a block (all games) and when one block hits another (Classic, Seasons, Rio, Space), after the engine has worked out `damage` (Star Wars: after the defence and the homing reduction) and before the block's strength is changed | a number replaces `damage`; `nil` keeps it |

`force` is the raw hit force before the bird's damage multipliers. Together with `engine.strengthChange` (what strength the block is left with) and the `classic.*` / `sw.*` tuning names (the constants inside the formulas) this lets Lua replace the whole damage rule.

```
damage(attacker, victim, force, damage):
    if attacker is a bird and victim is glass: return damage * 3    -- glass breaks easily
    return nil                                                      -- everything else as usual
```
```lua
function engine.damage(attacker, victim, force, damage)
    if string.find(attacker, "BIRD") and string.find(victim, "GLASS") then return damage * 3 end
end
```
Star Wars block-against-block hits do not go through `engine.damage` yet; use `engine.strengthChange` there.

### Ground colour

`getGroundColor()` returns `r, g, b` (0 to 255), the colour the old-style foreground fills below the ground strip; `setGroundColor(r, g, b)` changes it. Together with layer list 2 this makes the whole Classic ground changeable.

### Changing every new particle: `engine.particle`

| Function | Called | Return |
|---|---|---|
| `engine.particle(type, t)` | for every particle the engine creates, after it has picked the particle's values | `false` drops the particle; anything else keeps it. Change the table `t` to change the particle: `x, y, vx, vy, angle, angleVel, scaleBegin, scaleEnd, lifeTime, sprite` |

```
particle(type, t):
    if type is smoke: make it go up faster and live twice as long
```
```lua
function engine.particle(type, t)
    if string.find(type, "smoke") then t.vy = t.vy - 50; t.lifeTime = t.lifeTime * 2 end
end
```
It runs once per particle, so keep it short when a game makes hundreds at once.

### More Box2D numbers in `tuning`

| Name | Default | Effect |
|---|---|---|
| `solver.velocityThreshold` | 1 | Below this closing speed a hit does not bounce (Box2D's restitution threshold). Set it to 0 for bouncier small hits. |
| `solver.maxLinearCorrection` | 0.2 | The most Box2D pushes two overlapping bodies apart in one step. |
| `solver.baumgarte` | 0.2 | How hard overlaps are corrected each step (higher pushes apart faster and can jitter). |
| `solver.maxRotation` | 1.5708 | The most a body may turn in one step, in radians. |
| `solver.angularSleepTolerance` | 0.0349 | How slowly a body must spin to count as still for sleeping, in radians per second. |

### Data that is already plain Lua (edit it directly)

Most of what a game *is* lives in Lua tables that the engine reads when it needs them, so nothing has to be exposed for it. The names differ a little between games; list them with `for k, v in pairs(_G) do print(k, type(v)) end` or `printGlobals()`.

| Table | What it holds | Change it to |
|---|---|---|
| `objects.world[name]` | every live object (`strength`, `defence`, `material`, `sprite`, `x`, `y`, `angle`, `z_order`, ...) | damage, heal or re-skin an object; set `strength` and the next hit uses it |
| `objects.joints[name]` | the joints of the level | read joint settings (use `setJointParameters` to change a live joint) |
| `blockTable` / block definitions (`blocks.lua`) | strength, defence, density, damage factors of every material and bird | rebalance a material; new objects use the new numbers |
| `particleTable.particles[name]` | particle definitions | edit, then call `clearParticleCache()` |
| `scoreTable`, `deadBlocks` | score and destroyed blocks | change scoring, revive a block |
| `loadedObjects` | the level that was just loaded, before the objects are created | add, move or remove objects in a level as it loads (hook `levelLoaded` is raised after it) |
| theme tables (`themes.lua` and friends) | backgrounds, foregrounds, sounds of a theme | build your own theme; the layer functions above edit the live one |
| `settings`, `highscores` | the saves | `saveLuaFile` writes them |
| text groups | every string | `res.setString(group, key, text)` |
| the game's own functions (`update`, `birdCollision`, `shootSelectedBird`, ...) | the game rules written in Lua | wrap or replace them |

### Where each part of the engine can be changed (summary)

| Part | How |
|---|---|
| Physics: gravity, damping, restitution, density, speed caps, colliders, joints, sensors | natives (`setWorldGravity`, `setObjectDensity`, `setSpeedCap`, `setColliderRule`, `setJointParameters`, ...) |
| Physics: contact filter | `engine.shouldCollide` |
| Physics: solver, sleep, thresholds, water, gravity sensors | `tuning` (`solver.*`, `water.*`, `gravity.*`) |
| Physics: every step | events `physicsStep`, `physicsStepEnd`, `contact`, `contactEnd` |
| Damage and strength | `engine.damage`, `engine.strengthChange`, `tuning` (`classic.*`, `sw.*`), the block definitions |
| Drawing: objects, scene, backgrounds, foregrounds, ground | `engine.drawObject`, `drawGameBefore` / `drawGame`, `drawBackground*` / `drawForeground*`, layer lists 0, 1 and 2, `setGroundColor`, the render-state functions |
| Camera and zoom | `setTopLeft`, `setWorldScale`, `setMaxWorldScale`, `getCamera`, `tuning` (`zoom.*`) |
| Particles | `engine.particle`, `tuning` (`particle.*`), `particleTable`, `clearParticleCache` |
| Sound, text, saves, files, windows | the `res` functions and the natives in the reference above |
| Any native function | wrap it with `engine.original` |
| Anything the above does not reach | the C++ source (this repository): the Box2D code itself and the arithmetic inside the damage and particle routines |

### What was checked on which game

Every function and event of this chapter was run with a smoke script (it calls each one, defines every hook and counts the events) on these games:

| Game | Result |
|---|---|
| Angry Birds 1.6.2 (Classic), menu | no script errors; layer lists 0 and 2, the background and foreground events and `update` fire |
| Angry Birds 2.0.0, menu | the same |
| Angry Birds Space, level | no script errors; every event fires (`contact`, `contactEnd`, `physicsStep`, `physicsStepEnd`, `drawGame`, `drawBackground`, `update`), 10 background layers |
| Angry Birds Star Wars, Hoth level | no script errors; every event fires, background layers move with `dy`, `engine.shouldCollide` and `engine.drawObject` change the scene |
| Angry Birds Rio, menu | no script errors; the layer and draw events only fire inside a level, which was not run |
| Angry Birds Seasons | not run (only part of the Seasons data was available); it shares the Classic code paths |

Tell us about a game or call that misbehaves and it will be fixed.

---

### Level spawner (3.7)

`levelSpawner` (in `extra_scripts.lua`) puts the data of another level into the level that is being played. F1 opens a picker (Left / Right: level, Up / Down: folder, Return: spawn, Escape: close). The level file is read with `readDataFile`, run in an empty table to get its `world` and `joints`, and every entry is made like Red's Mighty Feathers (3.3.0) makes a vehicle: `createObject(blockTable, definition, newName, x, y, fields)` with the level's other fields, and `createJoint` with `end1` / `end2` changed to the new names. Fields: `enabled`, `key`, `closeKeys`, `spawnKeys`, `prefix` (names are `prefix .. number .. "_" .. oldName`), `skip` (definitions that are not made; birds never are), `drop` (a level file dropped on the window during a level is spawned at the cursor; `false` loads it as the level, as before) and `title`.


## Changing how damage is worked out and how things are drawn (2.9)

### `engine.force(a, b, momentumX, momentumY, force)`

Called for every contact in every game, before any damage is worked out. `a` and `b` are the two object names, `momentumX/Y` is the momentum difference (for a bird against a block it is the bird's momentum), `force` is the number the engine worked out from it. Return a number to use instead, or `nil`/nothing to keep it. Everything after this (`engine.damage`, defence, strength, score) uses what you return.

```lua
-- pseudocode: every hit is twice as strong, but nothing under 3 counts
engine.force = function(a, b, mx, my, force)
    if force < 3 then return 0 end
    return force * 2
end
```

Order of the damage hooks for one hit: `engine.force` (how hard) -> `engine.damage` (how much of it a victim takes) -> `engine.strengthChange` (what strength it is left with). In Star Wars this now covers block against block as well as bird against block. Classic, Seasons and Rio use the same three hooks.

### `engine.drawSprite(sprite, object, x, y)`

Called for every sprite the game draws (menus, backgrounds, objects, particles, text). `sprite` is the sprite name, `object` is the name of the object being drawn (empty for menus and backgrounds), `x, y` the position in pixels. Return:

* `false` to leave this sprite undrawn;
* a table to change how this one sprite is drawn, any of: `dx, dy` (added to the position), `angle` (added, radians), `sx, sy`, `alpha`, `r, g, b` (all multiplied), `skewX, skewY, bendX, bendY, blur, pixelate, z, blend, effect` (set), `flipX`;
* nothing to draw it as normal.

The hook can not draw by calling `res.drawSprite` inside itself for the same sprite name, a call from inside the hook is not passed to the hook again, so it is safe to draw extra sprites from it.

```lua
-- pseudocode: a sine wave through everything, red tint on pigs
engine.drawSprite = function(sprite, object, x, y)
    local t = {dy = 12 * math.sin(x / 60 + time)}
    if string.find(object, 'PIG') then t.g = 0.4; t.b = 0.4 end
    return t
end
```

Together with `engine.drawObject` (skip a whole object), the layer functions (`setThemeLayer`) and the draw events (`drawBackground`, `drawGame`, `drawForeground` and their `...Before` forms), a script decides what is drawn, where, and how. The only thing not reachable is the OpenGL code that turns a quad into pixels.

### Checked

Star Wars (Hoth level, blocks kicked with `setVelocity` so they hit each other): `engine.force` 388 calls, `engine.damage` 73, `engine.strengthChange` 73, `engine.drawSprite` about a million calls, the scene visibly tinted and waving. Rio 1.4.2 main menu: `engine.drawSprite` tinted and moved every sprite. Seasons main menu: hook ran with no errors. Classic and Rio hits are covered by the same code as the Star Wars damage test, which was not repeated there.

### Shaders (3.3)

`setShader` takes the built-in names (`"none"`, `"grayscale"`, `"sepia"`, `"invert"`, `"chromatic"`, `"glow"`) and, since 3.3, the name of any shader of your own. It needs a graphics driver with OpenGL 2.0 shaders (every PC of the last fifteen years); without one the presets work as before and your shaders are ignored. `AB_NO_SHADERS=1` turns them off.

**The file.** A shader is `<user data folder>/shaders/<name>.frag`:

```glsl
// the colours turn around the screen
vec4 shade(vec4 texel, vec2 uv, vec4 tint) {
    float h = v_pos.x / u_resolution.x * 6.2831 + u_time;
    vec3 k = 0.5 + 0.5 * vec3(sin(h), sin(h + 2.094), sin(h + 4.188));
    return vec4(texel.rgb * mix(vec3(1.0), k, 0.7), texel.a) * tint;
}
```

`texel` is the sprite's pixel and `tint` the colour it is drawn with; **both are premultiplied by alpha**, like every sheet, so scale `rgb` (not `a`) when you darken, and return a premultiplied colour. What is in front of your code:

| name | what |
|---|---|
| `sampler2D u_tex` | the sheet (use `sampleTex`) |
| `float u_time` | seconds since the start |
| `vec2 u_texel` | the size of one sheet pixel in uv units (`1.0 / sheet size`) |
| `vec2 u_resolution` | the game's screen, in screen units |
| `float u_hasTex` | 0 for the plain coloured rectangles |
| `vec4 u_uvRect` | the sprite's part of the sheet: minU, minV, maxU, maxV |
| `vec4 u_param0` .. `u_param3` | set with `setShaderParam(name, slot, x, y, z, w)`; 0 until set |
| `varying vec2 v_uv` | the position in the sheet |
| `varying vec4 v_color` | the tint (the `tint` argument) |
| `varying vec2 v_pos` | where this is on the screen, in screen units (before the layout of the camera) |
| `vec4 sampleTex(vec2 uv)` | the sheet at `uv`, held inside `u_uvRect`, so the neighbouring sprite is never read |

A file that has its own `void main()` gets this prelude only (write `gl_FragColor` yourself). The language is GLSL 1.10. Compile errors name the line of your file.

**Use it:** `setShader("rainbow")` (the render state), `setShader("rainbow", "background")` or any other target of "Graphics settings for one target", `setRenderState(..., 21st value)`. A shader file is loaded the first time its name is used. `createShader(name, source)` makes one from text (`true` or `false, errors`), `getShaderError(name)`, `listShaders()`, `setShaderParam(name, slot, x [, y, z, w])`.

**The presets are editable.** `shader export` in the console writes the presets and templates as files; after that `setShader("grayscale")` uses `grayscale.frag`, so edit it to change the preset, delete it to get the built-in one back. **Live reload:** a saved file is compiled within half a second; an error prints the messages and the last working version stays.

**Shader creator (console):** `shader templates`, `shader new <name> [template]`, `shader apply <target> <name>`, `shader param <name> <slot> x [y z w]`, `shader check <name>`, `shader reload [name]`, `shader list`, `shader export`. Typical session: `shader new wobble wave`, `shader apply background wobble`, edit `wobble.frag` and watch the picture change, `shader param wobble 0 8` for a stronger wave.

Limits: one shader per sprite; a shader cannot draw outside the sprite it is drawn for; the tiled ground blocks (masked quads) are not shaded; there is no vertex shader of your own and no access to the picture behind the sprite.
