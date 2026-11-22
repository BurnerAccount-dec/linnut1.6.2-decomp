Angry Birds Social server (stand-in for the old level-sharing server)
=====================================================================

What it does
  Plays Angry Birds Social on your own computer: login, level editor, saving and publishing levels,
  Play levels, Browse, high scores. Everything is kept in social-data.json next to server.js
  (a copy of the last start is kept as social-data.json.bak).

Set-up (once)
  1. Install Node.js (nodejs.org, the LTS installer).
  2. Add these two lines to C:\Windows\System32\drivers\etc\hosts (open Notepad as administrator):
         127.0.0.1 sl.angry-birds-social.appspot.com
         127.0.0.1 angry-birds-social.appspot.com
         127.0.0.1 1.angry-birds-social.appspot.com
         127.0.0.1 tpm.angry-birds-social.appspot.com
         127.0.0.1 jachren.angry-birds-social.appspot.com
         127.0.0.1 dev.angrybirds.com
     (only the first two are needed to play; the others make the domain buttons at the top of every page work)
  3. Put the game files into the "www" folder next to server.js: the patched
     AngryBirdsSocial_fp11_001.swf, index.html and the external_assets folder.

Every time
  1. Right-click start.bat > Run as administrator (port 80 needs it).
     Without administrator rights run "node server.js 8080" instead.
  2. In a browser with the Flash plugin (Basilisk) open  http://sl.angry-birds-social.appspot.com/
  3. Log in with any e-mail and password. The first login makes the account; the same password is needed later.
  The black window lists every request the game makes. "unknown request" lines are commands the server does not know.

The 63 Poached Eggs levels of Angry Birds are added to the level list the first time the server starts (poached-eggs.json), as levels by "Rovio".

Levels and Angry Birds Classic
  Open  http://sl.angry-birds-social.appspot.com/classic  to
    - download any Social level as a Classic level file (.lua). Drop the file onto the engine window while the level
      editor is open, or copy it into the game's data\levels folder;
    - paste a Classic level file (the text of a .lua) to send it to Social as a published level.
  Social measures in grid cells and Classic in world units, so sizes are matched block by block. Blocks, pigs and birds
  that Classic does not have (the white bird, for example) are left out and listed in the server window.

Playing with friends
  The server lets any number of players log in; every account has its own levels, coins and scores, and all published
  levels and scores are shared. When the server starts, its window prints an address like  http://192.168.1.20/join .
    On the same network (home Wi-Fi / LAN):
      1. Your friend opens that /join address in a browser, downloads join.bat and runs it as administrator once
         (it points sl.angry-birds-social.appspot.com at your computer in their hosts file).
      2. They open  http://sl.angry-birds-social.appspot.com/  in a browser with the Flash plugin and log in with their own e-mail.
      3. If Windows asks whether Node.js may use the network, allow it for private networks.
    Over the internet: forward port 80 of your router to your computer and give your friend your public address instead
    (the /join page then shows it when they open http://<your public address>/join ).
    The engine: set  socialLevels.server = "http://<the address>"  and  socialLevels.player = "<a name>"  in extra_scripts.lua. The
    score of a Social level cleared in the engine is sent to the server, so engine players and Social players compete on the same levels.
  http://sl.angry-birds-social.appspot.com/classic/live  shows who is playing, the best scores and what just happened (it reloads itself).

Avatar Creator
  Put AvatarCreator20.swf (and the files it loads from external_assets) in the www folder and open
  http://sl.angry-birds-social.appspot.com/avatar.html . What the creator saves is kept per account. Its server answers are not
  documented anywhere, so the server logs every avatar request in full ("avatar saved", "avatar purchase" and "unknown request" lines):
  if something does not show up, those lines say what the creator asked for.

Avatar Creator: the dice, saving and the profile picture now work. Saving stores the avatar's look and the 150x150 picture for the logged-in player; every item is unlocked.

Sign-in is shared by every Social page and domain (per computer). The page bar shows your name with "Sign out"; it ends the shared sign-in.

Players page: /classic/players (also "Players" in the page bar) lists every profile with its avatar and every level, open to anybody who can reach the server. E-mail addresses, passwords and tokens are never shown.

Hosting it for others, the limits it keeps and the PUBLIC_HOST setting: see HOSTING.txt.

Coins of the engine
-------------------
The engine (F10) keeps a wallet of coins and shares it with this server through /classic/wallet. The server keeps every wallet in social-data.json, so nothing is lost when it is
stopped, and a wallet that was made before any account existed waits here: as soon as an account with the name the engine uses (socialLevels.player in extras/extra_scripts.lua) is made, the
wallet's coins are added to the account once. After that the account's coins and the engine's coins are the same.

Angry Birds Friends (engine): "Log in with Facebook" asks /classic/account?player=Name whether a Social account with the player's name (socialLevels.player) exists; the coin window (F10) can share the Friends coins with that account, or move them across.
