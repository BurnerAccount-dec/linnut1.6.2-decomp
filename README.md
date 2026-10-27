# Angry Birds PC engine (unofficial, C++)

An unofficial, from-scratch reimplementation of the Angry Birds PC engine, rebuilt from a Ghidra decompile of the 1.6.2 executable. It is written in C++ (SDL2, OpenGL 1.x, Lua 5.1, Box2D 2.1 and 2.2) and does not re-implement the game logic: it runs the game's **original Lua scripts** and provides the native functions those scripts expect, so levels, menus, physics and scoring behave like the original.

Version 1.9. What works, per game:

| Game | Play | Level editor | Notes |
|---|---|---|---|
| Angry Birds 1.0.0 (PC) | Playable | Works | The backgrounds of the first five packs draw now |
| Angry Birds 1.6.2 / 1.6.3.1 (PC) | Playable | Works (tested in 1.6.3.1, 1.6.2 uses the same script) | 1.6.3.1 has no editor in its scripts, the engine brings Rovio's 1.6.2 one |
| Angry Birds 2.0.0 (PC) | Playable, a few known visual gaps (below) | Works, "Editor" button on the main menu | |
| Angry Birds Seasons 1.0.0 (PC) | Playable | Works, "Editor" button on the main menu | |
| Angry Birds Seasons 2.2.0, Year of the Dragon (PC) | Playable | The level name, status line and block palette draw (checked with Year of the Dragon files) | |
| Angry Birds Rio 1.4.2 (PC) | Playable | Works | Ice, glass and light blocks are stable (Mauro's level, Smuggler's Plane 12-8 and 12-15 tested against Box2D 2.1) |

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

## Extra script commands (new in 1.8)

These are not in any of the supported games; the engine adds them so scripts can use them, the same way they use `physicsTimeStep`. Nothing changes for the original scripts.

### Transparency (alpha)

`setRenderState(tx, ty, sx, sy, angle, pivotX, pivotY, alpha)`: the 8th value is the transparency of everything drawn after it, from 0 (invisible) to 1 (opaque); out-of-range values are clamped. `setAlpha(a)` does the same. The value stays until you set it again, and a call with only 7 values does not reset it, so always put it back to 1 when you are done:

    setRenderState(-screen.left, -screen.top, worldScale, worldScale, 0, 0, 0, 0.4)   -- everything after this is 40% visible
    drawSprite("BLOCK_WOOD_1", x, y)
    setRenderState(-screen.left, -screen.top, worldScale, worldScale, 0, 0, 0, 1)     -- back to solid

Masked and tiled sprites (the ground blocks) use it as well.

### Collider types

Collider types decide which objects can hit which, as in Angry Birds 3.0.1 and the last Seasons (ghost blocks). The engine only provides the numbers and the commands; the names are a table in the game's own script. In Angry Birds 3.0.1 that table is `collider_types` in `blocks.lua`. The other games do not have it, so to use names there add the same table to your own `blocks.lua` (or `gamelogic.lua`) as a plain global array:

    collider_types = { basic = 0, eagle = 1, static = 2, bird = 3, pig = 4, ghost = 5, sticky = 6 }

and keep going with your own numbers (the rule table holds 16 types, 0 to 15):

    my_types = { ice_ghost = 7, glass_wall = 8 }

Commands:

- `setColliderType(objectName, type)` / `getColliderType(objectName)`: give an object a type (call it after the object exists, for example right after `createObject`). The type is a number 0 to 15.
- `setColliderRule(typeA, typeB, collides)` / `getColliderRule(typeA, typeB)`: change one pair (it works both ways round). `collides` is `true` or `false`.
- By default every type collides with every other, except type 5 (ghost), which only collides with 1 (eagle) and 3 (bird). The sticky type (6) is stored but has no special behaviour yet.

Example, in a script that has the table above:

    setColliderRule(my_types.ice_ghost, collider_types.bird, true)
    setColliderRule(my_types.ice_ghost, collider_types.pig, false)
    setColliderRule(my_types.ice_ghost, my_types.glass_wall, false)

    local name = createObject(blockTable, "BLOCK_GLASS_1", nil, x, y)
    setColliderType(name, my_types.ice_ghost)

### Polygon skin

`setPolygonRadius(r)` / `getPolygonRadius()`: the collision skin of boxes and polygons created afterwards (Rio, Box2D 2.2 only; default 0.1, the skin only decides when blocks count as touching, it does not push stacked blocks apart).

## Native function summary

These are the functions the engine registers for the scripts (all in `src/lua/lua_api.cpp`). Several are only used by one game; the ones that only talk to online services, ads or purchases are stubs.

- **Window and input:** `requestExit`, `setBGColor`, `captureMouse`, `isMouseCaptured`, `setFullScreenMode`, `isInFullScreenMode`, `setResolution`, `setRetinaResolution`, `setIsMultitouchMouseWheelSimulationEnabled`, `doesMouseClickSetsTouchCount`, `getClipboardText`, `hasLocationCapability`.
- **Files, scripts and levels:** `loadLuaFileToObject`, `loadLuaFile`, `saveLuaFile`, `checkForLuaFile`, `createDirectory`, `checkDirectory`, `loadLevel`, `loadLevelFile`, `getLoadStatus`, `saveLevel`, `loadLevelEditor`, `setEditing`, `setGameOn`, `setGameParameters`.
- **Physics objects:** `createBox`, `createCircle`, `createPolygon` (with `clearVertices`, `addVertex`), `removeObject`, `setPosition`, `setRotation`, `setVelocity`, `setAngularVelocity`, `setSleeping`, `applyImpulse`, `applyForce`, `getVelocity`, `getAngle`, `getLocalPoint`, `getWorldPoint`, `setObjectParameter`, `getIntersectingObjects`, `hasMovingObjects`, `hasMovingObjectsZeroTolerance`.
- **Physics world:** `setPhysicsEnabled`, `isPhysicsEnabled`, `setPhysicsSimulationScale`, `setWorldGravity`, `setLevelLimits`, `setCollisionEnabled`; engine additions: `setColliderType`, `getColliderType`, `setColliderRule`, `getColliderRule`, `setPolygonRadius`, `getPolygonRadius`.
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
| `AB_VELITERS=n`, `AB_POSITERS=n` | velocity / position solver iterations per step (the engine runs 20 velocity iterations with Box2D 2.2) |
| `AB_NOWARM=1` | switch off warm starting (tests) |
| `AB_JOINTDBG=<text>` | print the joints created for objects whose name contains the text |
| `AB_CONSOLE=1` | keep the typed console on in a `--screenshot` run (tests) |
| `AB_NODEBUGKEYS=1` | run without the developers' keys and the editor button |
| `AB_SLOWFRAMES=1` | print every frame that took over 25 ms |
| `AB_CONTACTDBG=<text>`, `AB_PHYSDBG=1`, `AB_BODYDUMP=n` | physics debugging: contacts whose names contain the text, general physics log, positions of every body every n steps |
| `AB_EXEC='<lua>'`, `AB_EXEC_FRAME=n` (and `AB_EXEC2`, `AB_EXEC3` with their `_FRAME`) | run a Lua snippet in the scripts' environment at a frame, for tests |

## The `dec` folder (plain-text scripts)

Next to the exe the engine can keep a folder called `dec` with the game's scripts as plain text:

    cd /d "C:\path\to\the game"
    C:\path\to\AngryBirdsEngine.exe --decrypt

This writes `dec` next to the exe (or give a folder: `--decrypt C:\somewhere\dec`) and prints how many files are plain-text Lua source and how many are compiled. Then start the engine with `--use-dec` and it reads every script it finds in `dec` instead of the encrypted one, so you can edit them with any text editor.

- **Angry Birds 1.6.2** ships encrypted Lua source: the `dec` files are readable and editable, and the engine runs your edits.
- **1.6.3.1, 2.0.0, Seasons, Year of the Dragon and Rio** ship compiled Lua 5.1 bytecode. `--decrypt` still removes the encryption, but the result is bytecode, which is not readable text; a Lua decompiler is needed to turn it into source. Edit-by-text works for files you replace with your own plain Lua.
- A file you put into `dec` that the game does not have is ignored; a file that is missing from `dec` is read from the game as usual.
- The extra commands above (alpha, collider types, polygon skin) work in plain-text scripts of any game.

## Known gaps

- 2.0.0: a faint flash of the pause page after the tutorials.
- Editors: the Seasons editors start with the camera zoomed out on an empty "new level" page; a level that has no birds crashes when playtested (Shift+P), as it would in the original. Ctrl+S writes into the game's `data/levels` folder, so a game installed in `C:\Program Files (x86)` needs the engine started as administrator (or the game copied elsewhere).
- Rio: other levels than Mauro's, Smuggler's Plane 12-8 and 12-15 were not compared with the retail exe.
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

The code in this repository is MIT licensed (`LICENSE`). Lua, Box2D and minimp3 in `third_party/` keep their own licences, see the `LICENSE` files in their folders.
