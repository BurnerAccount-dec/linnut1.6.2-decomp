# Angry Birds PC engine (unofficial, C++)

An unofficial, from-scratch reimplementation of the Angry Birds PC engine, rebuilt from Ghidra decompiles of the original executables. It is written in C++ (SDL2, OpenGL 1.x, Lua 5.1, Box2D 2.1 and 2.2) and does not re-implement the game logic: it runs the game's **original Lua scripts** and provides the native functions those scripts expect, so levels, menus, physics and scoring behave like the original game.

**Version 2.7.** Changes are listed in [`CHANGELOG.md`](CHANGELOG.md).

**Not affiliated with, endorsed by or connected to Rovio Entertainment.** "Angry Birds" and everything in the game belong to their owners. This repository contains no game files: no scripts, images, sounds, levels or executables. To run it you need your own legally obtained copy of the game.

## Supported games

| Game | Play | Level editor | Notes |
|---|---|---|---|
| Angry Birds 1.0.0 (PC) | Playable | Works | The backgrounds of the first five packs draw |
| Angry Birds 1.6.2 / 1.6.3.1 (PC) | Playable | Works | 1.6.3.1 has no editor in its scripts, the engine brings Rovio's 1.6.2 one |
| Angry Birds 2.0.0 (PC) | Playable, a few known visual gaps | Works ("Editor" on the main menu) | |
| Angry Birds Seasons 1.0.0 and 2.2.0 (PC) | Playable | Works | 2.2.0 (Year of the Dragon) has the theme menus with parallax and its own cursor |
| Angry Birds Rio 1.4.2 (PC) | Playable | Works | Physics follows the exe's own Box2D 2.2 solver; ice ("light") blocks are stable |
| Angry Birds Space (PC) | Levels load and play (first theme tested) | Yes (F2) | Planet gravity, aiming aid and camera work; later themes, bosses and compound bodies are less tested |
| Angry Birds Star Wars (PC) | Playable (Hoth, Tatooine and Endor levels load); contact filter, damage, water, kill timers, per-frame sync and the aiming aid follow the exe | Not tested | Known gaps below |

## Quick start

1. **Build it** (Windows, Visual Studio and CMake; see [Build](#build)) or take the exe from a release zip (`AngryBirdsEngine-v2.7.zip`, made by `make_release.bat`).
2. **Start it from inside the game's folder**, the one that holds `config.lua`:

       cd /d "C:\Program Files (x86)\Rovio\Angry Birds"
       C:\path\to\AngryBirdsEngine.exe

   It works the same for every supported game, from its own folder. Do not name the exe `angrybirds.exe`: Windows treats that as the original's name and would overwrite it.
3. Saves go to `%APPDATA%\<publisher>\<product>` and are shared with the original game; `--userdata .` keeps them next to the engine instead.

More in [`docs/USAGE.md`](docs/USAGE.md): the command line, controls, the typed console, debug keys, the level editors and the plain-text `dec` folder.

## What works

- The original scripts and levels, including the encrypted ones (AES), run unmodified.
- Box2D physics with each exe's own settings (2.1 for Classic and Seasons, 2.2 for Rio, Space and Star Wars), the original collision and damage rules and the fixed frame update; Star Wars additionally has its own contact filter, damage, water and kill-timer rules.
- Level editors in the games that have one, saving over the original level file.
- Sprite sheets, bitmap fonts, particles (including level and menu particle layers and parallax), OpenGL 1.x drawing with scaling, rotation, transparency, tint and blend modes.
- Audio (WAV, MP3), video cutscenes (H.264 in MP4), the game's loops.
- Menus, level select, tutorials, pause, restart and level-complete flows in all supported versions.
- Saves shared with the original game (encrypted and plain format).
- A DPI-aware resizable window (the scripts' `resolutionChanged` runs), smoothed frame times, a typed console and debug tools.
- For mod makers: extra script functions (collider types, alpha, silhouettes, collision prediction, per-object gravity, audio and graphics controls) and `enterCollision` / `exitCollision` callbacks, all documented in [`docs/SCRIPTING.md`](docs/SCRIPTING.md).

## Documentation

| File | What is in it |
|---|---|
| [`docs/USAGE.md`](docs/USAGE.md) | Running the engine, command line options, controls, debug keys and logging, the console, environment variables, the `dec` folder |
| [`docs/SCRIPTING.md`](docs/SCRIPTING.md) | Everything for script writers and modders: the functions the engine adds, the native function summary, Star Wars parameters and callbacks |
| [`CHANGELOG.md`](CHANGELOG.md) | What changed in each version |
| [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) | Research notes with exe addresses and the guesses still to check |

## Known gaps

- 2.0.0: a faint flash of the pause page after the tutorials.
- Editors: the Seasons editors start with the camera zoomed out on an empty "new level" page; a level that has no birds crashes when playtested (Shift+P), as it would in the original. Ctrl+S writes into the game's `data/levels` folder, so a game installed in `C:\Program Files (x86)` needs the engine started as administrator (or the game copied elsewhere).
- Rio: Smuggler's Plane 11-10 and the rest of Rio's physics were checked by the author against the retail game and behave the same. The physics was matched against the decompiled code, not retail recordings (see "Physics: how closely it follows the exes" in `CHANGELOG.md`).
- Angry Birds Space: levels of the first theme load and play; the other themes, bosses and compound bodies are untested.
- 2.2: Mauro's early score points and Golden Beachball 5 were not reproduced in the test harness. Ham 'em High 12-1 creeps slightly after a first load and settles fully after a restart (Box2D contact order, not fixed).
- Not done yet in Space: the credits level's text placement and hitboxes, the particle limits, the level-select scrolling force, 1:1 graphics functions and the planet level-file parameters are still being compared with the retail exe.
- Shader programs you write yourself (GLSL) are not possible with the OpenGL 1.x renderer; `setShader` has built-in presets. The natively drawn backgrounds, foregrounds, trajectory dots and aiming aid can be styled through the targets `"background"`, `"foreground"`, `"trajectory"` and `"particles"`, but their layout is not yet editable from Lua.
- A few natives are not implemented (for example the colour table behind `drawBoxNative` and `setupCompoObject`).
- Windows only so far; the code is plain SDL2 and should port, but only Windows is tested.
- Angry Birds Star Wars: the Cloud City levels draw too high and the space-theme backgrounds of Hoth are not right yet; leaving a level through the level selection can crash on some saves (deathstar_1). Logs for these (`AB_DRAWDBG=1`, see `docs/USAGE.md`) are the most useful thing to send. The Star Wars level editor is untested, `renderGravityVisualsNative` draws nothing in the game's own scripts (as in the exe), and the `STREAM_1` editor block is untested.

## Build

    cd /d C:\path\to\engine
    cmake -S . -B build -A Win32
    cmake --build build --config Release
    ctest --test-dir build -C Release

(Use `-A x64` for a 64-bit build.) SDL2 is downloaded and built on the first configure if CMake cannot find it (internet needed). The program is `build\Release\AngryBirdsEngine.exe` (`Debug` for a debug build).

`make_release.bat` builds a release configuration and zips the exe with the licences into `AngryBirdsEngine-v2.7.zip`.

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

The physics tests cover Box2D 2.1.1 and 2.2.0, the Rio and Star Wars rules (contact filter, water, kill timers, line shapes) and the frame update.

## Contributing

Bug reports and findings from the original executables are welcome as issues. Please do not attach game files. For a drawing or physics bug, say which game and level and attach the last lines of the debug output (see `docs/USAGE.md`).

## Licence

The code in this repository is MIT licensed (`LICENSE`). Lua, Box2D, minimp3 and h264bsd in `third_party/` keep their own licences, see the `LICENSE` files in their folders.
