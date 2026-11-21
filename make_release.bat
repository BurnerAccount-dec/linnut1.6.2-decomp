@echo off
setlocal
rem Usage:  make_release.bat v3.9            builds the clean exe and zips it as AngryBirdsEngine-v3.8.zip
rem         make_release.bat v3.9 publish    the same, then makes the GitHub release v3.9 with the zip (needs the GitHub CLI, `gh auth login` once; the changes must be pushed first)
rem See docs/RELEASING.md for the whole routine, from a change to a published release.
set "VER=%~1"
if "%VER%"=="" (
  echo Usage: make_release.bat v3.9 [publish]
  exit /b 1
)
set "ZIP=AngryBirdsEngine-%VER%.zip"
rem Builds a release exe from a clean copy of the committed repository in C:\abbuild,
rem so no personal folder name ends up inside the exe, then zips it with the licences.
rem The zip holds the exe, the licences, the README, the CHANGELOG, the docs folder, tools (unluac) and extras. The exe opens PC games from their folder and Android .apk / iPhone .ipa files dropped onto it.
rem Run it from the x64 Native Tools prompt, inside the engine folder, after committing your changes (git add -A & git commit).

if exist C:\abbuild rmdir /s /q C:\abbuild
git clone "%~dp0." C:\abbuild || goto fail
cd /d C:\abbuild || goto fail
rem a build folder that was committed by mistake would carry the cache of another folder: start from nothing
if exist build rmdir /s /q build

rem SDL2 is normally downloaded from GitHub. If SDL2-2.30.9.tar.gz lies next to this bat, it is unpacked to C:\abbuild_sdl2 and used instead (no personal path in it).
if exist "%~dp0SDL2-2.30.9.tar.gz" if not exist C:\abbuild_sdl2\CMakeLists.txt (
  mkdir C:\abbuild_sdl2
  tar -xf "%~dp0SDL2-2.30.9.tar.gz" -C C:\abbuild_sdl2 --strip-components=1
)
set SDLOPT=
if exist C:\abbuild_sdl2\CMakeLists.txt set SDLOPT=-DFETCHCONTENT_SOURCE_DIR_SDL2=C:/abbuild_sdl2
cmake -S . -B build -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded %SDLOPT% || goto fail
cmake --build build --config Release --target angrybirds || goto fail
if not exist build\Release\AngryBirdsEngine.exe goto fail

rem the name of whoever runs this, taken from Windows, so the name is not written into this file
findstr /i /m /c:"%USERNAME%" build\Release\AngryBirdsEngine.exe >nul && set "HIT=%USERNAME%" && goto personal
findstr /i /m /c:"C:\Users" build\Release\AngryBirdsEngine.exe >nul && set "HIT=C:\Users" && goto personal

mkdir dist
copy build\Release\AngryBirdsEngine.exe dist\AngryBirdsEngine.exe || goto fail
copy LICENSE dist\LICENSE.txt || goto fail
copy third_party\lua\LICENSE dist\LICENSE-Lua.txt || goto fail
copy third_party\box2d\LICENSE dist\LICENSE-Box2D.txt || goto fail
copy third_party\minimp3\LICENSE dist\LICENSE-minimp3.txt || goto fail
copy third_party\h264bsd\LICENSE.md dist\LICENSE-h264bsd.txt || goto fail
copy README.md dist\README.md || goto fail
copy CHANGELOG.md dist\CHANGELOG.md || goto fail
xcopy /e /i /y docs dist\docs || goto fail
xcopy /e /i /y tools dist\tools || goto fail
xcopy /e /i /y extras dist\extras || goto fail
powershell -NoProfile -Command "Compress-Archive -Force -Path dist\* -DestinationPath %ZIP%" || goto fail

echo.
echo DONE. The exe is clean: C:\abbuild\%ZIP%
rem the release notes: the "New in" section of this version in CHANGELOG.md
powershell -NoProfile -Command "$t=Get-Content -Raw CHANGELOG.md; $v='%VER%'.TrimStart('v'); $m=[regex]::Match($t,'(?s)## New in '+[regex]::Escape($v)+'\r?\n(.*?)(\r?\n## New in |\z)'); Set-Content -Encoding UTF8 release-notes.md $m.Groups[1].Value.Trim()" || goto fail
if /i "%~2"=="publish" goto publish
echo To upload it, run:
echo   gh release create %VER% C:\abbuild\%ZIP% --repo BurnerAccount-dec/linnut1.6.2-decomp --title "%VER%" --notes-file C:\abbuild\release-notes.md
exit /b 0

:publish
gh release create %VER% "%ZIP%" --repo BurnerAccount-dec/linnut1.6.2-decomp --title "%VER%" --notes-file release-notes.md || goto fail
echo Published: https://github.com/BurnerAccount-dec/linnut1.6.2-decomp/releases/tag/%VER%
exit /b 0

:personal
echo.
echo STOP: the exe still contains a personal path. Do not publish it.
echo The exe contains the text: %HIT%
echo Where it is (the 160 characters around it):
powershell -NoProfile -Command "$t=[IO.File]::ReadAllText('build\Release\AngryBirdsEngine.exe',[Text.Encoding]::GetEncoding(28591)); $i=$t.IndexOf($env:HIT,[StringComparison]::OrdinalIgnoreCase); if($i -ge 0){$a=[Math]::Max(0,$i-80); $t.Substring($a,[Math]::Min(160,$t.Length-$a)) -replace '[^\x20-\x7e]','.'}"
exit /b 1

:fail
echo.
echo FAILED. The lines above say which step failed.
exit /b 1
