# Angry Birds PC engine (unofficial, C++)

An unofficial, from-scratch reimplementation of the Angry Birds PC engine, rebuilt from a Ghidra decompile of the 1.6.2 executable. It is written in C++ (SDL2, OpenGL 1.x, Lua 5.1, Box2D 2.1 and 2.2) and does not re-implement the game logic: it runs the game's **original Lua scripts** and provides the native functions those scripts expect, so levels, menus, physics and scoring behave like the original.

Version 2.2.2. What works, per game:

| Game | Play | Level editor | Notes |
|---|---|---|---|
| Angry Birds 1.0.0 (PC) | Playable | Works | The backgrounds of the first five packs draw now |
| Angry Birds 1.6.2 / 1.6.3.1 (PC) | Playable | Works (tested in 1.6.3.1, 1.6.2 uses the same script) | 1.6.3.1 has no editor in its scripts, the engine brings Rovio's 1.6.2 one |
| Angry Birds 2.0.0 (PC) | Playable, a few known visual gaps (below) | Works, "Editor" button on the main menu | |
| Angry Birds Seasons 1.0.0 (PC) | Playable | Works, "Editor" button on the main menu | |
| Angry Birds Seasons 2.2.0, Year of the Dragon (PC) | Playable | The level name, status line and block palette draw (checked with Year of the Dragon files) | |
| Angry Birds Rio 1.4.2 (PC) | Playable | Works | Ice blocks (called light blocks inside the game's scripts and glass in later games and versions) are stable (Mauro's level, Smuggler's Plane 12-8 and 12-15 tested against Box2D 2.1) |

**Not affiliated with, endorsed by or connected to Rovio Entertainment.** "Angry Birds" and everything in the game belong to their owners. This repository contains no game files: no scripts, images, sounds, levels or executables. To run it you need your own legally obtained copy of the game.

## What works

- The original scripts and levels, including the encrypted ones (AES), run unmodified.
- Box2D physics with the exe's own settings (2.1 for Classic and Seasons, 2.2 for Rio), the original collision and damage rules, and the fixed 1/30 s frame update.
- Level editors in every supported game (see "Debug keys and the level editor"), saving over the original level file.
- Sprite sheets, bitmap fonts, particles (including 2.0.0's level and menu particle layers and parallax), OpenGL 1.x drawing with scaling and rotation.
- Audio (WAV, MP3) with the game's loops.
- Menus, level select, tutorials, pause, restart and level-complete flows in all supported versions, including Seasons 2.2.0's theme menus (parallax foregrounds) and its own in-game cursor.
- Saves that are shared with the original game (both the encrypted and the plain format). The save folder is `%APPDATA%\<publisher>\<product>` from the game's `config.lua`.
- The window is DPI-aware like the original exes, resizing it re-lays the game out (the scripts' `resolutionChanged`), and frame times are smoothed to avoid stutter.

## New in 2.2.2

**Draw order and z.** The `z` of `setRenderState` and the `z_order` of blocks are now one scale: a sprite drawn after `drawGameNative` with a lower `z` than some blocks is drawn below those blocks, and the other way round. Without any `z` set, every game draws exactly as before (compared pixel for pixel on Rio levels and the Classic menu). See "Draw order and z" below.

## New in 2.2.1

**Choose the Box2D version in `config.lua`.** Add `box2d = 21` (Box2D 2.1, the Classic and Seasons physics) or `box2d = 22` (Box2D 2.2, the Rio physics) to the game's `config.lua`; `"2.1.1"` and `"2.2.0"` work too. Without the line the engine uses the game's own, as before. The console command `box2d` overrides it while the game runs, and the config overrides the `AB_BOX2D` variable. The version is picked when a level loads.

## New in 2.2

**Video.** `playVideo(videoPath .. "/" .. name)` plays an MP4 / M4V inside the engine window, no other program. On Windows the picture is decoded by Windows Media Foundation (every H.264 profile, like the sound); everywhere else, and when Media Foundation cannot open the file, the built-in baseline decoder is used. It looks for the file as given, then under the data folder and `videos/`. Click, Space, Return or Escape skip it; the Lua call returns when it ends. `videoPath` is a Lua global filled from `config.lua` (`videoPath = "videos"`), so it can be read and changed from the scripts. `stopVideo()` and `isVideoPlaying()` also exist. The sound track needs AAC decoding, which is done through Windows Media Foundation (Windows only; without it the picture plays silently). Decoder: h264bsd (`third_party/h264bsd`, own licence).

**`setRenderState(tx, ty, sx, sy, angle, pivotX, pivotY, alpha, skewX, skewY, bendX, bendY, z [, bendFunction])`.** Arguments 9 to 17 are new and optional (15 to 17 are the tint, see below).

| Argument | Meaning |
|---|---|
| `skewX`, `skewY` | shear about the pivot (units: shift per unit of height / width), applied before the rotation |
| `bendX`, `bendY` | how far the middle of the sprite is pushed sideways / up, in pixels; drawn as an 8x8 grid |
| `z` | draw order of the sprite (and of `drawRect` / `drawLine2D`) against the game's blocks and other sprites that use `z`; 0 means "in the order the script drew it". The same scale as a block's `z_order`. See "Draw order and z" |
| `bendFunction` | optional Lua function `f(t, 0, 1, 1)` with the same arguments as the gamelogic tweens (`tweenLinear(currentTime, startValue, changeOfValue, duration)`); it is sampled 9 times and its curve shapes the bend. Without it the bend is an arch (sine) |

`setObjectZOrder(name, z)` / `getObjectZOrder(name)` set the draw order of one block (Rio's own `zOrder` still works; in other games a group with a z-ordered object is sorted by it). Both kinds of z are on one scale, see "Draw order and z". The debug overlays drawn by `drawRect` and `drawLine` (the 8 key) now rotate with the body.

**Speed.** `physicsSpeedFactor` (Lua global) now changes only the physics step rate; the scripts' `update(dt)` is untouched. `gameSpeedFactor` multiplies the time step of everything (physics, scripts, animation). Both are read every frame, so `gameSpeedFactor = 0.5` in a script or `set gameSpeedFactor 0.5` in the console works. Defaults 1.

**Frame rate.** The window now runs without waiting for the monitor (vsync off) and without a cap, so on a fast machine it renders far above 60 fps. `fps 120` caps it, `fps 0` removes the cap, `vsync on` waits for the monitor. Start options: `--fps N`, `--vsync`.

**Audio.** From the console or Lua (`res.` functions): `audiospeed [x [track]]` / `res.setAudioSpeed(x)` change playback speed (pitch follows), `audiopause` / `audioresume [track]`, `mastervolume [v]`, and `res.setAudioClipVolume`, `res.setAudioClipSpeed`, `res.getAudioSpeed`, `res.isAudioPaused`, `res.getMasterVolume`. At speed 1 the mixer path is unchanged.

**Startup commands.** `--cmd "<console line>"` (repeatable), `--startup <file>` (one console line per file line) and `--prompt` (or `AB_PROMPT=1`: an `ab>` prompt before the game opens, an empty line starts it) run console commands before the first frame.

**Console.** New: `fps`, `vsync`, `gamespeed [x]`, `physicsspeed [x]`, `set <name> <value>` / `get <name>` (any Lua global), `audiospeed`, `audiopause`, `audioresume`, `mastervolume`.

**Editor.** Saving a level into a folder that does not exist now creates the folder; an absolute path is used as it is.

**Tint (`setRenderState`, arguments 15 to 17, and dedicated functions).** Sprites can be multiplied by a colour: red, green, blue from 0 (none) to 1 (unchanged); above 1 brightens. Like alpha it affects what is drawn after it and is reset when the engine starts a new drawing pass, so set it right before the sprites you want tinted.

    setRenderState(0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, nil, 1, 0.3, 0.3)   -- ..., z, bendFunction (nil), tintR, tintG, tintB: reddish
    _G.res.drawSprite("BIRD_RED_YELL", 200, 250)

The single values also have their own functions, so you do not have to repeat the others:

| Function | Effect |
|---|---|
| `setTint(r, g, b)` / `setTint(grey)` | tint of what is drawn next, for example `setTint(0, 1, 0)` |
| `getTint()` | returns r, g, b |
| `resetTint()` | back to 1, 1, 1 |
| `setAlpha(a)` | transparency, 0 to 1 |
| `setSkew(x, y)` | shear (arguments 9, 10) |
| `setBend(x, y)` | bend (arguments 11, 12) |
| `setRenderZ(z)` | draw order of sprites (argument 13) |
| `setBlur(pixels)` / `resetBlur()` | sprites are smeared over this many pixels (nine copies, each with 1/n of the alpha; `setRenderState` argument 18); 0 is sharp |
| `setPixelate(size)` / `resetPixelate()` | sprites are drawn in blocks of size x size sprite pixels, at most 64 x 64 blocks (`setRenderState` argument 19); 0 is off |

Example: a faded green bird, then the next sprite normal again:

    setAlpha(0.5) setTint(0, 1, 0)
    _G.res.drawSprite("BIRD_RED_YELL", 300, 250)
    setAlpha(1) resetTint()

**Editor, TAB.** TAB in the editor saves a copy of the level and starts the physics; the second TAB loads that copy back. The copy is now also kept in memory, so it is found even when the game folder cannot be written (for example under Program Files).

**Level start.** Every level load starts a new physics world, and blocks are created asleep; see the debug line below for the switches.

**`dec` folder.** `--decrypt` now also decompiles bytecode (1.6.3.1, 2.0.0, Seasons, Rio) into plain Lua with `java -jar unluac.jar` (Java must be installed; `tools\unluac.jar` and `tools\unluac-alt.jar` ship with the engine, `AB_UNLUAC=<jar>` picks another). Every result is checked by loading it as Lua 5.1 and only files that load are written; a list of the files that could not be decompiled is printed (Rio: `gamelogic.lua`, `menu.lua`, `page.lua` and a few menu files are too large for the jars and stay in the game's own bytecode, `--use-dec` falls back to them). `AB_DEC_KEEP=1` keeps the `.luac`. unluac: see its own licence.

**Hatchery and Challenges in 2.0.0 (debug build).** When the debug keys are on (the default; `AB_NODEBUGKEYS=1` turns them off) and the Angry Birds Island HD scripts are in the game's data folder (`data\\hatchery\\` with `hatchery.lua`, and `data\\scripts\\challenges\\challenges.lua` + `ChallengePage.lua`, ; the two ui components the Hatchery needs, CompoImage and InvisibleButton, are built into the engine), the engine switches `g_hatcheryEnabled` / `g_challengesEnabled` on after `options.lua` loads and adds HATCHERY and CHALLENGES buttons next to EDITOR on the main menu. `hatchery.lua` runs with `this` set to its own table, as its other files expect. Hatchery's isometric map and objects are drawn by `src/lua/hatchery_natives.lua` (the tile manager natives: animations run in seconds and turn about their own anchors, placement is checked against water, cliffs and solid objects, an egg needs an empty nest, eggs and birds are drawn inside their nest). The egg painter's canvas, the painted egg sprites (`EGG_PAINTED_n.png` in the user data) and the saved world (`tileMapSave.lua`, `savedEggSprites.lua`) are natives of the engine. The shop log is centred on the screen. The Hatchery needs the full HATCHERY image, font and audio folders. In a level (debug build) an egg button after the pause and restart buttons drops the Hatchery's bird bench; a bird picked from it replaces the next bird of the level (`setupCompoObject`: the object is drawn from the bird's sprites, each with its own offset, scale and angle) and the Mighty Eagle button on the bench calls the level's eagle. The iPad's `MENU_BUTTON_HATCHERY` sprite is not in the data, so the button is the egg sprite on a shadow.

**Debug.** `AB_BODYDUMP=n` also prints, once, gravity, the physics library, every body's mass / damping (`BI`) and the static bodies (`BS`). `AB_DMGDBG=1` lists every damaging contact. A revolute or prismatic joint that has both a motor and limits now reverses at its limits by itself (the Classic scripts never call `checkJointLimits`, which is why the Golden Eggs pickaxes in GE_21 used to sit still). Rio physics follows the retail exe's own solver (see the Rio notes below): 10 velocity and 10 position iterations, the full polygon skin in the position solver (`AB_SOLVERSKIN` can still override it for testing), and Box2D 2.1-style revolute and prismatic joints. Every level now loads into a brand new physics world, and the blocks are created awake. Before, a restarted level settled differently from the first load, which made Ham 'em High 12-1 and 12-8 (P5_610, P5_628) cave in and kill pigs on some restarts; in the sandbox both now give 0 damage events on the first load and on six restarts in a row, but retail has not been compared. `AB_KEEPWORLD=1` restores the old shared world, `AB_WAKEDBG=1` prints each time a script wakes a body. Classic now uses the exe's own continuous collision step (compared with the decompiled exe and the retail game on GE_21). The retail exe runs Box2D 2.1.0, whose time-of-impact pass finds the earliest impact of all contacts that involve a static body, moves the bodies there, solves the touching island with the rest of the step and repeats; the Box2D copy used for Classic had the later per body version, which threw the overlapping hidden TNT blocks of GE_21 (welded to a far static block) and the pigs on them around, destroyed about 20 pigs and scored 138030 before any input, where retail settles the pigs in the pit at 110. Blocks are also created awake again, as in the exe (an earlier v2.2 build started them asleep, which made objects stand still that should move). `AB_OLDTOI=1` switches the time of impact routine back to the bundled Box2D one (the default is the exe's own, ported from 0x4e70d0). `AB_ASLEEPSTART=1` starts blocks asleep for testing and `AB_NOCCD=1` turns continuous collision off. The regular island solve, the position solver's equalized mass and the TOI island follow the exe too. The hand cursor (`CURSOR_*` sprites) is always drawn last, on top and unaffected by z, blur or pixelation, and an editor whose scripts draw no cursor (2.0.0) shows the system cursor. Leaving a level played from the editor through the pause button now returns to the editor (it crashed in `updateMenuPage`).

## Changes since 2.1

Everything below was added after the 2.1 release and is part of 2.2. It is grouped by what it touches; the physics work is a comparison of the engine's functions with the Ghidra decompile of each retail exe.


### Lua compiler fix (constants beyond 256)
In a function with more than 256 constants, an expression such as `x + 800 * t.dir.x` was compiled so that the constant's register was overwritten by the second operand, giving `x + t.dir.x * t.dir.x`. The compiler now evaluates the second operand first (the same order later Lua versions use), for arithmetic and comparisons. Large scripts, such as a long game-logic file, now calculate these expressions correctly.

### Physics: Angry Birds 1.6.2 and the other Box2D 2.1 games (Classic, Seasons)

- Continuous collision follows the exe's own routine (the time of impact pass ported from 0x4e70d0): the earliest impact of all contacts that involve a static body is found, the bodies are moved there, the touching island is solved with the rest of the step and the pass repeats. `AB_OLDTOI=1` brings back the bundled Box2D routine, `AB_NOCCD=1` turns continuous collision off.
- `b2Contact::Update` checks the overlap of the two fixtures' boxes before it runs the narrow phase, as the exe does; the wake-up rules of `b2Body` and the term order of `Solve33` in `b2Math` match the exe.
- The island solver, the position solver's equalised mass and the TOI island follow the exe. Blocks are created awake, and every level load starts a brand new physics world (`AB_KEEPWORLD=1` restores the shared world). A restarted level now settles like the first load.
- Revolute and prismatic joints with both a motor and limits reverse at their limits by themselves (the Classic scripts never call `checkJointLimits`), which makes the Golden Eggs pickaxes move.
- The Eagle can be used once per level, as in the retail game.
- Seasons 1.0.0 to 2.2.0 use the same Box2D code as 1.6.2; the exes differ only in compiler inlining, so no separate physics is needed.

### Physics: Angry Birds Rio 1.4.2

Rio's exe contains a Box2D 2.2.0 trunk build with its own time of impact solver, which the engine now follows:

- `Island::SolveTOI` works on the earliest-impact contact, advances each body's sweep, and the "leap of faith" that commits the new positions applies to **every** body of the island, not only the two bodies of the contact.
- The group of bodies of a time of impact island stops growing at 32 bodies (`b2_maxTOIContacts`), the contact list has room for 4 times that.
- Contact partition, the sleep condition (it no longer depends on whether the position solver finished), the position solver's mass normalisation, its -1.5 x linear slop threshold and the TOI position solver (which moves both bodies) match the exe. Constants: linear slop 0.05, TOI target 0.15, tolerance 0.0125, 20 position iterations, Baumgarte 0.75.
- Rio instruction matching (v2.2): the position solver subtracts both radii in full (`b2_solverSkinFraction` = 1), the circle manifold returns the normal (1, 0) for coinciding centres, revolute and prismatic joints use the exe's 2.1-style Init / SolveVelocity / SolvePosition (including the large-detachment step), `b2Mat33::Solve33` uses the exe's term order, `b2Body::SetType` / `ResetMassData`, `AddPair` (no waking) and `Collide` (awake test) follow the exe, and the world is stepped with 10/10 iterations. Smuggler's Plane 1-10 still holds and 12-15 stays stable.

### Physics: how closely it follows the exes

The Box2D code of each game was compared, function by function, with the Ghidra decompile of that game's exe:

- **Angry Birds Rio 1.4.2** (`AngryBirdsRio.exe`, Box2D 2.2.0 trunk with 2.1-style joints): every Box2D function the engine uses was compared and made to follow the exe, including the order of the floating point terms. That covers the world step and solve, the time of impact pass, the contact manager (AddPair, Collide), the contact and position solvers and the polygon / circle manifolds, the island solve, the revolute, prismatic, weld and distance joints, bodies (SetType, mass data), the shapes' mass, centroid and ray casts, the GJK distance routine, and the dynamic tree and broadphase (the exe has the older 2.2.0 tree with a rebalance pass after each pair update, which decides the order contacts are created in).
- **Angry Birds 1.6.2** (Box2D 2.1.0): the world, the time of impact pass, the island and contact solvers, the joints and the collision functions follow the exe.
- **Angry Birds 2.0.0**: its decompile was compared with the 1.6.2 one. Of the Box2D functions, all but a few dozen are identical after the names are stripped, and the rest differ only in addresses, so 2.0.0 runs the same physics as 1.6.2.

What cannot be matched is the last bit of `sin`, `cos` and `sqrt`: the exes call the x87 versions, the engine uses the host's math library. Apart from that the physics in these games is the exes' physics. This was done by reading the decompiled code, not by comparing with recordings of the retail games, so a mistake in a function the engine does not run (or in a case no level reaches) would not have shown up.

### Drawing and speed

- **Static blocks that tile a texture draw correctly again.** A file used both as a sprite sheet image and as a repeating block texture is now kept as two textures (one that repeats, one that does not). When both wanted the same file, terrain showed as one flat colour.
- **Textures are uploaded when the sprite sheet is created**, behind the loading screen, like the exe does, instead of on the first draw. The frames after a menu or a level opened used to stutter, the more so the more sheets a game has.
- **A sprite name that is in no sheet is remembered**, like one that is. Before, every `drawSprite`, `getSpriteBounds` or `getSpritePivot` call with a name that does not exist walked every sprite of every sheet (about 40 microseconds in Rio, per call); scripts that measure or draw such names for each object each frame lost many milliseconds per frame. Composite sprite lookups are remembered too (a missing name now costs about 2.5 microseconds).
- **The hand cursor** (`CURSOR_*`) is now drawn at the very end of the frame, on top of everything the scripts and the engine draw (a background, a panel or the game drawn after the script's cursor call used to cover it, as in the Seasons editor), with the clip rectangle off and with z, blur, pixelation, transparency and tint of the sprites before it taken away. The system cursor is only hidden when a cursor sprite was really drawn that frame.

### Angry Birds Space (PC)

The engine starts Space's scripts and gets through the splash screen. The level loader, camera, planet gravity, compound body, aiming aid and shader natives are not finished, so levels do not load yet.

- Space's scripts are AES encrypted with their own key, which the engine knows; they are read from `scripts_common` as well as `scripts` (`config.lua`'s `commonScriptPath`).
- The natives Space's `gamelogic.lua` expects at start that the other games do not have (bitwise or, vector helpers, device id, time differences, Flurry and logging calls, timers) are provided by a built-in Lua file, `space_natives.lua`.

### Fixes in earlier 2.2 builds

See "New in 2.2" above for video, `setRenderState` extensions, tint, console commands, speed controls, the frame limiter and the `dec` folder.

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

Collider types decide which objects can hit which, as in Angry Birds 3.0.1 and the last Seasons (ghost blocks). The engine only provides the numbers and the commands; the names are a table in the game's own script. In Angry Birds 3.0.1 that table is `collider_types` in `blocks.lua`. The other games do not have it, so to use names there add the same table to your own `blocks.lua` (or `gamelogic.lua`) as a plain global array:

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
| a block, bird or other physics object (`drawGameNative`) | its `z_order` (from the level / block definition, or `setObjectZOrder`). In Rio every object has one; in the earlier games it counts only when `setObjectZOrder` was used, otherwise it is 0 | the game draws them in its own groups (see below) |
| a sprite, `drawRect` or `drawLine2D` after `setRenderState(..., z)` with `z` not 0 | that `z` | placed by z, see the rule below |
| everything else: sprites with `z` 0, text, backgrounds, foregrounds, particles, trajectory dots, the hand cursor | none | in the order the script (or the engine) drew it; never moved |

Because `z` is reset by `setRenderState` like the other render state values, set it right before the sprites that need it and set it back to 0 afterwards.

**The groups of `drawGameNative`.** The game draws the physics objects in this order, whatever their z: static blocks with `z_order` below 5, the other blocks below 5, the trajectory dots and puffs, the birds (and objects that draw with the birds) below 5, and last everything with `z_order` 5 or more. Inside a group Rio sorts by `z_order` (equal z: creation order); the earlier games keep creation order unless `setObjectZOrder` was used for an object of that group. The objects therefore keep their own order, and a sprite with `z` is fitted around them.

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

## Command line and environment variables

    AngryBirdsEngine.exe [options]

| Option | What it does |
|---|---|
| `--scripts <folder>`, `--data <folder>` | where the game's scripts and data are |
| `--config <file>` | window settings (title, size, fullscreen, cursor) |
| `--userdata <folder>` | keep saves here (`.` = working folder); default `original` shares progress with the original game |
| `--decrypt [folder]` | write the game's scripts into a plain-text `dec` folder and exit (details below) |
| `--use-dec [folder]` | read the scripts from that folder (where a file exists there) instead of the game's own |
| `--cmd "<line>"`, `--startup <file>`, `--prompt` | console commands run before the game starts (see "New in 2.2") |
| `--fps N`, `--vsync` | frame-rate cap (default none) and vsync (default off) |
| `--size <w> <h>`, `--screenshot <file> <frames>`, `--click`, `--drag`, `--key`, `--wheel`, `--audio-out` | test-render helpers: run for some frames, feed input, write a picture or the audio |

### The console (typing into the command prompt window)

The window that opens behind the game accepts typed lines. Click into it, type a line and press Enter; the game keeps running.

| You type | What happens |
|---|---|
| `help` | lists these commands |
| `box2d` | shows which physics library is running |
| `box2d 21` / `box2d 22` | choose Box2D 2.1.1 or 2.2.0; it is switched when the next level loads (restart the level). The same as the `AB_BOX2D` variable, without restarting the engine |
| `polyradius 0.1` | collision skin of boxes and polygons made from now on (Box2D 2.2) |
| `lua <code>` or just the code | runs Lua among the game's scripts, for example `lua print(score)` or `lua setAlpha(0.5)` |

Environment variables, set in the command prompt before starting the engine (`set NAME=value`, `set NAME=` to clear):

| Variable | What it does |
|---|---|
| `AB_BOX2D=21` or `22` | choose the physics library: 2.1.1 (Classic, Seasons) or 2.2.0 (Rio). Without it the engine picks the game's own. For example `set AB_BOX2D=21` makes Rio use 2.1 |
| `AB_POLYRADIUS=r` | collision skin of boxes and polygons (2.2 only, default 0.1) |
| `AB_SOLVERSKIN=f` | how much of the skin the 2.2 position solver keeps between blocks (default 0; 1 is stock Box2D and pushes flush stacks apart) |
| `AB_NOCCD=1` | switch off continuous collision for fast objects |
| `AB_VELITERS=n`, `AB_POSITERS=n` | velocity / position solver iterations per step (the engine uses the scripts' own 10/10 for Rio, like the exe) |
| `AB_NOWARM=1` | switch off warm starting (tests) |
| `AB_JOINTDBG=<text>` | print the joints created for objects whose name contains the text |
| `AB_CONSOLE=1` | keep the typed console on in a `--screenshot` run (tests) |
| `AB_NODEBUGKEYS=1` | run without the developers' keys and the editor button |
| `AB_SLOWFRAMES=1` | print every frame that took over 25 ms |
| `AB_CONTACTDBG=<text>`, `AB_PHYSDBG=1`, `AB_BODYDUMP=n` | physics debugging: contacts whose names contain the text, general physics log, positions of every body every n steps |
| `AB_DMGDBG=1` | prints every contact that damages or destroys a block (`DMG`, `DESTROYED`) |
| `AB_TESTDT=<seconds>` | frame step used by `--screenshot` runs (default 1/30) |
| `AB_EXEC='<lua>'`, `AB_EXEC_FRAME=n` (and `AB_EXEC2`, `AB_EXEC3` with their `_FRAME`) | run a Lua snippet in the scripts' environment at a frame, for tests |

## The `dec` folder (plain-text scripts)

Next to the exe the engine can keep a folder called `dec` with the game's scripts as plain text:

    cd /d "C:\path\to\the game"
    C:\path\to\AngryBirdsEngine.exe --decrypt

This writes `dec` next to the exe (or give a folder: `--decrypt C:\somewhere\dec`) and prints how many files are plain-text Lua source and how many are compiled. Then start the engine with `--use-dec` and it reads every script it finds in `dec` instead of the encrypted one, so you can edit them with any text editor.

- **Angry Birds 1.6.2** ships encrypted Lua source: the `dec` files are readable and editable, and the engine runs your edits.
- **1.6.3.1, 2.0.0, Seasons, Year of the Dragon and Rio** ship compiled Lua 5.1 bytecode. Since 2.2 `--decrypt` decompiles it with unluac (needs Java, see "New in 2.2"); files that cannot be decompiled are listed and not written, so `--use-dec` keeps using the game's own copy of those.
- A file you put into `dec` that the game does not have is ignored; a file that is missing from `dec` is read from the game as usual.
- The extra commands above (alpha, collider types, polygon skin) work in plain-text scripts of any game.

## Known gaps

- 2.0.0: a faint flash of the pause page after the tutorials.
- Editors: the Seasons editors start with the camera zoomed out on an empty "new level" page; a level that has no birds crashes when playtested (Shift+P), as it would in the original. Ctrl+S writes into the game's `data/levels` folder, so a game installed in `C:\Program Files (x86)` needs the engine started as administrator (or the game copied elsewhere).
- Rio: LevelPL2_309 is open. Smuggler's Plane 11-10 and the rest of Rio's physics were checked by the author against the retail game and behave the same. The physics was matched against the decompiled code, not retail recordings (see "Physics: how closely it follows the exes").
- Angry Birds Space: starts and shows its splash screen, but levels do not load yet (see "Changes since 2.1").
- 2.2: Mauro's early score points and Golden Beachball 5 were not reproduced in the test harness. Ham 'em High 12-1 creeps slightly after a first load and settles fully after a restart (Box2D contact order, not fixed).
- A few natives are not implemented (for example the colour table behind `drawBoxNative` and `setupCompoObject`).
- Windows only so far; the code is plain SDL2 and should port, but only Windows is tested.

## Build (Windows, Visual Studio + CMake)

    cd /d C:\path\to\engine
    cmake -S . -B build -A Win32
    cmake --build build --config Release
    ctest --test-dir build -C Release

(Use `-A x64` for a 64-bit build.) SDL2 is downloaded and built on the first configure if CMake cannot find it (internet needed). The program is `build\Release\AngryBirdsEngine.exe` (`Debug` for a debug build).

## Run

Start it from inside the game's folder, the one that holds `config.lua`, so it reads that game's settings (the folder name of the data comes from `config.lua`):

    cd /d "C:\Program Files (x86)\Rovio\Angry Birds"
    C:\path\to\AngryBirdsEngine.exe

It works the same for Seasons and for 2.0.0, just from their own folders. You can also name everything explicitly:

    AngryBirdsEngine.exe --scripts "<game>\data\scripts" --data "<game>\data" [--config <file>] [--userdata <folder>]

You can also put the exe next to the game's `data` folder, where the original exe sits. Do not name it `angrybirds.exe`: Windows treats that as the original's name and would overwrite it.

### Options

- `--scripts <folder>`, `--data <folder>`: where the scripts and data are.
- `--config <file>`: window settings (title, size, fullscreen, cursor); by default the `config.lua` above `--data`.
- `--size <width> <height>`: window size for `--screenshot` test renders.
- `--userdata <folder>`: keep saves in this folder instead (`.` is the working folder). The default, `original`, uses `%APPDATA%\<publisher>\<product>` (from `config.lua`) and shares progress with the original game. The first overwrite of a file keeps a copy as `<file>.original-backup`.

### Controls added by the engine

- Alt+Enter: window / full screen.
- Hold Shift: the game runs at twice the speed.
- Ctrl+minus / Ctrl+plus: slow down / speed up in 0.25x steps (0.25x to 8x) until the game closes.

Set the environment variable `AB_SLOWFRAMES=1` to print a line for every frame that took over 25 ms (a debugging aid for stutter).

### Debug keys and the level editor

The developers' tools in the scripts are switched on in every game (set `AB_NODEBUGKEYS=1` to run without them). `releaseBuild` itself stays on, because turning it off unlocks every level and the whole game; only the code that reads the debug keys is told it is off.

- 8: draw the hit boxes (sleeping objects in another colour), 9: hide / show the background, 0 or F: frames per second.
- C (or K in the older versions): clear the level. G (Rio): fail the level. Space (Rio): single step. Shift+Left / Shift+Right: previous / next level (where the scripts have it, for example 2.0.0).
- Main menu, "Editor" button: pick a level and edit it. Ctrl+S saves it over the level file (an encrypted original becomes a plain Lua file, which the game reads just the same; a copy for playtesting goes to `temp/` next to it). Shift+P plays the level you are editing, and returns to the editor when you leave it. Tab switches the physics on and off, Shift+C / Shift+B store the castle / bird camera, Delete removes the selection. Right button drag selects, left button drag moves the selection; the Rio block palette is on the keys of `groups.lua` (Q/W wood, A/S glass, 1/2 stone and so on). The level files are written into the game's own `data/levels` folder.

Everything else is the original game's own keyboard and mouse handling (for example R restarts a level in the versions where the scripts support it).

## How it works

The original executable embeds a Lua 5.1 interpreter and about 130 native functions. The scripts call these for drawing, physics, audio, files and input. This project reimplements those natives, working from the decompiled exe, so that everything above them, the actual game, is the original code.

| Folder | What it holds |
|---|---|
| `src/lua/` | Script host, the registered natives (`lua_api`), the AES script/level/save decryption, the Lua side of the frame update |
| `src/game/` | Collision and damage rules, the per-frame update (exe 0x40b670, 0x408980) |
| `src/physics/` | Box2D world, bodies, joints and the exe's collision filter |
| `src/engine/` | `GamePlatform`: level loading, objects, particles, sprite sheets, fonts, input, drawing |
| `src/audio/` | Mixer for WAV and MP3 |
| `src/window/` | SDL2 window, OpenGL backend, command line |
| `third_party/` | Lua 5.1 (float numbers), Box2D 2.1.1 and 2.2.0 with the exe's tuned constants, minimp3 |
| `tests/` | Unit tests and the script boot / level / render test programs |

The research behind the code, with the exe addresses and the guesses still to check, is in [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md).

## Tests

    cmake --build build --config Debug
    ctest --test-dir build -C Debug

## Contributing

Bug reports and findings from the original executable are welcome as issues. Please do not attach game files.

## Licence

The code in this repository is MIT licensed (`LICENSE`). Lua, Box2D, minimp3 and h264bsd in `third_party/` keep their own licences, see the `LICENSE` files in their folders.
