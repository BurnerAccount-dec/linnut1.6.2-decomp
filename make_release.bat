@echo off
setlocal
rem Builds a release exe from a clean copy of the committed repository in C:\abbuild,
rem so no personal folder name ends up inside the exe, then zips it with the licences.
rem Run it from the x64 Native Tools prompt, inside the engine folder, after committing your changes (git add -A & git commit).

if exist C:\abbuild rmdir /s /q C:\abbuild
git clone "%~dp0." C:\abbuild || goto fail
cd /d C:\abbuild || goto fail

cmake -S . -B build -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded || goto fail
cmake --build build --config Release --target angrybirds || goto fail
if not exist build\Release\AngryBirdsEngine.exe goto fail

findstr /i /m /c:"EpikBirb" build\Release\AngryBirdsEngine.exe && goto personal
findstr /i /m /c:"Hugofritz" build\Release\AngryBirdsEngine.exe && goto personal
findstr /i /m /c:"C:\Users" build\Release\AngryBirdsEngine.exe && goto personal

mkdir dist
copy build\Release\AngryBirdsEngine.exe dist\AngryBirdsEngine.exe || goto fail
copy LICENSE dist\LICENSE.txt || goto fail
copy third_party\lua\LICENSE dist\LICENSE-Lua.txt || goto fail
copy third_party\box2d\LICENSE dist\LICENSE-Box2D.txt || goto fail
copy third_party\minimp3\LICENSE dist\LICENSE-minimp3.txt || goto fail
powershell -NoProfile -Command "Compress-Archive -Force -Path dist\* -DestinationPath AngryBirdsEngine-v1.6.zip" || goto fail

echo.
echo DONE. The exe is clean. Upload this file to the GitHub release:
echo   C:\abbuild\AngryBirdsEngine-v1.6.zip
exit /b 0

:personal
echo.
echo STOP: the exe still contains a personal path. Do not publish it. Tell Claude.
exit /b 1

:fail
echo.
echo FAILED. Copy the last lines above and send them to Claude.
exit /b 1
