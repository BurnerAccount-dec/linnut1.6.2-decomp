# Development notes

Working notes from while the engine was being written, kept for reference. They describe the reverse-engineering findings behind the code (addresses refer to the 1.6.2 PC exe). Parts of the status columns are out of date: everything listed as not started now works, see the main README.

Spec of what the scripts need: `ENGINE_SPEC.md` (not included in this repository). Physics must be **Box2D 2.1.0** for Angry Birds Classic and Seasons and **Box2D 2.2.0** for Angry Birds Rio, so the feel matches (both are built in, see below). Lua must be **5.1 with `lua_Number` = float** (`#define LUA_NUMBER float` in luaconf.h).

## Layout (mirrors the original source tree, whose paths are embedded in the exe)

| Folder | Original files | Status |
|---|---|---|
| `src/game/` | `Resources.cpp`, `SpriteSheet.cpp`, `BitmapFont.cpp`, `TextGroupSet.cpp`, game object / contact code | `collision_damage`, `frame_update` done |
| `src/gr/` | `gl/GL_Context, GL_Texture, GL_Pass, GL_Shader, GL_EffectParser`, `gl1/GL1_*`, `DIPrimitive` | not started |
| `src/hgr/` | `Node.cpp`, `ParticleSystem.cpp`, `DefaultResourceManager.cpp` | not started |
| `src/audio/` | `AudioMixer`, `AudioClip`, win32 `AudioOutputImpl` / `AudioInputImpl` | not started |
| `src/io/`, `src/lang/` | file bundle, streams, hashtable, thread, strings | not started |
| `src/framework/` | `win32/WinMain.cpp`, window, input | not started |
| `src/lua/` | Lua state wrapper and the ~130 registered functions | `platform`, `lua_api`, `script_host`, `lua_frame_host` done |

## `src/game/collision_damage.*`

The pure rules from the exe's contact listener `BeginContact` (0x40b670): block-vs-block force and score, bird-vs-bird force, bird-vs-block damage with the per-bird damage and velocity multipliers, the strength and defence bookkeeping, the bird slowing down after breaking a block (new and legacy path), and the order and arguments of the `birdCollision` / `blockCollision` Lua callbacks.

How sure: read from the decompile and checked against the disassembly (the squared-speed comparison, the constants 0.1, 10.0 and 1.75, the argument order of the callbacks). The tests in `tests/` were calculated by hand from that reading, so they catch typos, not misreadings. The real check is running the original and the rebuild side by side on the same collision.

Not yet wired: reading `objects.world[name]` into `ContactObject`, writing the result back, and the two skip flags at Game+0x1e9 / +0x1ea whose meaning is still unknown.

## `src/game/frame_update.*`

The per-frame update (0x408980): fixed 1/30 s steps with an accumulator (`Step(1/30, 10, 10)`), `removeBlocks`, applying the bird velocity the collision code queued, double-buffered snapshots taken only in the last two steps of a frame, interpolation (with angle wrap), destroy timers, writing `x y xVel yVel angle frozen sleeping` into the object tables, the `hasMovingObjects` / `hasAwakeObjects` / `hasMovingObjectsAboveTolerance` flags, joint endpoints `x1 y1 x2 y2`, the three rolling loops, and finally Lua `update(dt, dt)`.

Box2D, Lua and audio are behind the `Body`, `JointView` and `FrameHost` interfaces, so this compiles and is tested without any of them.

Resolved on the way: `setLevelLimits(minX, _, maxX, _)` keeps only args 1 and 3 as ints (Game+0x2a4 / +0x2a8); the two y arguments are ignored. "Frozen" means an object with mass whose y > 20 or whose x is outside those limits.

Guesses to check against the original: joint endpoints are taken as (anchor A, anchor B) from the two virtual calls on the joint wrapper (the call order is certain, the Box2D meaning is inferred); `UpdateEffects` is the object at Game+0x60 (0x41cec0), whose purpose is not decoded yet; the screen size, zoom and `touches` setup at the top of the original is left to the host (`StepZoom` covers the zoom part).

## `src/lua/` and `third_party/lua/`

- `third_party/lua/`: Lua 5.1.1 with `lua_Number` = float (`luaconf.h` edited; the interpreter and compiler front-ends are left out).
- `lua_api.*`: registers every function in ENGINE_SPEC sections 5 and 6, plus `res` and `particles`, and forwards them to the `Platform` interface (`platform.h`). Every `Platform` method has a harmless default, so the scripts run before physics, drawing and audio exist. `saveLuaFile`, `clipText`, `print`, `printGlobals`, `loadLuaFileToObject` are fully implemented here.
- `script_host.*`: builds the script environment (ENGINE_SPEC 1b), sets the globals and tables, loads starLimits, loadlist, gamelogic, blocks, particles, and calls the callbacks (`update`, `birdCollision`, `blockCollision`, ...).
- `lua_frame_host.*`: the Lua half of the frame update (writes into `objects.world[name]`, `objects.joints[name]`, `deadBlocks`, the flags).

Check with the real scripts: `ab_script_boot <scripts folder> <user folder> [frames]` loads your six scripts, runs `createStartUpAssets` and 3000 frames at 30 fps. With stub resources the scripts get through the splash screens, run `initialize` and reach the main menu with no Lua error. It prints the globals the scripts read while they were still nil (mostly the scripts' own variables); an engine-provided name that shows up there is a missing piece.

Not done: anything behind the `Platform` defaults (drawing, audio, sprite sheet and font loading, level loading), input tables (`keyPressed`, `cursor`, `touches` are empty), and connecting `Platform`'s physics methods to `PhysicsWorld` (below).

## `src/physics/`, `third_party/box2d/` and `third_party/box2d22/`

Angry Birds Classic and Seasons use Box2D 2.1.0 (`third_party/box2d`, 2.1.1 as supplied, close enough); Rio uses Box2D 2.2.0 (`third_party/box2d22`). Both are linked into the one executable: every name in the 2.2.0 copy gets the suffix `_v22` (`Common/b2Rename.h`, included first by `b2Settings.h`), so the two do not clash. `physics_world.h` is a Box2D-free interface (`PhysicsWorld`, `PhysBody`); `physics_world.cpp` is the part both versions share and `physics_world_b2.cpp` is compiled once per version. `GamePlatform::Physics()` makes the world on first use, after the scripts have said which game this is (Rio: 2.2.0). Differences that matter to the game code: 2.2.0 keeps the body angle in (-pi, pi], and has `b2Rot` instead of a rotation matrix. The Rio exe has the same three changed constants as the classic exe (found as the same constants in its island and solver code), so `box2d22` has them too, except the polygon skin, see "Rio light blocks and the polygon skin" below. `NDEBUG` is set for `box2d22`: 2.2.0 asserts in `SolveTOI` for a contact left over between two static bodies, which the release exe skips. One more 2.2.0 quirk: `b2Body::GetAngle()` returns the wrapped angle but the joint solvers use the raw sweep angle, and `b2JointDef::Initialize` takes the reference angle from `GetAngle()`. The scripts keep angles in [0, 2*pi), so welds/hinges between bodies rotated past pi started a full turn out of line and the Airfield Chase boss level exploded. `physics_world_b2.cpp` therefore sets `referenceAngle` from `GetSweepAngle()` (a one-line accessor added to `third_party/box2d22/.../b2Body.h`). Debugging: `AB_BOX2D=21|22` forces a version, `AB_BODYDUMP=n` prints every dynamic body each n steps (`BD <step> <name> x y angle`), `AB_PHYSDBG=1` prints the version.

`third_party/box2d/` is Box2D 2.1.1 as supplied. The exe's copy has the same layout, but four constants in `Common/b2Settings.h` were changed to match it (found in the exe's solver code):

| setting | stock 2.1.1 | AngryBirds.exe |
|---|---|---|
| `b2_linearSlop` (so `b2_polygonRadius` = 2x is 0.1) | 0.005 | 0.05 |
| `b2_timeToSleep` | 0.5 | 0.25 |
| `b2_linearSleepTolerance` | 0.01 | 0.1 |

Everything else I checked is stock (angular slop 2 deg, max corrections 0.2 and 8 deg, max translation 2, max rotation pi/2, Baumgarte 0.2, TOI Baumgarte 0.75, velocity threshold 1, AABB extension 0.1). 2.1.1 renamed `ApplyImpulse` to `ApplyLinearImpulse`; same behaviour. The exe's body has one extra field (`inertiaScale`, always 1.0), which has no effect.

`physics_world.*` creates the world and objects exactly as the exe does: gravity (0, 20), sleeping, warm starting and continuous physics on; bodies dynamic if density != 0, else static; angular damping 1 (2 for controllable circles and polygons); fixture filter category 1, mask 0xFFFF; box = `SetAsBox(w/2, h/2)`, circle radius, polygon from the vertex list. Joint types 1 to 4 (distance, weld, revolute, prismatic) with their coordType anchor rules, `checkJointLimits`, `setJointParameters`, type 5 "link" records, `removeObject`, `setPosition`, `setRotation`, velocities, sleeping, `getWorldPoint`. It also holds the game's collision filter (0x40c440): objects collide only if both have `collision` set, and a circle created with z_order 999 does not collide with an object whose material is "immovable". `Body` and `JointView` adapters feed `UpdateFrame`. `BeginContact` calls a hook; the Lua side then uses `ResolveContact`.

Guesses to check: the z_order 999 test (the decompile only shows `ftol(x) == 999` on a value I read as z_order); the joint `destroyJoint` behaviour (not read from the exe yet); the "link" (type 5) mechanism, read from `removeObject`.

Not yet done: the `Platform` overrides that call `PhysicsWorld` and write the Lua tables (mass, joint fields), `setObjectParameter`, `setMaterial`, and the `BeginContact` Lua wiring. These need a level to test against.

## level loading and `src/engine/game_platform.*`

`GamePlatform` is the first real `Platform`: it owns a `PhysicsWorld`, creates bodies/joints from the scripts' `createBox` / `createCircle` / `createPolygon` / `createJoint` calls, writes the `objects.world[name]` and `objects.joints[name]` tables the way the exe does (fields and defaults in `lua_api.cpp`), and runs the frame update (`RunFrame(dt)`).

`loadLevel(file)` follows 0x40d2c0: drops the old physics, re-reads `blocks.lua` and `particles.lua` into fresh `blockTable` / `particleTable` tables (they are data files, not scripts that share the script environment), runs the level file in an empty table and stores that as `loadedObjects` (nil if the file is missing). Level files are looked up under the data folder given to `GamePlatform`; they are read as plain Lua (the original reads them encrypted).

Optional joint fields follow the exe's reading rules: a flag counts only if it is a real boolean (`collideConnected = 0` is "absent"), a number only if it is a number or string.

`ab_level_test <scripts> <data> <levels/pack1/Level1.lua> [seconds]` boots the scripts, loads a level through `loadLevelInternal` and runs the physics. Today it reports NaN-like results for blocks, because sprite sizes still come from the stubbed `res.getSpriteBounds` (every block is 0 x 0). The sprite sheet loader is the next piece.


## debug keys and the level editor (`script_host.cpp`, `level_writer.*`)

At the end of `LoadScripts` a few lines of Lua set `cheatsEnabled` and `showEditor` and give `update` / `updateGame` (the two functions that read the debug keys) an environment in which `releaseBuild` is false and everything else is the real global table. `releaseBuild` itself must stay true: false makes `resetSettings` and the level select unlock the whole game. Rio's other debug keys are gated by `cheatsEnabled` alone.

`saveLevel(file)` (`level_writer.cpp`) writes the global `objects` as a Lua file at `<data>/<file>.lua` (written next to it and moved over the old file, so a failed write keeps the old level; the Windows read-only flag is cleared). What goes in: `theme`, `filename`, `physicsToWorld`, camera data, `counts`, `joints`, `themeSprites` (not when empty) and every block of `objects.world` with only the authored fields (`name`, `definition`, `x`, `y`, `angle`, `startNumber`, `respawn`, and `strength` for Seasons). Runtime tables (`deadBlocks`, `levelParticles`, `loadedObjects`) stay out. Top-level entries of the file that the scripts do not copy into `objects` (Rio `trainCarts`, Seasons `weather` / `worldGravity`) are kept from the last `loadLevel` (registry entry `ab.lastLevel`) and written back. `createDirectory` / `checkDirectory` work on the data folder. Checked by loading every Rio test level through `loadLevelInternal`, saving it and loading the saved file: the loaded tables are equal except `filename`, angles reduced to 0..2 pi, and `particlesBG` / camera fields the loader recomputes anyway.

Rio's shipped block files carry no `index` and, for most blocks, no `group`, which the editor scene needs to build its palette (it stops with "table index is nil" on entry). When `blockTable` is filled, each block gets `group` = the name of the list it was defined in (`woodBlocks`, `glassBlocks`... the names in `groups.lua`) and `index` = its position in that list. Only the editor reads these two fields. `createJoint` also keeps a joint's `recreate` flag in `objects.joints`, which `checkJointsForObject` callers read (and the editor writes back).

## Rio damage between blocks

`damageFactors` of the attacker tell how much damage it deals to a material: the damage a block takes from a contact is the force times the other block's `damageMultiplier[this block's material]` (1 if absent), applied before the defence comparison. The platforms Mauro jumps between have the default set, whose `monkeyBoss` multiplier is 0, so landing does not hurt him. The event's force stays the raw force.

Rio: when the level stops being drawn (exit to the menu or the loading screen) its particles, and the still running stars of the result screen, are dropped.

Rio light blocks and the polygon skin (v1.8): the small ice tower next to Mauro (8-15, `LevelC2_BOSS`) and Smuggler's Plane 12-8 collapsed in our Box2D 2.2.0 run but stand in the retail exe. The authored levels place blocks flush (gaps of 0.005 to 0.08 between stacked blocks). With a skin of 0.1 (two linearSlops, the stock 2.2.0 ratio) the skins of touching blocks overlap, the solver pushes them apart and tall stacks are kicked over within the first 10 steps. With a skin of one linearSlop (0.05) the same levels settle exactly like they do in Box2D 2.1.1 (12-8: one body moves, the intended rolling rock; with 0.1: 36 bodies moved). `third_party/box2d22/Box2D/Common/b2Settings.cpp` therefore starts `b2_polygonRadiusValue` at `b2_linearSlop`; scripts can change it with `setPolygonRadius(r)` before a level is loaded (`getPolygonRadius()` reads it), and `AB_POLYRADIUS=r` overrides it for a test run. A regression test in `tests/physics_world_test.cpp` stacks two flush blocks. Not checked: any Rio level other than the two above, because only the levels listed were staged for the test.

## Collider types and alpha (v1.8)

Collider types follow Angry Birds 3.0.1's `blocks.lua` (`collider_types = {basic = 0, eagle = 1, static = 2, bird = 3, pig = 4, ghost = 5, sticky = 6}`; since 1.9 the engine no longer installs that table, the scripts define it) and the natives that the last PC Seasons exe registers (`setColliderType` is among its strings). `setColliderType(name, type)`, `getColliderType(name)` (-1 if there is no such object), `setColliderRule(typeA, typeB, collides)` and `getColliderRule(typeA, typeB)` are in `lua_api.cpp`; the rule table lives in `PhysicsWorld::colliderRules_` and is part of `ShouldCollideObjects`, so it works with Box2D 2.1.1 and 2.2.0 alike. Types are 0 to 15. Default rules: every pair collides, except the ghost type (5), which only collides with birds (3) and the eagle (1). That ghost default is an assumption (the South Hamerica scripts use another encryption key and could not be read; the exe only shows the native names), so it is a rule table that scripts can edit rather than hard-wired behaviour. The sticky type (6) is stored but has no special behaviour yet. No script of the supported games calls these natives, so nothing changes for them until a script does.

`setRenderState(tx, ty, sx, sy, angle, pivotX, pivotY, alpha)`: the 8th value is the alpha of everything drawn afterwards, 0 (invisible) to 1 (opaque), values outside are clamped; `setAlpha(a)` sets the same value. It stays until it is set again and is not reset by a 7-argument call, because 2.0.0's scripts set it with `setAlpha` and call `setRenderState` afterwards. Tiled (masked) sprites use it too since 1.9.

## v1.9 notes

- **Rio skin, second pass.** The 0.05 skin of v1.8 fixed Mauro's ice tower and 12-8 but made the TNT in Smuggler's Plane 12-15 fall (it sits 0.1 to 0.2 above its support and only counts as touching with the 0.1 skin, as in the 2.1 build). The cause of the collapses with a 0.1 skin is the 2.2 position solver, which aims for a separation of -linearSlop *including* the skin, so flush stacks are pushed 0.1 apart. `b2_solverSkinFraction` (b2Settings.cpp, used in `b2ContactSolver.cpp` `b2PositionSolverManifold`) scales the skin the position solver uses; default 0 = the skin decides touching only. Measured with `AB_BODYDUMP` at step 60: 12-15 no TNT lost, 12-8 0 bodies moved, Mauro's level the same 5 moved bodies as with 2.1 (a skin 0.05 with the stock solver loses 2 TNT and 15 bodies on 12-15). `AB_POLYRADIUS` and `AB_SOLVERSKIN` override both for tests.
- **Year of the Dragon editor.** Its scripts keep the editor overlay (level name, Gravity/Start/SpeedX lines, saved status, group palette preview) in a global `drawEditor()` which the exe calls each frame; nothing in the scripts does. `ScriptHost::Update` now calls it when `currentGameMode == updateEditor` (`DrawEditorOverlay` in script_host.cpp). Games without a `drawEditor` are unaffected.
- **`dec` folder.** `src/lua/dec_folder.cpp`: `--decrypt` writes every script via `ReadLuaFile`'s decryption into a folder, `--use-dec` makes `ReadLuaFile` try that folder first.
- **Masked and tiled sprites take the alpha** (`RenderBackend::DrawMaskedQuad` has an `alpha` argument; GL multiplies the colour and alpha, the software renderer scales the premultiplied colour). `tests/alpha_test.cpp` checks that a masked quad at 0.5 equals a plain quad at 0.5. Note: native draws like `drawGameNative` and `drawRect` (not inWorld) reset the render state, so a script's alpha only applies to the sprites it draws itself after `setRenderState`/`setAlpha`.
- **`collider_types` is no longer installed by the engine**; Angry Birds 3.0.1 defines it in its own `blocks.lua`. The natives take plain numbers (0 to 15).
- **Console** (`ConsoleCommand` in game_platform.cpp, reader thread in window/main.cpp): typed lines run on the main thread once per frame.
- **Rio velocity iterations.** Smuggler's Plane 12-15 has long chains of revolute joints with balloons; with 10 velocity iterations Box2D 2.2 lets one chain go unstable after about 2 s (position 1e10, NaN, and then `b2TimeOfImpact` never returns, the window freezes). 20 or more velocity iterations keep it stable (checked: 20/10, 25/10, 30/10, 20/20), 10/30 and 15/15 do not. The engine raises the count to at least 20 for Box2D 2.2 (`physics_world_b2.cpp`, `Step`); `AB_VELITERS` / `AB_POSITERS` override it. With that, the TNT of 12-15 stays in place for 250 steps exactly like the 2.1 run. Not reproduced: Golden Beachball level 5 (`LevelGBB_235`), because the `goldenbeachball` level folder is not in the staged Rio files; its early score points are untested.
