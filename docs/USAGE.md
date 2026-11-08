# Using the engine: running, options, debugging

How to start the engine, the command line, the console, the environment variables for debugging, the editors and the plain-text script folder. For building see the README, for script writers [`docs/SCRIPTING.md`](SCRIPTING.md).

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

## Command line and environment variables

    AngryBirdsEngine.exe [options]

| Option | What it does |
|---|---|
| `--scripts <folder>`, `--data <folder>` | where the game's scripts and data are |
| `--config <file>` | window settings (title, size, fullscreen, cursor) |
| `--userdata <folder>` | keep saves here (`.` = working folder); default `original` shares progress with the original game |
| `--decrypt [folder]` | write the game's scripts into a plain-text `dec` folder and exit (details below) |
| `--use-dec [folder]` | read the scripts from that folder (where a file exists there) instead of the game's own |
| `--cmd "<line>"`, `--startup <file>`, `--prompt` | console commands run before the game starts (see "New in 2.2" in `CHANGELOG.md`) |
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
| `text <group> <key> <text>`, `texts [group]` | sets or adds one text (what `res.getString` and `drawString` read), or lists the text groups / the texts of one group |
| `gravity [x y]`, `maxtranslation [t]` | shows or sets the world's gravity, and the longest distance a body moves in one physics step |
| `scale <object> <sx> [sy]`, `speedcap <object> <speed>`, `blend <object> <mode>`, `shader <object> <name>` | the same as `setScale`, `setSpeedCap`, `setBlendMode` and `setShader` for one object (`blend`: normal, add, multiply, screen; `shader`: none, grayscale, sepia, invert, chromatic, glow) |
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
| `AB_JOINTDBG=<text>` | print the joints created for objects whose name contains the text |; also logs the entry count of every `createJoints` call
| `AB_DUMPLEVEL=<prefix>` | writes the decrypted text of every loaded level to `<prefix>.<level>.lua` (compiled levels stay bytecode; `unluac` reads them) |
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
- **1.6.3.1, 2.0.0, Seasons, Year of the Dragon and Rio** ship compiled Lua 5.1 bytecode. Since 2.2 `--decrypt` decompiles it with unluac (needs Java, see "New in 2.2" in `CHANGELOG.md`); files that cannot be decompiled are listed and not written, so `--use-dec` keeps using the game's own copy of those.
- A file you put into `dec` that the game does not have is ignored; a file that is missing from `dec` is read from the game as usual.
- The extra commands above (alpha, collider types, polygon skin) work in plain-text scripts of any game.

## Changing the engine from Lua

Nothing here needs a rebuild. Put the code in `data/scripts/extra_scripts.lua` (Classic 1.6.2 loads it by itself), in a plain-text script in the `dec` folder, or type it into the console. The full reference is in [`SCRIPTING.md`](SCRIPTING.md): the `engine` table (`engine.original`, `engine.on`, `engine.shouldCollide`, `engine.strengthChange`, `engine.drawObject`), the `tuning` table (`engine.tuningDefaults` lists every name), `getThemeLayers` / `setThemeLayer` / `getCamera`, and the events `update`, `levelLoaded`, `objectCreated`, `objectRemoved`, `contact`, `drawBackground`, `drawForeground`, `drawGameBefore` and `drawGame`. To try a line quickly: `AB_EXEC="<lua>" AB_EXEC_FRAME=<n>` runs it on frame `n`.

## Debug logging

These variables print what the engine does to the command prompt window, for bug reports and for matching the engine against the original game (set them with `set NAME=1` before starting the engine):

| Variable | What it prints |
|---|---|
| `AB_DRAWDBG=1` | how the backgrounds, layers, sprites and native draws are placed (`NTREF`, `NTL`, `AIM`, `COMPOPART` lines): the first thing to attach to a drawing bug |
| `AB_WOBDBG=1` | every hit wobble that is started and every contact that could start one |
| `AB_TRAJDBG=1`, `AB_AIMDBG=1` | the points of the aiming aid's trajectory (`TRAJ`, `AP` lines) |
| `AB_DMGDBG=1` | every contact that damages or destroys a block |
| `AB_CONTACTDBG=<text>`, `AB_PHYSDBG=1`, `AB_BODYDUMP=n` | contacts whose names contain the text, a general physics log, the positions of every body every n steps |
| `AB_JOINTDBG=<text>` | the joints created for objects whose name contains the text |; also the entry count of every `createJoints` call
| `AB_DUMPLEVEL=<prefix>` | writes the decrypted text of every loaded level to `<prefix>.<level>.lua` (compiled levels stay bytecode; `unluac` reads them) |
| `AB_SLOWFRAMES=1` | every frame that took over 25 ms |

When you report a problem, the last 40 lines of this output plus the name of the level are usually enough to find it.
