# Building and publishing a release

Everything below is typed in the **x86 Native Tools Command Prompt for VS** (Start menu, "Visual Studio"; use the x64 one for a 64-bit build), inside the engine folder. Nothing needs a browser or the GitHub website except the very first sign-in.

Replace `v4.0` with the version you are releasing. Versions are `vMAJOR.MINOR`, `vMAJOR.MINOR.PATCH` or `vMAJOR.MINOR.PATCH.BUILD`; the number must match the `## New in 3.9` heading of `CHANGELOG.md` (the release notes are cut from that section) and the version in the first lines of `README.md`.

## One command

    cd /d C:\path\to\engine
    publish.bat v4.0 "what changed"

`publish.bat` does the six steps below in order and stops at the first that fails: build, tests, personal-name check, commit, push, release. Without the message it only does the first three (a dry run). Everything else in this file explains the steps, so you can do one by hand or fix one that stopped.

## One-time setup

1. **Tools** (once, in any prompt): Git, CMake, Visual Studio (or its Build Tools) with the C++ workload, and the GitHub CLI:

       winget install --id Git.Git -e
       winget install --id Kitware.CMake -e
       winget install --id GitHub.cli -e

   Close and reopen the prompt afterwards so `git`, `cmake` and `gh` are found.

2. **Sign in to GitHub** with the account that owns the repository:

       gh auth login

   Choose GitHub.com, HTTPS, "Login with a web browser" (or paste a token). `gh auth status` shows who is signed in. `gh auth setup-git` makes plain `git push` use the same sign-in.

3. **Who the commits are by.** Commits carry an author name and e-mail, and they are published with the history. Use ones that are not yours, set for this repository only (no `--global`):

       git config user.name "BurnerAccount-dec"
       git config user.email "<id>+BurnerAccount-dec@users.noreply.github.com"

   The noreply address is on GitHub under Settings, Emails ("Keep my email addresses private" shows it). Check with `git config --list --local`. If an old commit already carries a real name, the history has to be rewritten before publishing.

4. **The remote** (only if `git remote -v` prints nothing):

       git remote add origin https://github.com/BurnerAccount-dec/linnut1.6.2-decomp.git
       git branch -M main

## The steps, one by one

1. **Update the notes.** Each change gets an entry in `CHANGELOG.md` under `## New in 3.9`, the user-visible list in `README.md` is brought up to date, and anything a script writer can call or change is in `docs/SCRIPTING.md` (what it does, its arguments, an example). A change that has no entry here is not finished.

2. **Build.**

       cmake -S . -B build -A Win32
       cmake --build build --config Release

   The exe is `build\Release\AngryBirdsEngine.exe`. SDL2 is downloaded on the first configure (internet needed); put `SDL2-2.30.9.tar.gz` next to `make_release.bat` to work offline.

3. **Test.**

       ctest --test-dir build -C Release --output-on-failure

   All tests must pass. Then start the exe in a game folder and try what changed.

4. **Personal names.** The Windows user name (or any personal name) must not be in a published file, in the exe, or in the history. `publish.bat` searches every file git would commit; `make_release.bat` searches the finished exe and stops with `STOP: the exe still contains a personal path` if it finds one. By hand:

       git grep -I -i -n --untracked --exclude-standard -e "%USERNAME%"

5. **Commit and push.**

       git status
       git add -A
       git commit -m "what changed"
       git push origin HEAD

   Add to an earlier message instead of starting a new one when a batch of changes belongs together: `git commit --amend -m "old text, new text"` (only before it is pushed; after that, a new commit).

6. **Build the release zip from a clean copy.** This clones the committed repository to `C:\abbuild`, so only committed files are in it, builds with a static runtime and zips the exe with the licences, README, CHANGELOG, `docs`, `tools` and `extras`:

       make_release.bat v4.0

   The zip is `C:\abbuild\AngryBirdsEngine-v4.0.zip` and the notes `C:\abbuild\release-notes.md`.

7. **Publish it on GitHub.**

       make_release.bat v4.0 publish

   or, by hand, from the folder where the zip is:

       gh release create v4.0 AngryBirdsEngine-v4.0.zip --repo BurnerAccount-dec/linnut1.6.2-decomp --title "v4.0" --notes-file release-notes.md

   `gh release view v4.0 --repo BurnerAccount-dec/linnut1.6.2-decomp --web` opens it.

## When something stops

| Message | What to do |
|---|---|
| `'gh' is not recognized` | Install it (setup step 1) and open a new prompt. |
| `gh auth login` asked again / `HTTP 401` | `gh auth status`, then `gh auth login`. |
| `git push` says `rejected ... fetch first` | Someone (or another computer) pushed: `git pull --rebase origin main`, then push again. |
| `Author identity unknown` | Setup step 3. |
| `release already exists` | The version was published: use a new version number, or `gh release delete v4.0 --yes --repo BurnerAccount-dec/linnut1.6.2-decomp` and publish again. To only replace the zip: `gh release upload v4.0 AngryBirdsEngine-v4.0.zip --clobber --repo BurnerAccount-dec/linnut1.6.2-decomp`. |
| `STOP: the exe still contains a personal path` | Do not publish. The script prints where it is. |
| `FAILED` in `make_release.bat` | Read the lines above it. The usual cause is a file that is not committed (the clean copy is made from commits, not from the folder). |
| CMake cannot find SDL2 | Needs internet on the first configure, or the `SDL2-2.30.9.tar.gz` next to `make_release.bat`. |

## What is in the repository and what is not

Source (`src`, `third_party`, `tests`, `tools`, `extras`, `docs`), `CMakeLists.txt`, `LICENSE`, the two `.bat` files, README and CHANGELOG. Not in it (see `.gitignore`): no game files, no icon, no `build`, `dist` or `stage` folders, no zips, no `settings.lua` / `highscores.lua`.

## Versions

- The engine version is written in the first lines of `README.md`, the newest `## New in` heading of `CHANGELOG.md`, and the release tag. Features carry the version they appeared in in the docs ("since 3.5").
- The notes of a version cover everything since the last version that was published; the 4.0 notes cover what came after 3.9.
