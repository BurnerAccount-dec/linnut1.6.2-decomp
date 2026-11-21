@echo off
title Angry Birds Social server
cd /d "%~dp0"
where node >nul 2>nul || (echo Node.js is not installed. Get the LTS version from https://nodejs.org and run this again. & pause & exit /b 1)
echo Starting the server on port 80 (needs "Run as administrator")...
node server.js 80
echo.
echo The server stopped. If it says EADDRINUSE, another program uses port 80 - close it, or run: node server.js 8080
pause
