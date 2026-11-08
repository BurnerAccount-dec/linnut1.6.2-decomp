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
| `rayCast(x1, y1, x2, y2 [, all])` | Box2D's own ray cast. Without `all`: `name, hitX, hitY, normalX, normalY, fraction` of the nearest object (nil if none). With `all = true`: a table of `{name, x, y, nx, ny, fraction}` for every object the ray passes through, nearest first (lasers through stacked glass, piercing shots). `getRayCastedObjects` is still there. |
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

* `setBlendMode(mode [, target])`: `"normal"`, `"add"`, `"multiply"`, `"screen"` (or 0 to 3). `setRenderState` takes it as its 20th value.
* `setShader(name [, target])`: `"none"`, `"grayscale"`, `"sepia"`, `"invert"`, `"chromatic"` (colour fringes), `"glow"`. `setRenderState` takes it as its 21st value. These are built in: the renderer is fixed-function OpenGL 1.x, so shader programs you write yourself are not possible.
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

### Polygon skin

`setPolygonRadius(r)` / `getPolygonRadius()`: the collision skin of boxes and polygons created afterwards (Rio, Box2D 2.2 only; default 0.1, the skin only decides when blocks count as touching, it does not push stacked blocks apart).

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

### How the Space physics now follows the retail exe

- The gravitation sensors use one function for the real physics and for the aiming aid, so the two cannot disagree. Like the exe, a sensor's pull ignores the "gravity on" parameter (22), wakes a sleeping body inside the field every step, and is skipped only while the sensor's own body sleeps.
- The aiming aid (`updateBirdTrajectoryTable`) now follows what the exe does: it flies a copy of the bird on its own with the free-flight integrator (world gravity plus the sensors' force times the inverse mass, the velocity damping clamp `1 - h * damping` between 0 and 1, the maximum translation of 2 per step), without collisions, and stores a point every few steps.
- Box2D's block-solver condition number is 1000 for Space and 100 for Rio, as in each exe.

## Native function summary

These are the functions the engine registers for the scripts (all in `src/lua/lua_api.cpp`). Several are only used by one game; the ones that only talk to online services, ads or purchases are stubs.

- **Window and input:** `requestExit`, `setBGColor`, `captureMouse`, `isMouseCaptured`, `setFullScreenMode`, `isInFullScreenMode`, `setResolution`, `setRetinaResolution`, `setIsMultitouchMouseWheelSimulationEnabled`, `doesMouseClickSetsTouchCount`, `getClipboardText`, `hasLocationCapability`.
- **Files, scripts and levels:** `loadLuaFileToObject`, `loadLuaFile`, `saveLuaFile`, `checkForLuaFile`, `createDirectory`, `checkDirectory`, `loadLevel`, `loadLevelFile`, `getLoadStatus`, `saveLevel`, `loadLevelEditor`, `setEditing`, `setGameOn`, `setGameParameters`.
- **Physics objects:** `createBox`, `createCircle`, `createPolygon` (with `clearVertices`, `addVertex`), `removeObject`, `setPosition`, `setRotation`, `setVelocity`, `setAngularVelocity`, `setSleeping`, `applyImpulse`, `applyForce`, `getVelocity`, `getAngle`, `getLocalPoint`, `getWorldPoint`, `setObjectParameter`, `getIntersectingObjects`, `hasMovingObjects`, `hasMovingObjectsZeroTolerance`.
- **Physics world:** `setPhysicsEnabled`, `isPhysicsEnabled`, `setPhysicsSimulationScale`, `setWorldGravity`, `setLevelLimits`, `setCollisionEnabled`; engine additions: `setObjectAlpha`, `getObjectAlpha`, `setColliderType`, `getColliderType`, `setColliderRule`, `getColliderRule`, `setPolygonRadius`, `getPolygonRadius`.
- **Joints:** `createJoint`, `createJoints`, `setJointParameters`, `checkJointLimits`, `destroyJoint`, `removeJointsFromObject`.
- **Camera and drawing state:** `setTopLeft`, `setWorldScale`, `setMaxWorldScale`, `setMaxTranslation`, `enableSmoothZooming`, `setRenderState` (8th value: alpha), `setAlpha`.
- **Drawing:** `drawRect`, `drawRectLines`, `drawLine2D`, `drawTexturedRect`, `drawSome`, `drawLayer`, `drawBoxNative`, `drawUITextNative`, `clipText`, `drawGameNative`, `drawBackgroundNative`, `drawForegroundNative`, `drawParticlesNative`, `drawLevelParticlesNative`, `drawMenuParticlesNative`, `updateBackgroundAndForegroundScrollingNative`.
- **Sprites, materials and themes:** `setSprite`, `setMaterial`, `setTexture`, `setTheme`, `createThemeSprite`, `modifyThemeSprite`, `updateThemeSprite`, `removeThemeSprite`, `setThemeSprite`, `rotateThemeSprites`, `isHDVersion`.
- **Particles:** `setParticleSystemLimits`, `clearParticleCache`, `clearParticles`, `clearParticlesNative`, `updateMenuParticlesNative`, `enableInGameParticlesNative`.
- **Trajectory dots:** `startNewTrajectory`, `addToTrajectory`, `addPuffToTrajectory`.
- **Time and system:** `getCurrentTime`, `getTimeDifference`, `getTImeDifferenceInSeconds`, `getOSName`, `getOSVersion`, `getModel`, `getManufacturer`, `getCurrentLocale`, `initLocales`, `getDeviceID`, `verifyDeviceID`, `areDeviceIDsEqual`, `canOpenProgram`, `openProgram`, `playVideo`, `print`, `printGlobals`, `throwException`.
- **The `res` table** (reached as `_G.res`): sprite sheets, fonts, `drawSprite`, `drawString`, `getSpriteBounds`, `getSpritePivot`, `useFont` and the audio functions.
- **Stubs for services that are not part of a PC build:** registration and updates (`openRegistrationDialog`, `registerKey`, `checkRegistrationResult`, `requestLatestVersionNumber`, `checkForUpdates`, `callVersionUpdater`), Crystal, Game Center and leaderboards, Flurry and Medio statistics, ads, in-app purchases, notifications, NFC and the task switcher.

