# Development notes

Working notes from while the engine was being written, kept for reference. They describe the reverse-engineering findings behind the code (addresses refer to the 1.6.2 PC exe). Parts of the status columns are out of date: everything listed as not started now works, see the main README.

Spec of what the scripts need: `ENGINE_SPEC.md` (not included in this repository). Physics must be **Box2D 2.1.0** (not 2.2.x) so the feel matches. Lua must be **5.1 with `lua_Number` = float** (`#define LUA_NUMBER float` in luaconf.h).

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

## `src/physics/` and `third_party/box2d/`

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
