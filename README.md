# Angry Birds PC engine (unofficial, C++)

An unofficial, from-scratch reimplementation of the Angry Birds PC engine, rebuilt from a Ghidra decompile of the 1.6.2 executable. It is written in C++ (SDL2, OpenGL 1.x, Lua 5.1, Box2D 2.1 and 2.2) and does not re-implement the game logic: it runs the game's **original Lua scripts** and provides the native functions those scripts expect, so levels, menus, physics and scoring behave like the original.

Version 1.8. What works, per game:

| Game | Play | Level editor | Notes |
|---|---|---|---|
| Angry Birds 1.0.0 (PC) | Playable | Works | The backgrounds of the first five packs draw now |
| Angry Birds 1.6.2 / 1.6.3.1 (PC) | Playable | Works (tested in 1.6.3.1, 1.6.2 uses the same script) | 1.6.3.1 has no editor in its scripts, the engine brings Rovio's 1.6.2 one |
| Angry Birds 2.0.0 (PC) | Playable, a few known visual gaps (below) | Works, "Editor" button on the main menu | |
| Angry Birds Seasons 1.0.0 (PC) | Playable | Works, "Editor" button on the main menu | |
| Angry Birds Seasons 2.2.0, Year of the Dragon (PC) | Playable | Same script and native as 1.0.0, not tested on the real Year of the Dragon files | |
| Angry Birds Rio 1.4.2 (PC) | Playable | Works | Ice, glass and light blocks are stable now (Mauro's level, Smuggler's Plane 12-8 tested) |

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

- `setRenderState(tx, ty, sx, sy, angle, pivotX, pivotY, alpha)`: the 8th value is the transparency of everything drawn after it, from 0 (invisible) to 1 (opaque); out-of-range values are clamped. `setAlpha(a)` does the same.
- Collider types, as in Angry Birds 3.0.1 and the last Seasons: the global table `collider_types` (`basic`, `eagle`, `static`, `bird`, `pig`, `ghost`, `sticky` = 0 to 6), `setColliderType(name, type)`, `getColliderType(name)`, `setColliderRule(typeA, typeB, collides)` and `getColliderRule(typeA, typeB)`. By default all types collide with each other, except ghost blocks, which only collide with birds and the eagle; change any pair from a script. The sticky type is stored but has no special behaviour yet.
- `setPolygonRadius(r)` / `getPolygonRadius()`: the collision skin of boxes and polygons created afterwards (Rio only, default 0.05).

## Known gaps

- 2.0.0: a faint flash of the pause page after the tutorials.
- Editors: the Seasons editors start with the camera zoomed out on an empty "new level" page; a level that has no birds crashes when playtested (Shift+P), as it would in the original. The Year of the Dragon editor was not tested against the real Year of the Dragon files. Ctrl+S writes into the game's `data/levels` folder, so a game installed in `C:\Program Files (x86)` needs the engine started as administrator (or the game copied elsewhere).
- Rio: other levels than Mauro's and Smuggler's Plane 12-8 were not compared with the retail exe.
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
