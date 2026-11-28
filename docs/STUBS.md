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
- `native_applySensorForces` and `native_setThemeFgLayerOffsetY` are matched against the binary (4.4); Stella registers them but never calls them.
- Every Lua name Stella registers exists (4.4). Those its scripts never call are matched against the binary: `getObjectVertices`, `decomposePolygon`, `objectAndTrackOverlap`, `drawPolygon`, `native_shareScreenShot`, `native_getOSName`, `getDirectoryFileList`, `printWithTag`, `destroyUniqueShaders` and `onLoadLuaFileFail`. Two differences remain:
  - `native_shareScreenShot` saves the picture to the save folder; there is no share sheet on a computer.
  - `destroyUniqueShaders` has nothing to remove, because the engine draws without shaders.
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

Stella abilities: each activation makes a fresh state table, and only one ability runs at a time (a second bird's tap is ignored until the running one ends), so timers are never shared between two abilities at once.

## Angry Birds 4.0 (PC)
- Every Lua name the exe registers exists (4.4). The differences that remain:
  - `setGravityEnabled` stores its flag and does nothing else, exactly as the exe does.
  - A background mask block is drawn in the order of the other blocks: the game draws all masks as one batch just before the other textured blocks.
  - `saveParticleFile` and `captureScreen` write to the save folder.
  - The theme's rectangle layers use the screen fractions from `blocks.lua` and the layer colour the scripts set.
- (4.5) The power-ups are switched on (`g_powerups_enabled`), with the five of each the game gives at the start; the PC build has no shop, so `iap` is a stand-in that does nothing. The debug keys of `debugUtils.lua` work (F frame rate, shift+D debug console, shift+X / shift+Z power-up rewards, ctrl+1..0 language, ctrl+F5 layout dump).

## Behaviour that is accurate to the retail games (not bugs)
- **Stella's feathers in Seasons (PC) turn at odd angles.** The Seasons particle system uses the emitter's angle range (`angleMinEmitter` .. `angleMaxEmitter`, degrees) twice: once for the direction a particle flies off in, and once as the sprite's starting rotation (`p.angle`, the Seasons branch of `addParticles`, matched against the Seasons exe). Stella's feather particles are made with a wide emitter range so they scatter, so every feather also starts turned by that same random angle and keeps spinning by `minAngleVel` .. `maxAngleVel` from there. The later games (Rio, 2.0.0 and on) read a separate `minAngle` / `maxAngle` for the rotation; Seasons has no such field, so the retail game draws them the same way. A particle file can be given its own rotation by setting the emitter range to the angle the sprite should have and moving the scatter into `minVelX` .. `maxVelY`.
