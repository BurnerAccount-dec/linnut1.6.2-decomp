@echo off
setlocal
rem Everything from "I changed something" to "it is on GitHub", in one go. Run it from the x86 (or x64) Native Tools Command Prompt for VS, inside the engine folder:
rem     publish.bat v4.0 "what changed"
rem It builds, runs the tests, checks that no personal name is in the files, commits, pushes, and makes the GitHub release with the zip (make_release.bat v4.0 publish).
rem Leave the message out to only build, test and check:   publish.bat v4.0
rem The routine and what to do when a step stops: docs/RELEASING.md
set "VER=%~1"
if "%VER%"=="" (
  echo Usage: publish.bat v4.0 "what changed"
  exit /b 1
)

echo === 1/6 build
cmake -S . -B build -A Win32 || goto fail
cmake --build build --config Release || goto fail
echo === 2/6 tests
ctest --test-dir build -C Release --output-on-failure || goto fail

echo === 3/6 personal names
rem the Windows user name must not be in anything that gets published (only the files git would commit are searched, the name is not printed)
git grep -I -i -q --untracked --exclude-standard -e "%USERNAME%"
if not errorlevel 1 (
  echo STOP: your Windows user name is written in one of the files. List them with:  git grep -I -i -n --untracked --exclude-standard -e "%%USERNAME%%"
  exit /b 1
)
for /f "delims=" %%E in ('git config user.email') do set "EMAIL=%%E"
if "%EMAIL%"=="" (
  echo STOP: git has no author e-mail for this repository. Set one that is not yours, see docs/RELEASING.md, "One-time setup".
  exit /b 1
)

if "%~2"=="" (
  echo.
  echo Build, tests and checks passed. Give a message to commit, push and release:  publish.bat %VER% "what changed"
  exit /b 0
)

echo === 4/6 commit
git add -A || goto fail
git commit -m %2 || echo (nothing new to commit: continuing with what is there)
echo === 5/6 push
git push origin HEAD || goto fail
echo === 6/6 release
call "%~dp0make_release.bat" %VER% publish || goto fail
echo.
echo ALL DONE: %VER% is on GitHub.
exit /b 0

:fail
echo.
echo FAILED. The lines above say which step failed.
exit /b 1
