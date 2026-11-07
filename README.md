# Angry Birds PC engine (unofficial, C++)

An unofficial, from-scratch reimplementation of the Angry Birds PC engine, rebuilt from a Ghidra decompile of the 1.6.2 executable. It is written in C++ (SDL2, OpenGL 1.x, Lua 5.1, Box2D 2.1 and 2.2) and does not re-implement the game logic: it runs the game's **original Lua scripts** and provides the native functions those scripts expect, so levels, menus, physics and scoring behave like the original.

Version 2.3. What works, per game:

| Game | Play | Level editor | Notes |
|---|---|---|---|
| Angry Birds 1.0.0 (PC) | Playable | Works | The backgrounds of the first five packs draw now |
| Angry Birds 1.6.2 / 1.6.3.1 (PC) | Playable | Works (tested in 1.6.3.1, 1.6.2 uses the same script) | 1.6.3.1 has no editor in its scripts, the engine brings Rovio's 1.6.2 one |
| Angry Birds 2.0.0 (PC) | Playable, a few known visual gaps (below) | Works, "Editor" button on the main menu | |
| Angry Birds Seasons 1.0.0 (PC) | Playable | Works, "Editor" button on the main menu | |
| Angry Birds Space (PC) | Levels load and play (first theme tested) | Not tried | Planet gravity, aiming aid and the camera work; see "Angry Birds Space (PC)" |
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

## New in 2.3

**Angry Birds Space compatibility update.** Space's physics now follows its own exe instead of Rio's: its Box2D is a mix of 2.2 collision with 2.3 broad phase, island, sleep and solver rules (rotating dynamic tree, position-solved sleeping, 2.3 position-solver constants, a larger time-of-impact island, `setStatic` through `SetType`). The planets' pull, the aiming aid and the per-object gravity functions share one code path that follows the exe's gravity function. See "Engine-added script functions: full reference" for the new script functions.

**Space natives that used to do nothing.** `setRecordVelocity(name, on)` (publishes `xVel`, `yVel` and `angle` into `objects.world[name]` for that object), `applyForceNative(name, fx, fy, px, py)` (the same force as `applyForce`) and `setRevoluteJointSpeed(jointName, speed)` (sets the motor speed of the joints with that name, as the exe does), `setSpriteRotation(name, angle)` (the sprite turns by this much on top of the body's angle), `setPivotOffset(name, x, y)` (moves the point the sprite turns about), `setTextureScale(name, scale)` (stored; the tile draw does not use it yet) and `drawTexturedLine2D(sprite, x1, y1, x2, y2, width [, r, g, b, a])` (the sprite stretched along a line, used for the slingshot's rubber bands), `setNormalTrailSprite(name)` / `setSpecialTrailSprite(name)` (the dot and puff sprites of the trajectory the next shot draws) and `setDecorationObjects(name)` (the copies of a block definition's `decorations` sprite drawn about the object, as on the huge planets) now work.

**More Space natives.** `createThemeAnimation(table)` (a theme sprite whose picture cycles through the `animation` list every `animDelay` seconds; fixes the Red Planet crash), `getLinearVelocity(name)` (`vx, vy`), `getBGColor()` (`r, g, b`, 0 to 255), `getRayCastedObjects({x1=, y1=, x2=, y2=})` (a flat list, six entries per non-sensor object the segment crosses: name, hit x, hit y, normal x, normal y, fraction) and `createCompoundBodyFromTable(t)` (a compound built from a table with `name`, `x`, `y`, `material` and `children`, each child with `x`, `y`, `angle` and a `blockData` of type box, circle or polygon). `addParticlesWithScale` and `addParticlesWithProperties` now apply their extra arguments, and Space particle definitions honour `useAngleFromSpawner`, `minAngle` / `maxAngle`, `emitAreaScaleX/Y` and their own `ignoreLimits`.

**Space planet gravity.** The pull follows the exe's contact list: one entry per touching fixture pair (an object made of several fixtures is pulled once per pair), applied last entry first and only to objects that are awake; a gravity sensor with parameter 19 sets the linear damping of objects flagged with parameter 1 or 34 to 100 when they leave it.

**Space level limits.** `setLevelLimits(minX, minY, maxX, maxY)` keeps all four limits and an object that moves outside them is removed; the old rule (anything below y 20 is gone) is now only used by the other games. It made the Utopia boss (4-30, which spawns its popcorn at y 60) and the Red Planet boss (5-30) delete themselves on the first frame, and removed birds that flew low in some planet levels.

**Space and Rio draw order.** See "Draw order and z": one pass over the z layers, as the exes do.

## New script functions in 2.3 (modding)

Every one of these is native, so it works in every game and version the engine runs. The graphics setters take an optional **target** as their last argument (see "Graphics settings for one target").

### Objects and physics

| Function | What it does |
|---|---|
| `setScale(name, sx [, sy])`, `getScale(name)` | The scale the object's picture is drawn with: the same value `setObjectParameter(name, 5, v)` sets, without going through it. Leave `sy` out for a uniform scale. `getScale` returns `sx, sy` (nil if there is no such object). It scales the picture only; `resizeCircle` changes the shape (the Time Travel puffer bird calls both). |
| `setSpeedCap(name, maxSpeed [, maxVx, maxVy])`, `getSpeedCap(name)`, `clearSpeedCap(name)` | A limit for one object: after every physics step its velocity is brought back under `maxSpeed` (length) and, if given, `maxVx` / `maxVy` (0 or nil = no limit on that one). It is applied on top of Box2D's global `setMaxTranslation`, which still limits how far any body may move in one step (to let a capped object go faster than that, raise `setMaxTranslation`). |
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

New commands: `text <group> <key> <text>`, `texts [group]`, `gravity [x y]`, `maxtranslation <t>`, `scale <object> <sx> [sy]`, `speedcap <object> <speed>`, `blend <object> <mode>`, `shader <object> <name>`. Anything else typed is still run as Lua (`lua getTouchingObjects("pig_3")`), and since every native is a Lua function, anything the engine does can be changed from there.

### Prototype build and window

The folder of the Angry Birds Space prototype (`AngryBirdsSpace_v1.0.0_proto_Windows`) is accepted: its `SettingsWrapper` global and the bit functions its scripts call are provided. The command prompt closes with the game when the game is closed normally; after a crash it stays open so the message can be read.

## New in 2.2.4

**Angry Birds Space.** Levels load and play (see "Angry Birds Space (PC)"). Space uses Box2D 2.2.0 and no world gravity; contacts with sensors go to `enterCollision` / `exitCollision`.

## New in 2.2.3

**Reading and changing the z of objects.** Everything about the `z_order` of blocks can now be asked for and changed from the scripts and the console (the rules of the draw order are under "Draw order and z"):

| Call | Meaning |
|---|---|
| `getObjectZOrder(name)` | the object's current `z_order`; `nil` if there is no such object (before: -1e9) |
| `objects.world[name].z_order` | the same value, from the table the scripts already read. It is kept up to date: `setObjectZOrder`, `bringObjectToFront`, `sendObjectToBack` and the console commands below write the new value into it |
| `setObjectZOrder(name, z)` | sets it (as before; now also updates `objects.world`) |
| `bringObjectToFront(name)` | puts the object in front of every other one and returns its new `z_order` (the highest z in the world plus 1, at least 5, so it is in the group that is drawn last in every game); `nil` if there is no such object |
| `sendObjectToBack(name)` | puts it behind every other object of its group (the lowest z in the world minus 1) and returns the new `z_order`. The static blocks below z 5 are drawn first whatever z the others have, so a moving block cannot go behind those |
| `getObjectDrawOrder()` | a table of object names from the back to the front, in the order `drawGameNative` draws them (the trajectory dots are not objects and are not listed) |
| `getRenderZ()` | the `z` of the current render state (`setRenderZ(z)`, which already existed, changes only that value) |

Console: `zorder` lists every object back to front with its `z_order`; `zorder <name>` shows one and where it is drawn; `zorder <name> <z>` sets it; `zfront <name>` and `zback <name>` bring it to the front or send it to the back. For example `zorder BLOCK_WOOD_3 20`.

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
| `setRenderZ(z)` / `getRenderZ()` | the z of the render state alone (argument 13 of `setRenderState`), see "Draw order and z" |
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

Space's menus and levels load, draw and play: the level is built by the scripts, the planets pull the birds and blocks (gravitation sensors, Box2D 2.2.0 like Rio and no world gravity), the slingshot launches the bird and the aiming aid draws its dots. Only the first theme's levels were tested. Still approximate: the background layers (positions and parallax), the aiming aid (it flies the bird on alone under the sensors and ignores what it would hit), the foreground, and the compound bodies, particle scale and several shader and font natives, which are provided but not tested against a level that uses them.

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
| `zorder`, `zorder <name>`, `zorder <name> <z>`, `zfront <name>`, `zback <name>` | list the objects back to front with their `z_order`, show or set one object's z, bring it to the front / send it to the back |
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
- Rio: Smuggler's Plane 11-10 and the rest of Rio's physics were checked by the author against the retail game and behave the same. The physics was matched against the decompiled code, not retail recordings (see "Physics: how closely it follows the exes").
- Angry Birds Space: levels of the first theme load and play; the other themes, bosses and compound bodies are untested.
- 2.2: Mauro's early score points and Golden Beachball 5 were not reproduced in the test harness. Ham 'em High 12-1 creeps slightly after a first load and settles fully after a restart (Box2D contact order, not fixed).
- Not done yet in Space: the credits level's text placement and hitboxes, the particle limits, the level-select scrolling force, the level editor, 1:1 graphics functions and the planet level-file parameters are still being compared with the retail exe. The Rio bird that stays on its yelling sprite when the rubber band is pulled is still to be fixed.
- Shader programs you write yourself (GLSL) are not possible with the OpenGL 1.x renderer; `setShader` has built-in presets. The natively drawn backgrounds, foregrounds, trajectory dots and aiming aid can be styled through the targets `"background"`, `"foreground"`, `"trajectory"` and `"particles"`, but their layout is not yet editable from Lua.
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
