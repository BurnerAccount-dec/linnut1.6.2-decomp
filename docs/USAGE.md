# Using the engine: running, options, debugging

How to start the engine, the command line, the console, the environment variables for debugging, the editors and the plain-text script folder. For building see the README, for script writers [`docs/SCRIPTING.md`](SCRIPTING.md).

## Run

Start it from inside the game's folder, the one that holds `config.lua`, so it reads that game's settings (the folder name of the data comes from `config.lua`):

    cd /d "C:\Program Files (x86)\Rovio\Angry Birds"
    C:\path\to\AngryBirdsEngine.exe

It works the same for Seasons and for 2.0.0, just from their own folders. You can also name everything explicitly:

    AngryBirdsEngine.exe --scripts "<game>\data\scripts" --data "<game>\data" [--config <file>] [--userdata <folder>]

You can also put the exe next to the game's `data` folder, where the original exe sits. Do not name it `angrybirds.exe`: Windows treats that as the original's name and would overwrite it.

### Opening an Android .apk or iPhone .ipa

Drag an `.apk` (Android) or `.ipa` (iPhone) file onto `AngryBirdsEngine.exe`, choose the exe in "Open with", or run `AngryBirdsEngine.exe C:\path\AngryBirds_1.3.5.apk`. The first time, the engine unpacks the game's data (an .apk's `assets/data`, an .ipa's `Payload/<name>.app/data_iphone`) into `%LOCALAPPDATA%\AngryBirdsEngine\apk\<file name>\data` (a few seconds; the next start reuses it, delete the folder to unpack again) and starts the game from there. The `config.lua` it writes next to that data (window size, `deviceModel = "android"` or `"iphone"`, `isLiteVersion` for the Lite iPhone game, folder names, `product` for the save folder) can be edited. Only the game's data is used; the native code is not run. iPhone textures in PVRTC format are decoded when loaded. Checked with Angry Birds 1.3.5 and 2.1.0 (.apk) and Angry Birds Free 1.0.0 (.ipa): menus, level select and a level.
Drop `.lua` files onto the exe together with it (select the `.apk`/`.ipa` and the scripts, then drag them all onto the exe): each script is loaded once the game is up. Try `extras/extra_scripts.lua` with 1.3.5 or the Angry Birds Free `.ipa`.

### Angry Birds Friends 2.0.0: the Weekly Tournament's levels

"Play Tournament" logs in as a guest on a stand-in server inside the engine. The `.apk` has no tournament levels (the server sent them). If you have opened Friends 1.0.0's `.ipa` with this engine, or that `.ipa` is in the same folder as the `.apk`, its 30 tournament levels are copied to `data\levels\friends_1.0.0` and used. To play other levels instead, put level files in `%LOCALAPPDATA%\AngryBirdsEngine\apk\Angry Birds Friends 2.0.0\data\levels\tournament` (make the `tournament` folder); while that folder has levels, they replace Friends 1.0.0's. Each file is a level in the game's format named `Level<code>.lua`; the 30 `Level2000-*.lua` files from Friends 1.0.0's `levels\pack57` folder work as they are. With up to six files, all of them are the tournament; with more, six are played each week, in turn, the week ending on Monday at midnight. Star scores: a `starLimits.lua` in the same folder (`Level2000-1 = {silverScore = 96000, goldScore = 110000}`) if there is one, else the level file's own scores (Friends 1.0.0's levels have them), else 40000 / 60000. Coins, bought power-ups and slingshots, the week's scores, trophies and the daily wheel are kept in `settings.lua`.

### Replacing the game's own exe with the engine (3.3; icons, tools question and `.original` drop 3.9)

Instead of starting the engine from a shortcut or a copy next to the game, the engine can take the place of the game's exe, so the original `AngryBirds.exe` (its shortcuts, its Start menu entry, its Steam or desktop icon) starts the engine.

* **Drag the game's `.exe` onto the engine exe.** A question appears ("Replace this exe with the engine?"); on Yes the original is saved as `AngryBirds.exe.original` in the same folder and the engine is copied over `AngryBirds.exe`. The engine starts from the folder of the exe it was started as, the way it always looks for `data` and `scripts` beside it, so nothing else is needed. This works for any `.exe`, not only the game's (a game from the Seasons, Space or Rio folders works the same way).

**.apk and .ipa:** dragging a mobile package onto the engine asks "Play it / Replace with the engine / Cancel" (`--yes` skips the question and plays it; `--replace-exe <package>` replaces it directly). On replace the package takes the engine's contents under its own name, the original is saved as `<name>.apk.original` (or `.ipa.original`), and dropping the package again puts it back. There is no icon or tools question for packages, and the result runs only where a Windows program runs.
* **The replaced exe looks like the original (3.9).** The engine copies the icons and the version information of the original exe into its copy, so Explorer, the taskbar and the title bar show the retail icon (the small one left of the window title too). If the original has no icon, there is none to copy.
* **The question about the tools (3.9).** After the Yes, the engine asks whether to keep the command prompt and the developers' tools (debug keys, Ctrl + minus / plus and Alt speed, the console) on or turn them off, so the game plays like the retail one. The answer is written to `config.lua` beside the exe as `debugTools = true / false` (and `console` with it); edit those lines to change it later. `--debug-tools on|off` answers it on the command line; with `--yes` and no `--debug-tools` the file is left alone.
* **Drag the saved original (`AngryBirds.exe.original`) onto the engine (3.9)** to put it back; it is the same as dragging the replaced exe.
* **Drag the replaced exe onto the engine again to undo it.** The engine notices that the file already is the engine and that a saved original is beside it, and offers to put the original back (the `.original` file is then removed). If the file is the engine but no `.original` is there, nothing is changed and you are told so.
* **Command line:** `AngryBirdsEngine.exe --replace-exe "C:\Games\AngryBirds\AngryBirds.exe"` does the same as the drop (and the undo when it is already replaced), `--restore-exe "..."` only puts the original back, and `--yes` leaves out the questions (for scripts). The exit code is 0 when it was done, 1 when nothing was to be done or you said no, 2 on an error.
* **Protected folders (3.9).** In `C:\Program Files (x86)` Windows only lets an administrator change files. The engine then shows the usual Windows "Run as administrator" question and does the replace or the restore in that run; answer Yes. (There is no way around that question, and the engine does not try: it is Windows' own protection.)
* **Safe by design:** the original is copied first and the engine is written to a temporary file beside the target and moved over it, so if the game is still running (Windows will not let its exe be overwritten) or the disk is full, nothing is changed and you get a message. The exe being dragged cannot be the engine itself. A game that updates itself will put its own exe back; drop it again.

### Options

- `--scripts <folder>`, `--data <folder>`: where the scripts and data are.
- `console = false` in `config.lua` (3.7.1): no command prompt window opens beside the game, so only the game window shows. Since 3.9 the exe is a window program, so the prompt no longer flashes up for a few frames first. The printed lines and the typed console commands are gone with it; `--prompt` keeps the window. The default is `true`. Started from a command prompt, the exe prints into that prompt and the prompt does not wait for it to end.
- `debugTools = false` in `config.lua` (3.9): the engine runs without the developers' tools: no command prompt, no debug keys or editor button (the same as `AB_NODEBUGKEYS=1`), no Ctrl + minus / plus and Alt speed keys. The default is `true`. Dropping an exe on the engine asks for it (see above).
- `floatingScoreFont = "FONT_INGAME_SCORE"` in `config.lua` (3.9): the font of the floating scores, the numbers that rise from a destroyed pig or block. The 1.0.0 and 1.6.x PC scripts ask for `FONT_INGAME_SCORE`, which has a brown outline, while the other releases use `FONT_SCORE` with the black outline; the engine uses `FONT_SCORE` unless this line names another font. `"FONT_INGAME_SCORE"` brings the brown one back.
- `--config <file>`: window settings (title, size, fullscreen, cursor); by default the `config.lua` above `--data`.
- `--size <width> <height>`: window size for `--screenshot` test renders.
- `--userdata <folder>`: keep saves in this folder instead (`.` is the working folder). The default, `original`, uses `%APPDATA%\<publisher>\<product>` (from `config.lua`) and shares progress with the original game. The first overwrite of a file keeps a copy as `<file>.original-backup`.

### Controls added by the engine

- Alt+Enter: window / full screen.
- Hold Alt: the game runs at twice the speed.
- Hold Shift while turning the mouse wheel: the zoom moves in much smaller steps.
- Ctrl+minus / Ctrl+plus: slow down / speed up in 0.25x steps (0.25x to 8x) until the game closes.
- F9: the language list (the languages the game's text files have; Up / Down and Return, or a click). F10: the coin window (the wallet, "Watch an ad" for 10 coins, "Sync now" with the Social stand-in server). Esc closes both.
- F7 (in the games that list achievements): the achievement page. A drag (it carries on after a flick), the wheel or the arrow / Page Up / Page Down / Home / End keys scroll it; Esc, F7, Backspace or the back arrow close it; T or the "Test pop-up" button shows the pop-up of a made-up achievement (nothing is saved).

Set the environment variable `AB_SLOWFRAMES=1` to print a line for every frame that took over 25 ms (a debugging aid for stutter).

### Debug keys and the level editor

The developers' tools in the scripts are switched on in every game (set `AB_NODEBUGKEYS=1` or `debugTools = false` in `config.lua` to run without them). `releaseBuild` itself stays on, because turning it off unlocks every level and the whole game; only the code that reads the debug keys is told it is off.

- 8: draw the hit boxes (sleeping objects in another colour; since 3.6 the engine draws them itself in the games whose scripts have no such drawing, 1.0.0 among them), 9: hide / show the background, 0 or F: frames per second.
- C (or K in the older versions): clear the level. G (Rio): fail the level. Space (Rio): single step. Shift+Left / Shift+Right: previous / next level (where the scripts have it, for example 2.0.0).
- Main menu, "Editor" button: pick a level and edit it. Ctrl+S saves it over the level file (an encrypted original becomes a plain Lua file, which the game reads just the same; a copy for playtesting goes to `temp/` next to it). Shift+P plays the level you are editing, and returns to the editor when you leave it. Tab switches the physics on and off, Shift+C / Shift+B store the castle / bird camera, Delete removes the selection. Right button drag selects, left button drag moves the selection; the Rio block palette is on the keys of `groups.lua` (Q/W wood, A/S glass, 1/2 stone and so on). The level files are written into the game's own `data/levels` folder.

### Editor keys

Two editors exist. The **classic editor** is Rovio's own (the 1.6.2 editor, which the engine also brings to games whose scripts have none: 1.6.3.1, Seasons 2.x, and the games that use it as they ship it). Angry Birds Space and Star Wars have the engine's small **Space editor**. A game whose scripts ship their own editor (2.0.0, Seasons 1.0.0 / 2.2.0, Rio) uses it with the keys the game defines; the keys below are the ones the classic editor reads, which those editors are descended from. Open the editor from the "Editor" button of the main menu (in the games that have it); Space: F2 in a level.

**Classic editor (1.6.x, Seasons 2.x, Rio, 2.0.0 and the others that use it)**

| Key | What it does |
|---|---|
| Left button | Place the block chosen on the palette; click an object to select it; drag with Space held to move the view |
| Left button drag | Move the selection; with Ctrl held, add to the selection |
| Right button / drag | Clear what is being placed or copied; drag a rectangle to select |
| Mouse wheel | Zoom; with Shift held in steps of 5% |
| Middle button | Reset the zoom and the view; with Shift only the zoom |
| The group keys (`keyUp` / `keyDown` of each group of `blocks.lua`, for example Q / W wood, A / S glass, 1 / 2 stone) | Choose the block to place from the group, next / previous |
| 1 .. 9 (a bird selected) | Set the bird's order (`startNumber`) |
| Shift + the theme key of `blockTable.themes.settings.keyCode` | Next theme |
| Arrow keys (selection) | Move the selection; Ctrl held: fine steps |
| Shift + Left / Right / Up / Down | Mirror the selection across its own middle |
| Shift + R | Turn the copied objects by 45 degrees |
| R | Rotate the selected objects |
| Ctrl + C, then click | Copy the selection and place the copy |
| Delete | Remove the selection (and its joints) |
| F12 (Angry Birds 1.6.2, in a menu or in a level) | Social levels: a list of the levels of a stand-in Angry Birds Social server (`socialLevels.server` in `extras/extra_scripts.lua`, `http://localhost` by default); Up / Down choose, Left / Right turn the page, Return or a click plays the level, Escape closes; the list shows each level's best score, and the score of a cleared level is sent to the server (`socialLevels.player` is the name, `socialLevels.sendScores = false` stops it) |
| F1 (in a level, nothing selected in the editor) | Level spawner: a picture of the level, Left / Right pick another level, Up / Down another folder, Return puts its blocks, pigs, joints and birds (they join the line-up) at the cursor; a click on the picture lets you move it about as a half-transparent copy first (Return puts it down), a level file dropped on the window does the same (dropped while no level is running, in the menus, it is played like a level picked from the menu) |
| J, or F1 .. F5 (two objects selected; Shift + J in 1.6.x, where J is a block key) | Open the joint window (F1 distance, F2 weld, F3 revolute, F4 prismatic, F5 "destroy attached", and in the 1.6.x window F6 pulley, F7 gear, F8 mouse, F9 line / wheel, F10 friction, F11 rope; the type button in the window cycles through all eleven); 1.6.x games without one get the window of Seasons 2.3.0 |
| Tab | Physics on / off (the level is restored when it is switched off) |
| Shift + P | Play the level, Escape (also on the clear / failed page) comes back to the editor |
| Ctrl + S | Save the level |
| Shift + C / Shift + B | Store the castle / the bird camera |
| Ctrl + Shift + C / End, Ctrl + Shift + B / Home | Go to the stored castle / bird camera; End alone: the castle camera |
| Ctrl + Shift + Space | Show the debug cameras (needs the game's `g_camerasList`; ignored in games without it) |
| **Shift + G / Ctrl + G / Shift + Ctrl + G** | **New in 3.1:** world gravity +1 / -1 / back to 20; saved in the level as `worldGravity` |
| Escape | Close the joint window; else leave the editor |

**Space editor (Angry Birds Space and Star Wars, F2 in a level)**

| Key | What it does |
|---|---|
| F2 | Turn the editor on or off (the physics stops while it is on) |
| Arrow keys / wheel | Pan / zoom |
| Shift + wheel, PageUp / PageDown | Choose the block on the palette |
| Left button (drag) | Select an object / move it |
| N | Place the palette block at the cursor |
| D | Copy the selected object |
| Q / E | Turn the selection (Shift: in small steps) |
| Delete / Backspace | Remove the selection |
| Ctrl + S | Save the level |

Space levels have no world gravity of their own (the planets pull), so the gravity keys do not exist there. Angry Birds Seasons 2.4.1 (iPad / iPhone) uses its own editor, which has Shift + G (+1) and Ctrl + G (-1) for the gravity as well.

Everything else is the original game's own keyboard and mouse handling (for example R restarts a level in the versions where the scripts support it).

## Command line and environment variables

    AngryBirdsEngine.exe [options]

| Option | What it does |
|---|---|
| `--scripts <folder>`, `--data <folder>` | where the game's scripts and data are |
| `--config <file>` | window settings (title, size, fullscreen, cursor) |
| `--userdata <folder>` | keep saves here (`.` = working folder); default `original` shares progress with the original game |
| `--retina` | an `.ipa` with a 960x640 / 2048x1536 picture set uses it (off by default: the smaller set is used) |
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
| `scale <object> <sx> [sy]`, `speedcap <object> <speed>`, `blend <object> <mode>`, `shader <object> <name>` | the same as `setScale`, `setSpeedCap`, `setBlendMode` and `setShader` for one object (`blend`: normal, add, multiply, screen; `shader`: none, grayscale, sepia, invert, chromatic, glow, or the name of your own shader) |
| `shader list`, `shader templates`, `shader new <name> [template]`, `shader apply <target> <name>`, `shader param <name> <slot> x [y z w]`, `shader check <name>`, `shader reload [name]`, `shader export` | the shader creator (3.3): writes a shader file from a template, applies it to a target, sets its parameters, shows its compile errors; see `docs/SCRIPTING.md`, "Shaders" |
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
| `AB_TRACK=<object>` or `AB_TRACK=all` (3.9) | the same as the console command `track`: prints the object's position and angle twice a second, or with `all` every object that moved or turned (about once a second); with `AB_LOGFILE` this is a physics log without a prompt |
| `AB_LOGFILE=<file>` (3.9) | everything the engine prints (including `AB_DMGDBG` lines) goes to this file instead of a command prompt; read it afterwards or with `type` |
| `AB_NODEBUGKEYS=1` | run without the developers' keys and the editor button (`debugTools = false` in `config.lua` does this and more) |
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
- **1.6.3.1, 2.0.0, Seasons, Year of the Dragon and Rio** ship compiled Lua 5.1 bytecode. Since 2.2 `--decrypt` decompiles it with unluac (needs Java, see "New in 2.2" in `CHANGELOG.md`); files that cannot be decompiled are listed and not written, so `--use-dec` keeps using the game's own copy of those. Since 3.3 a script that a decompiler cannot finish (unluac 1.2.2 loops for ever on some, e.g. Seasons 4.1.0's menu_flow.lua) is given up after 90 seconds (`AB_DEC_TIMEOUT`, `0` = no limit) and the next decompiler is tried.
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


## Episodes file (Angry Birds 1.6.2 and 1.6.3.1)

Before 2.0.0 the episodes of the menu are written out in `gamelogic.lua`; from 2.0.0 on they are one table in `episodes.lua`. The engine's `extras/extra_scripts.lua` (version 19 or newer, which Classic 1.6.2 loads by itself) reads an `episodes.lua` from the same folder as `gamelogic.lua` and builds the old menu from it. Copy `extras/extra_scripts.lua` and `extras/episodes.lua` into the game's `data/scripts` folder, then edit `episodes.lua`:

- The menu has six episode pages: episode 1 holds worlds 1 to 3, episode 2 worlds 4 and 5, episodes 3 to 5 three worlds each, episode 6 worlds 15 to 17.
- `episodes[n].worlds` lists the worlds of episode n, in order: `{ folder = "pack1", levels = { "Level1", "Level57" } }`. `folder` is the folder under `data/levels/` the level files are read from (a folder of your own works), `levels` the file names without `.lua`. A world may hold fewer levels than its page: the other places stay empty.
- `episodes[n].tile` is the picture on the episode's tile (1 to 6, the stock episodes' pictures, names included).
- `episodes.order` is the order of the tiles on the episode screen, as episode numbers: `{ 2, 1, 3, 4, 5, 6 }` shows episode 2 first. Its score, stars and lock move with it.
- Leave out what should stay as it is. The names printed on the tiles are part of their pictures.

### Seasons 2.3.0 and before

For an `.apk` or `.ipa`, put `episodes.lua` next to the `extra_scripts.lua` that is dragged onto the engine with the game (4.1); `style = "popup"` does nothing there. `style = "popup"` draws the episode screen with the pop-up boxes of the mobile versions (names as text, four episodes and the Golden Eggs) instead of picture tiles; the boxes grow under the cursor. `episodes[n] = { name = "2. My Pack" }` sets the title of a box. `tile` and `order` only apply to picture tiles.

Seasons keeps its worlds as packs (`pack1` to `pack15`, listed in the comments of `extras/episodes.lua`) and builds the season and episode pages from them. The engine reads `episodes.lua` from the folder of `gamelogic.lua` by itself (no extra script needed) and `episodes.packs = { pack2 = { "LevelX_39", "LevelV_3" } }` makes a pack hold exactly those levels, in that order. The level files are read from that pack's folder in `data/levels/`. Only the pack lists are changed in Seasons; the order and pictures of the season and episode buttons are the game's own.
