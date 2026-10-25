# Angry Birds PC engine (unofficial, C++)

An unofficial, from-scratch reimplementation of the Angry Birds PC engine, rebuilt from a Ghidra decompile of the 1.6.2 executable. It is written in C++ (SDL2, OpenGL 1.x, Lua 5.1, Box2D 2.1) and does not re-implement the game logic: it runs the game's **original Lua scripts** and provides the native functions those scripts expect, so levels, menus, physics and scoring behave like the original.

| Version | Status |
|---|---|
| Angry Birds 1.6.2 / 1.6.3 (PC) | Playable |
| Angry Birds Seasons 1.0.0 (PC) | Playable |
| Angry Birds 2.0.0 (PC) | Playable, a few known visual gaps (below) |
| Angry Birds 1.0.0 | Starts, with rendering and language problems |

**Not affiliated with, endorsed by or connected to Rovio Entertainment.** "Angry Birds" and everything in the game belong to their owners. This repository contains no game files: no scripts, images, sounds, levels or executables. To run it you need your own legally obtained copy of the game.

## What works

- The original scripts and levels, including the encrypted ones (AES), run unmodified.
- Box2D 2.1 physics with the exe's own settings, the original collision and damage rules, and the fixed 1/30 s frame update.
- Sprite sheets, bitmap fonts, particles (including 2.0.0's level and menu particle layers and parallax), OpenGL 1.x drawing with scaling and rotation.
- Audio (WAV, MP3) with the game's loops.
- Menus, level select, tutorials, pause, restart and level-complete flows in all three versions.
- Saves that are shared with the original game (both the encrypted and the plain format).

## Known gaps

- 2.0.0: a faint flash of the pause page after the tutorials.
- A few natives are not implemented (for example the colour table behind `drawBoxNative` and `setupCompoObject`).
- Windows only so far; the code is plain SDL2 and should port, but only Windows is tested.

## Build (Windows, Visual Studio + CMake)

    cmake -S . -B build
    cmake --build build --config Release

SDL2 is downloaded and built on the first configure if CMake cannot find it (internet needed). The program is `build\Release\AngryBirdsEngine.exe` (`Debug` for a debug build).

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
- `--userdata <folder>`: keep saves in this folder instead (`.` is the working folder). The default, `original`, uses `%APPDATA%\Rovio\<product>` and shares progress with the original game. The first overwrite of a file keeps a copy as `<file>.original-backup`.

### Controls added by the engine

- Alt+Enter: window / full screen.
- Hold Shift: the game runs at twice the speed.
- Ctrl+minus / Ctrl+plus: slow down / speed up in 0.25x steps (0.25x to 8x) until the game closes.

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
| `third_party/` | Lua 5.1 (float numbers), Box2D 2.1.1 with the exe's tuned constants, minimp3 |
| `tests/` | Unit tests and the script boot / level / render test programs |

The research behind the code, with the exe addresses and the guesses still to check, is in [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md).

## Tests

    cmake --build build --config Debug
    ctest --test-dir build -C Debug

## Contributing

Bug reports and findings from the original executable are welcome as issues. Please do not attach game files.

## Licence

The code in this repository is MIT licensed (`LICENSE`). Lua, Box2D and minimp3 in `third_party/` keep their own licences, see the `LICENSE` files in their folders.
