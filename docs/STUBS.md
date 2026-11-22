# Unfinished parts

What is stubbed, approximated or untested, by game. Found by searching the engine and the native scripts.

## Engine (all games)
- **Audio loops and the effects manager** (`src/engine/game_platform.cpp`): `LoopActive`, `StartLoop`, `SetLoopVolume` and `StopLoop` do nothing.
- **Game Center, Flurry, ads, in-app purchase, overlays** (`lua_api.cpp`, `script_host.cpp`): answer "nothing"; only the wallet, fake ad, update checks and leaderboard stand-ins are real.
- **`isGameOverlayActive`** is a stub in retail too (always false).
- **Setting `objects.world` entries:** adding or removing whole entries from a script is not implemented (fields of existing entries are).
- **Windows branches** of `isNetworkConnected` / TcpReachable are written but not compiled in the Linux checks.
- **Seasons additions in `lua_api.cpp`:** `loadLevelFile` answers nothing; the Seasons 4.x list marked "not implemented yet" is only partly done.

## Angry Birds Space / Star Wars (`space_natives.lua`)
- Natives stubbed so scripts keep running (`setChannelCountLimit`, `resetTimer`, audio input/output, `setStat`, `captureSprite`, `setPath`, `updateBirdTrajectoryTable`, shop/ad calls): harmless, no real behaviour.
- `renderMaskedImageNative`, `drawSelectedTexturizedObject`: not implemented.
- Star Wars Hoth backgrounds incomplete; `deathstar_1` crashes; Space 1.1.0 `.ipa` background drawing is still off in places.

## Angry Birds Stella (`stella_natives.lua`)
- `native_applySensorForces`: blocked, not implemented.
- Abilities in `extra_scripts.lua` are a port for Classic birds: the aim aids use Classic's own sprites and lines (Stella's art is not in the other games), Dahlia's force scales are tuned by eye, and one ability state `S` is shared, so two birds cannot hold different timers.

## Angry Birds Friends
- Coins and items are not merged with Social accounts.
- 1.0.0 zoom-out backgrounds use the same ground-anchored rule as 3.3.0 (checked by eye, no retail reference).

## Classic 2.x / 3.x apk
- 3.3.0 zoom-out backgrounds are fixed in 4.2 (Friends 1.0.0 uses the same rule; compared by eye only); a slowdown on collisions in the 2.0.0 apk is reported and not reproduced.

## Social
- The leaderboard list page has never been viewed in a test.

## Per-sprite changes (4.2)
- Backgrounds and foregrounds: whole repeats of a layer's sprite only; parts of composite sprites, trails, and the layers of the new theme system (Stella, Space) are not covered.
- The Friends wheel split uses the right 23% of the window as the profile panel; it was not tried with a real mouse wheel here.


Per-sprite control (4.2) does not reach the parts inside composite sprites (a composite is changed as one sprite) or the aiming-aid dots.
