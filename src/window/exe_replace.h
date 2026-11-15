// v3.3: the engine's exe can take the place of another exe (usually the game's own AngryBirds.exe), and give it back.
#pragma once
#include <string>

namespace ab {

// Replaces `target` (an .exe) with a copy of the running engine exe, after saving the original next to it as "<target>.original". If `target` already is the engine and
// the backup is there, it puts the original back instead (the same call undoes itself). `confirm` false skips the question (--yes). Prints what it did and shows a message box.
// Returns 0 when done, 1 when nothing was changed on purpose (the user said no, nothing to do), 2 on an error.
int ReplaceOrRestoreExe(const std::string& target, bool confirm);

// "--restore-exe <target>": puts the saved original back and refuses to do anything else.
int RestoreExe(const std::string& target, bool confirm);

// The full path of the running exe ("" when it cannot be found).
std::string SelfExePath();

}  // namespace ab
