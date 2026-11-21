// v3.3: the engine's exe can take the place of another exe (usually the game's own AngryBirds.exe), and give it back.
#pragma once
#include <string>

namespace ab {

// Replaces `target` (an .exe) with a copy of the running engine exe, after saving the original next to it as "<target>.original". If `target` already is the engine and
// the backup is there, it puts the original back instead (the same call undoes itself). `confirm` false skips the question (--yes). Prints what it did and shows a message box.
// Returns 0 when done, 1 when nothing was changed on purpose (the user said no, nothing to do), 2 on an error.
// 3.9: the replaced exe gets the icons and the version information of the original (so Explorer, the taskbar and the title bar show the retail icon), and the question
// whether the command prompt and the developers' tools stay on is asked (debugTools: 1 on, 0 off, -1 ask; with confirm false -1 changes nothing). The answer is written to
// config.lua beside the exe as `debugTools = true / false` and `console = true / false`.
int ReplaceOrRestoreExe(const std::string& target, bool confirm, int debugTools = -1);

// "--restore-exe <target>": puts the saved original back and refuses to do anything else.
int RestoreExe(const std::string& target, bool confirm);

// 3.9: an .apk or .ipa dropped on the engine: asks whether to play it (0), replace it with the engine / put the original back (1), or do nothing (2).
// ReplaceOrRestoreExe takes such a package the same way as an .exe (saved as "<package>.original", no icon or tools question).
int AskPackageAction(const std::string& package);

// The full path of the running exe ("" when it cannot be found).
std::string SelfExePath();

}  // namespace ab
