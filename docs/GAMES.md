# Games and natives

Natives are shared where behaviour is the same and kept per line where it differs. The line is chosen from marker scripts in the game data.

| Game | Line | Natives |
|---|---|---|
| Space, Star Wars, Stella | Space (`blocks_planets.lua`, `blocks_tatooine.lua`, `PolaroidMode.lua`) | space_natives.lua / stella_natives.lua |
| Seasons 4.1.0, Friends 1.0.0, 3.3.0 apk, unknown games with common scripts | Seasons 4.x | shared Seasons 4 natives + generic stubs |
| Friends 2.0.0 | `TutorialMode.lua` | friends2_natives.lua |
| Rio | `LoadRioTables` | Rio tables |
| Hatchery | | hatchery_natives.lua |
| Classic 1.x/2.x | none | built-in |

Every game also gets the generic fallback stubs (see STUBS.md), so a missing native is a no-op instead of an error.
