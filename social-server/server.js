// Angry Birds Social: a small stand-in for the old level-sharing server.
// Speaks the protocol the client (AngryBirdsServer.as / HTTPConnection.as / ResponseHandler.as) uses:
//   POST  http://<host>/services/<command>   form fields in the body
//   reply  C=<command>&json=<url-encoded JSON>        (an "E" field marks an error)
// No packages needed: run with   node server.js [port]
// Everything is kept in social-data.json next to this file.

const http = require("http");
const fs = require("fs");
const path = require("path");
const classic = require("./classic.js");
const crypto = require("crypto");

const WWW = path.join(__dirname, "www");      // the game's files (the SWF and external_assets) go here
const MIME = { ".html": "text/html", ".swf": "application/x-shockwave-flash", ".xml": "text/xml", ".png": "image/png", ".jpg": "image/jpeg",
               ".js": "text/javascript", ".css": "text/css", ".json": "application/json", ".mp3": "audio/mpeg", ".txt": "text/plain" };
const PORT = parseInt(process.argv[2] || process.env.PORT || "80", 10);
const FILE = path.join(__dirname, "social-data.json");

let db = { users: {}, tokens: {}, levels: {}, scores: {}, rates: {}, wallets: {}, nextLevel: 1 };
try { db = Object.assign(db, JSON.parse(fs.readFileSync(FILE, "utf8"))); } catch (e) { /* first start */ }
try { if (fs.existsSync(FILE)) fs.copyFileSync(FILE, FILE + ".bak"); } catch (e) { /* no backup, carry on */ }
setInterval(() => { try { if (fs.existsSync(FILE)) fs.copyFileSync(FILE, FILE + ".bak"); } catch (e) { /* carry on */ } }, 3600000);          // a copy of the data every hour
let saveTimer = null;
function save() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => fs.writeFile(FILE, JSON.stringify(db), () => {}), 200);
}

// ---- for a server that others reach: hashed passwords, limits, sessions that do not mix up two players behind one router
const hashPass = (pw) => { const salt = crypto.randomBytes(8).toString("hex"); return "s1$" + salt + "$" + crypto.scryptSync(pw, salt, 32).toString("hex"); };
const passOk = (stored, pw) => { if (String(stored).startsWith("s1$")) { const [, salt, h] = stored.split("$"); return crypto.timingSafeEqual(Buffer.from(h, "hex"), crypto.scryptSync(pw, salt, 32)); } return stored === pw; };
const tries = {};       // per address: failed logins and new accounts, forgotten after an hour
const tooMany = (ip, kind, max) => { const now = Date.now(), t = (tries[ip + kind] = (tries[ip + kind] || []).filter((x) => now - x < 3600000)); return t.length >= max; };
const countTry = (ip, kind) => { (tries[ip + kind] = tries[ip + kind] || []).push(Date.now()); };
const LIMIT_BODY = 3 * 1024 * 1024, LIMIT_LEVEL = 600 * 1024, LIMIT_IMAGE = 400 * 1024, MAX_LEVELS_PER_USER = 200, MAX_USERS = 2000;
let callerIp = "127.0.0.1";        // the address of the request being answered (set before every command)

// the address friends put in their hosts file: PUBLIC_HOST (set it on a router-forwarded or named server), else the address the request came to
const publicHost = (req) => process.env.PUBLIC_HOST || req.socket.localAddress.replace(/^::ffff:/, "");

const SLOTS_START = 4;          // level slots every new user has
const SLOT_PRICE = -1;          // -1: the client shows no "unlock" plot; a positive number is the coin price


// every page the server hands out gets a slim bar with one button per Social domain, opening the same page on that domain
const DOMAINS = ["sl.angry-birds-social.appspot.com", "angry-birds-social.appspot.com", "1.angry-birds-social.appspot.com", "tpm.angry-birds-social.appspot.com", "jachren.angry-birds-social.appspot.com", "dev.angrybirds.com", "localhost", "127.0.0.1"];
const PAGES = [["Game", "/"], ["Avatar Creator", "/avatar.html"], ["Social \u21c4 Classic", "/classic"], ["Players", "/classic/players"], ["Live", "/classic/live"], ["Join", "/join"], ["V1 game", "/v1/"], ["V1 Avatar Creator", "/v1/avatar.html"]];
const BAR = '<div id="pagebar" style="font:12px sans-serif;padding:4px 8px 0;background:#111;color:#999;text-align:center">Pages: </div>' +
    '<div id="domainbar" style="font:12px sans-serif;padding:4px 8px;background:#111;color:#999;text-align:center">Domains: </div>' +
    '<script>(function(){var b=document.getElementById("pagebar");' + JSON.stringify(PAGES) + '.forEach(function(x){var a=document.createElement("a");a.textContent=x[0];a.href=x[1];var on=location.pathname==x[1]||(x[1]=="/"&&location.pathname=="/index.html");' +
    'a.style.cssText="margin:0 3px;padding:2px 7px;border:1px solid #444;border-radius:3px;text-decoration:none;color:"+(on?"#fff":"#fc8")+(on?";background:#543":"");b.appendChild(a);});var o=new XMLHttpRequest();o.open("GET","/session");o.onload=function(){var r={};try{r=JSON.parse(o.responseText);}catch(e){}if(!r.token)return;var u=document.createElement("span");u.textContent="  "+r.username+" ";u.style.color="#9c9";var g=document.createElement("a");g.textContent="Sign out";g.href="#";g.style.cssText="margin:0 3px;padding:2px 7px;border:1px solid #844;border-radius:3px;text-decoration:none;color:#f99";g.onclick=function(){var q=new XMLHttpRequest();q.open("POST","/signout");q.onload=function(){location.reload();};q.send();return false;};b.appendChild(u);b.appendChild(g);};o.send();})();</script>' +
    '<script>(function(){var b=document.getElementById("domainbar"),h=' + JSON.stringify(DOMAINS) + ',p=location.port?":"+location.port:"";' +
    'h.forEach(function(d){var a=document.createElement("a");a.textContent=d;a.href="http://"+d+p+location.pathname+location.search;' +
    'a.style.cssText="margin:0 3px;padding:2px 7px;border:1px solid #444;border-radius:3px;color:"+(d==location.hostname?"#fff":"#8cf")+";text-decoration:none;"+(d==location.hostname?"background:#345":"");b.appendChild(a);});})();</script>';
function withBar(html) { return /<body[^>]*>/i.test(html) ? html.replace(/<body[^>]*>/i, (m) => m + BAR) : BAR + html; }

const seen = {};                                   // player name -> when the game or the engine last asked something (the "online now" list)
function userOf(p) {
    const name = db.tokens[p.token] || db.tokens[p.auth];          // Social V1 sends its token as "auth" in some calls
    if (name) seen[name] = Date.now();
    return name ? db.users[name] : null;
}
function note(user, text) {                        // the activity list of the live page
    db.activity = db.activity || [];
    db.activity.push({ t: Date.now(), user, text });
    if (db.activity.length > 200) db.activity.splice(0, db.activity.length - 200);
    seen[user] = Date.now();
}
function best(key) {                               // the best score of a level: { username, score } or null
    const board = Object.values(db.scores[key] || {}).sort((a, b) => b.score - a.score);
    return board[0] || null;
}

function levelEntry(l, withShot) {
    const o = { levelKey: l.key, levelName: l.name, user: l.user, username: l.user, plays: l.plays, published: l.published,
                timesPlayed: l.plays, timesLiked: l.likes, timesFavorited: 0, silverScore: l.silver, goldScore: l.gold, cost: l.cost };
    if (withShot && l.screenShot) o.screenShot = l.screenShot;
    return o;
}

function listObject(levels, withShot) {      // the client reads  level-1 .. level-N
    const o = {};
    levels.forEach((l, i) => { o["level-" + (i + 1)] = levelEntry(l, withShot); });
    return o;
}

function unescapeLoose(s) { try { return decodeURIComponent(s); } catch (e) { return unescape(s); } }
const yes = (v) => v === true || v === "true" || v === "1" || v === 1;

// the 63 Poached Eggs levels of Angry Birds, converted from the game's own files (poached-eggs.json), as published levels by "Rovio".
// They get ordinary keys (L1001 ...) like every other level, and a picture for the Browse page.
const thumbnail = require("./thumbnail.js")
const AVATAR_ITEMS = ["RedBow", "OldLadyHat", "BrownHat", "BrownCowboyHat", "WhiteWesternHat", "IndianFeathers", "Zombrero", "CrazyScientistHair", "PirateRedHeadscarf", "PirateCaptainHat", "BlackTopHat", "ScientistRoundGlasses", "PirateEyepatch", "BrownMushtacheGotee", "LightBrownMustache", "TieRedStripes", "RedBraw", "PinkFlowerDress", "WhiteKarateBelt", "BlueKarateBelt", "BlueHeartUnderpants", "RedHeartUnderpants", "AmmoBelt", "Revolver", "PegLeg", "PirateHook"];;
try {
    const seeded = JSON.parse(fs.readFileSync(path.join(__dirname, "poached-eggs.json"), "utf8"));
    let changed = 0;
    for (const k of Object.keys(db.levels)) if (/^PE_\d+_\d+$/.test(k)) { delete db.levels[k]; changed++; }   // keys of the first version
    seeded.forEach((s, i) => {
        const key = "L" + (1001 + i);
        if (!db.levels[key]) { db.levels[key] = { key, user: "Rovio", name: s.name, data: s.xml, published: true, plays: 0, likes: 0, silver: 0, gold: 0, cost: 0 }; changed++; }
        if (!db.levels[key].screenShot) { db.levels[key].screenShot = thumbnail.render(db.levels[key].data) || ""; changed++; }
    });
    if (!(db.nextLevel > 1100)) db.nextLevel = 1100;
    for (const l of Object.values(db.levels)) if (l.data && !l.screenShot) { l.screenShot = thumbnail.render(l.data) || ""; changed++; }   // any level that has no picture yet
    if (changed) { console.log("Poached Eggs / thumbnails updated (" + changed + ")"); save(); }
} catch (e) { console.log("seeding problem: " + e.message); }

const commands = {
    login(p) {
        const auth = String(p.auth || "").trim();
        if (!auth) return { E: "missing user" };
        let name = (auth.split("@")[0] || auth).replace(/[^\w .-]/g, "").slice(0, 24).trim();
        if (!name) return { E: "missing user" };
        const pw = String(p.pass || "");
        if (tooMany(callerIp, "bad", 10)) return { E: "too many tries, wait an hour" };
        let u = db.users[name];
        if (!u) {
            if (callerIp !== "127.0.0.1" && callerIp !== "::1" && tooMany(callerIp, "new", 5)) return { E: "too many new accounts from this address" };
            if (Object.keys(db.users).length >= MAX_USERS) return { E: "this server is full" };
            countTry(callerIp, "new");
            u = db.users[name] = { name, pass: hashPass(pw), coin: 100, experience: 0, goldenEgg: 0, level: 1, slots: SLOTS_START, settings: {}, stats: {} };
        } else if (!passOk(u.pass, pw)) {
            countTry(callerIp, "bad");
            return { E: "wrong password" };
        } else if (!String(u.pass).startsWith("s1$")) u.pass = hashPass(pw);          // an account made before passwords were hashed
        const token = crypto.randomBytes(12).toString("hex");
        db.tokens[token] = name;
        note(name, "logged in");
        save();
        return { token, username: name };
    },
    updateStats(p) { return commands.uploadStats(Object.assign({}, p, { token: p.token || p.auth })); },       // V1 sends its token as "auth"
    getFacebookUserIdentifiers(p) { return {}; },
    getStats(p) { const u = userOf(p); return { stats: u ? u.stats : {} }; },
    uploadStats(p) { const u = userOf(p); if (u) { for (const k in p) if (k !== "token") u.stats[k] = p[k]; save(); } return { ok: 1 }; },
    getUserSettings(p) { const u = userOf(p); return u ? Object.assign({}, u.settings) : {}; },
    setUserSetting(p) { const u = userOf(p); if (u) { u.settings[p.key] = p.value; save(); } return { ok: 1 }; },
    getMetaGameData(p) { const u = userOf(p) || { coin: 0, experience: 0, goldenEgg: 0 }; return { coin: Math.max(u.coin || 0, 99999), experience: u.experience || 0, goldenEgg: u.goldenEgg || 0, level: 99 }; },   // everything unlocked in the stand-in

    // ---- levels
    uploadLevel(p) {
        const u = userOf(p); if (!u) return { E: "not logged in" };
        const base64 = unescapeLoose(String(p.data || ""));          // the client sends escape(Base64(xml))
        if (base64.length > LIMIT_LEVEL) return { E: "level too big" };
        if (!p.levelKey && Object.values(db.levels).filter((x) => x.user === u.name).length >= MAX_LEVELS_PER_USER) return { E: "too many levels" };
        const name = unescapeLoose(String(p.level || "Level"));
        let l = p.levelKey ? db.levels[p.levelKey] : null;
        if (l && l.user !== u.name) l = null;
        if (!l) {
            const key = "L" + (db.nextLevel++);
            l = db.levels[key] = { key, user: u.name, plays: 0, likes: 0 };
        }
        Object.assign(l, { name, data: base64, published: true, silver: +p.silverScore || 0, gold: +p.goldScore || 0, cost: +p.cost || 0 });
        if (p.screenShot) l.screenShot = String(p.screenShot);
        note(u.name, "published \"" + name + "\"");
        save();
        return { levelKey: l.key };
    },
    uploadScreenShot(p) { const l = db.levels[p.levelKey]; if (l) { l.screenShot = String(p.screenShot || ""); save(); } return { ok: 1 }; },
    downloadLevel(p) {
        const l = db.levels[p.levelKey]; if (!l) { console.log("downloadLevel: unknown key " + p.levelKey); return { E: "no such level" }; }
        console.log("downloadLevel " + l.key + " (" + l.name + ")");
        l.plays++;
        { const who = db.tokens[p.token]; if (who) note(who, "plays \"" + l.name + "\""); }
        save();
        return { levelKey: l.key, username: l.user, levelName: l.name, levelData: l.data, timesPlayed: l.plays, timesLiked: l.likes, timesFavorited: 0 };
    },
    getLevelList(p) {
        let filter = {}; try { filter = p.filter ? JSON.parse(p.filter) : {}; } catch (e) { /* none */ }
        let list = Object.values(db.levels);
        if (filter.creator) list = list.filter((l) => l.user === filter.creator);
        if (filter.published !== undefined) list = list.filter((l) => l.published === !!filter.published);
        if (p.sortBy === "played") list.sort((a, b) => b.plays - a.plays);
        const off = filter.offset | 0, lim = filter.limit | 0;
        list = list.slice(off, lim > 0 ? off + lim : undefined);
        return listObject(list, yes(p.includeScreenShot));
    },
    deleteLevel(p) { const u = userOf(p), l = db.levels[p.levelKey]; if (u && l && l.user === u.name) { delete db.levels[p.levelKey]; save(); } return { ok: 1 }; },
    getLevelSlots(p) { const u = userOf(p); return { slots: u ? u.slots : SLOTS_START, unlockPrice: SLOT_PRICE }; },
    unlockLevelSlot(p) { const u = userOf(p); if (u) { u.slots++; save(); } return { slots: u ? u.slots : SLOTS_START }; },
    getLevelStats(p) {
        const l = db.levels[p.levelKey]; if (!l) return { stats: {} };
        const sc = Object.values(db.scores[l.key] || {});
        return { stats: { plays: l.plays, likes: l.likes, players: sc.length, bestScore: sc.reduce((m, s) => Math.max(m, s.score), 0) } };
    },

    // ---- ratings
    submitRateLevel(p) {
        const u = userOf(p), l = db.levels[p.levelKey]; if (!u || !l) return { E: "no such level" };
        const r = (db.rates[l.key] = db.rates[l.key] || {});
        const before = r[u.name]; r[u.name] = p.rate;
        l.likes += (p.rate === "like" ? 1 : 0) - (before === "like" ? 1 : 0);
        save();
        return { rate: p.rate };
    },
    getLevelRates(p) { const u = userOf(p); const r = db.rates[p.levelKey]; return { rate: (u && r && r[u.name]) || "" }; },

    // ---- scores
    submitHighScore(p) {
        const u = userOf(p); if (!u) return { E: "not logged in" };
        const s = (db.scores[p.levelKey] = db.scores[p.levelKey] || {});
        const score = +p.score || 0, best = s[u.name] ? s[u.name].score : 0;
        const first = !s[u.name];
        if (!s[u.name] || score > best) s[u.name] = { username: u.name, score };
        const board = Object.values(s).sort((a, b) => b.score - a.score);
        const position = board.findIndex((e) => e.username === u.name) + 1;
        { const lv = db.levels[p.levelKey]; note(u.name, "scored " + score + " on \"" + (lv ? lv.name : p.levelKey) + "\"" + (position === 1 ? " (best so far)" : "")); }
        const coin = first ? 10 : 0, experience = Math.floor(score / 1000);
        u.coin += coin; u.experience += experience; save();
        return { score: { position, score: s[u.name].score }, rewards: { coin, experience } };
    },
    getUserHighScore(p) { const u = userOf(p); const e = u && db.scores[p.levelKey] && db.scores[p.levelKey][u.name]; return { score: e ? e.score : 0 }; },
    getHighScores(p) {
        const board = Object.values(db.scores[p.levelKey] || {}).sort((a, b) => b.score - a.score).slice(0, +p.amount || 100);
        return { scores: board.map((e, i) => ({ username: e.username, userName: e.username, score: e.score, position: i + 1, image: "" })) };
    },
    getFriendScoresForLevel(p) {
        const names = String(p.friends_list || "").split(",").filter(Boolean);
        const board = Object.values(db.scores[p.level_id] || {}).filter((e) => names.includes(e.username)).sort((a, b) => b.score - a.score);
        return { scores: board.map((e, i) => ({ username: e.username, userName: e.username, score: e.score, position: i + 1, image: "" })) };
    },
    getFacebookUserIdentifiers() { return { list: [] }; },

    // ---- the Avatar Creator (AvatarCreator20.swf). Its answers are not documented anywhere, so these keep what it sends and give it back; every request is
    // written to the window in full, so a field it expects and does not get can be added from the log.
    getItemList(p) {
        const u = userOf(p); const a = (u && u.avatar) || {};
        if (p.type === "ownedAvatarItemList") return { list: AVATAR_ITEMS.map(id => ({ itemId: id, amount: "1", type: "avatar_equip" })) };
        return { list: a.items || [] };
    },
    submitItemList(p) {
        const u = userOf(p); if (!u) return { E: "not logged in" };
        let list = p.list;
        if (typeof list === "string") { try { list = JSON.parse(list); } catch (e) { list = null; } }
        if (list && !Array.isArray(list) && Array.isArray(list.list)) list = list.list;
        u.avatar = u.avatar || {};
        if (Array.isArray(list)) u.avatar.items = list;
        console.log("  avatar saved for " + (u.name||"player") + ":", JSON.stringify(p.list).slice(0, 200));
        save(); return { ok: 1 };
    },
    purchaseItem(p) { const u = userOf(p); if (!u) return { E: "not logged in" }; u.avatar = u.avatar || {}; u.avatar.owned = (u.avatar.owned || []).concat([p.item || p.itemId || p.id]); console.log("  avatar purchase:", JSON.stringify(p).slice(0, 300)); save(); return { ok: 1, coin: u.coin }; },
    purchaseItemList(p) { console.log("  avatar purchase list:", JSON.stringify(p).slice(0, 300)); return { ok: 1 }; },
    uploadProfileImage(p) {
        const u = userOf(p); if (!u) return { E: "not logged in" };
        let b = String(p.profileImageData || p.image || "");
        try { b = decodeURIComponent(b); } catch (e) {}
        b = b.replace(/ /g, "+").replace(/^data:image\/png;base64,/, "");
        if (b.length < 20) return { E: "no image" };
        if (b.length > LIMIT_IMAGE) return { E: "image too big" };
        u.profileImage = b; console.log("  profile picture saved for " + (u.name||"player") + " (" + b.length + " chars)");
        save(); return { ok: 1 };
    },

    // ---- daily features (kept simple: the most played published level is "of the day")
    getLevelOfTheDay(p) {
        const all = Object.values(db.levels);                       // published levels first, most played; otherwise any level, so the game always has one to open
        const l = all.filter((x) => x.published).sort((a, b) => b.plays - a.plays)[0] || all[all.length - 1];
        return l ? commands.downloadLevel({ levelKey: l.key }) : { E: "no level of the day yet" };
    },
    getLevelOfTheDayInfo() { return { timeLeft: msToMidnight(), scores: [], standing: 0, topPercentage: 0, screenShot: "" }; },
    submitLevelOfTheDayScore(p) { return commands.submitHighScore(p); },
    getLevelOfTheDayScore() { return { scores: [] }; },
    getDailySprint() { return { timeLeft: msToMidnight(), scores: [], standing: 0, topPercentage: 0, screenShot: "" }; },
    getWeeklyRace() { return { timeLeft: 7 * 86400000, scores: [] }; },
};

function msToMidnight() { const d = new Date(); const n = new Date(d); n.setHours(24, 0, 0, 0); return n - d; }

function parseForm(text) {
    const o = {};
    for (const part of text.split("&")) {
        if (!part) continue;
        const i = part.indexOf("=");
        const k = decodeURIComponent((i < 0 ? part : part.slice(0, i)).replace(/\+/g, " "));
        o[k] = decodeURIComponent((i < 0 ? "" : part.slice(i + 1)).replace(/\+/g, " "));
    }
    return o;
}

function reply(res, cmd, obj) {
    const err = obj && obj.E;
    const body = err ? "C=" + encodeURIComponent(cmd) + "&E=" + encodeURIComponent(String(err))
                     : "C=" + encodeURIComponent(cmd) + "&json=" + encodeURIComponent(JSON.stringify(obj || {}));
    res.writeHead(200, { "Content-Type": "text/plain", "Access-Control-Allow-Origin": "*", "Content-Length": Buffer.byteLength(body) });
    res.end(body);
}


// ---- Angry Birds Classic bridge: /classic lists the levels, /classic/<key>.lua downloads one as a Classic level file,
// and a Classic level file sent to /classic/import (the form on that page) becomes a published Social level.
function classicRoutes(req, res, url, body) {
    const send = (code, type, text, extra) => { res.writeHead(code, Object.assign({ "Content-Type": type + "; charset=utf-8", "Cache-Control": "no-store" }, extra || {})); res.end(type === "text/html" ? withBar(String(text)) : text); };
    const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
    if (url.pathname === "/classic/wallet") {           // the engine's coins: /classic/wallet?id=<wallet>&player=Name&delta=10&coins=40, answered "ok <tab> coins <tab> account"
        // A wallet belongs to one copy of the engine and exists whether or not there is a Social account: the coins it earns offline are kept here and, once an account
        // with the player's name exists, they join the account's coins (once). Afterwards the account's coins are the wallet's.
        const id = String(url.searchParams.get("id") || ""), player = String(url.searchParams.get("player") || "").replace(/[^\w .-]/g, "").slice(0, 24);
        if (!/^[0-9a-f]{8,32}$/.test(id)) return send(400, "text/plain", "no wallet id");
        if (tooMany(callerIp, "wallet", 240)) return send(429, "text/plain", "too many requests"); countTry(callerIp, "wallet");
        const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, Number.isFinite(v) ? v : 0));
        db.wallets = db.wallets || {};
        const delta = clamp(parseInt(url.searchParams.get("delta"), 10), -1000, 1000);
        let w = db.wallets[id], account = "";
        const name = player || (w && w.player) || "";          // a wallet that joined an account stays with it, even when the engine names nobody
        const u = name ? db.users[name] : null;
        if (!w) w = db.wallets[id] = { coins: clamp(parseInt(url.searchParams.get("coins"), 10), 0, 100000), player: "", created: Date.now() };     // first time: the coins earned so far
        else if (u && w.player === name) u.coin = Math.max(0, (u.coin | 0) + delta);                                                          // linked: the account takes the change
        else w.coins = Math.max(0, (w.coins | 0) + delta);
        if (u) {
            if (w.player !== name) { u.coin = (u.coin | 0) + w.coins; note(name, "joined " + w.coins + " coins from the engine"); w.coins = 0; w.player = name; }
            account = name;
        }
        save();
        return send(200, "text/plain", "ok\t" + (account ? u.coin | 0 : w.coins) + "\t" + account + "\n");
    }
    if (url.pathname === "/classic/levels.txt") {       // for the engine's "Social levels" list: one level a line, key, name, user and plays with tabs between
        const clean = (v) => String(v).replace(/[\t\r\n]+/g, " ");
        const all = Object.values(db.levels).filter((l) => l.data && l.published !== false);
        const num = (l) => parseInt(String(l.key).slice(1), 10) || 0;
        const mine = all.filter((l) => l.user !== "Rovio").sort((a, b) => num(b) - num(a)), rovio = all.filter((l) => l.user === "Rovio").sort((a, b) => num(a) - num(b));
        return send(200, "text/plain", mine.concat(rovio).map((l) => { const b = best(l.key); return [l.key, clean(l.name), clean(l.user), l.plays | 0, b ? b.score : 0, b ? clean(b.username) : ""].join("\t"); }).join("\n") + "\n");
    }
    if (url.pathname === "/classic/score") {            // the engine sends the score of a cleared Social level: /classic/score?key=L5&player=Name&score=12345
        const key = url.searchParams.get("key") || "", player = String(url.searchParams.get("player") || "").replace(/[^\w .-]/g, "").slice(0, 24) || "Player", score = parseInt(url.searchParams.get("score"), 10) || 0;
        const lv = db.levels[key]; if (!lv || score <= 0) return send(400, "text/plain", "no such level or no score");
        if (tooMany(callerIp, "score", 120)) return send(429, "text/plain", "too many scores"); countTry(callerIp, "score");
        const s = (db.scores[key] = db.scores[key] || {});
        if (!s[player] || score > s[player].score) s[player] = { username: player, score };
        const position = Object.values(s).sort((a, b) => b.score - a.score).findIndex((e) => e.username === player) + 1;
        note(player, "scored " + score + " on \"" + lv.name + "\" in the engine" + (position === 1 ? " (best so far)" : ""));
        save();
        console.log("classic score", key, player, score, "position", position);
        return send(200, "text/plain", "position " + position + "\n");
    }
    if (url.pathname === "/classic/play") {             // the engine started a level: counts as a play
        const lv = db.levels[url.searchParams.get("key") || ""]; if (!lv) return send(404, "text/plain", "no such level");
        lv.plays++; note(String(url.searchParams.get("player") || "Player").replace(/[^\w .-]/g, "").slice(0, 24) || "Player", "plays \"" + lv.name + "\" in the engine"); save();
        return send(200, "text/plain", "ok\n");
    }
    if (url.pathname === "/classic/players") {         // every profile and every level of everyone, open to anybody (no e-mail, password or token is shown)
        const levels = Object.values(db.levels).filter((l) => l.data && l.published !== false && l.user !== "Rovio");
        const cards = Object.values(db.users).map((u) => {
            const mine = levels.filter((l) => l.user === u.name);
            const wins = Object.values(db.levels).filter((l) => { const b = best(l.key); return b && b.username === u.name; }).length;
            return "<div style='display:inline-block;vertical-align:top;width:21em;margin:6px;padding:10px;border:1px solid #999;border-radius:6px'>" +
                "<img src='/services/downloadProfileImage?user=" + encodeURIComponent(u.name) + "' width=64 height=64 style='float:left;margin-right:10px;background:#ddd'>" +
                "<b>" + esc(u.name) + "</b><br>Level " + (u.level | 0) + " &middot; " + mine.length + " level" + (mine.length === 1 ? "" : "s") + " &middot; " + wins + " best score" + (wins === 1 ? "" : "s") +
                "<div style='clear:both'></div>" + mine.map((l) => "<div><a href='/classic/" + l.key + ".lua'>" + esc(l.name) + "</a> (" + (l.plays | 0) + " plays)</div>").join("") + "</div>";
        }).join("");
        return send(200, "text/html", "<!doctype html><meta charset=utf-8><title>Players</title><body style='font:15px sans-serif;max-width:70em;margin:2em auto'><h2>Players and their levels</h2>" + (cards || "<p>nobody has signed up yet</p>") + "<p><a href='/classic'>All levels</a></p>");
    }
    if (url.pathname === "/classic/news.json") {       // the newest things that happened, for the News box of the V1 page
        return send(200, "application/json", JSON.stringify((db.activity || []).slice(-12).reverse().map((x) => ({ user: x.user, text: x.text, t: x.t }))), { "Access-Control-Allow-Origin": "*" });
    }
    if (url.pathname === "/classic/live") {             // who is playing and what just happened; reloads itself
        const now = Date.now(), online = Object.keys(seen).filter((n) => now - seen[n] < 5 * 60 * 1000);
        const tops = Object.values(db.levels).map((l) => ({ l, b: best(l.key) })).filter((x) => x.b).sort((a, b) => b.b.score - a.b.score).slice(0, 10);
        const act = (db.activity || []).slice(-30).reverse().map((a) => "<tr><td>" + new Date(a.t).toLocaleTimeString() + "</td><td>" + esc(a.user) + "</td><td>" + esc(a.text) + "</td></tr>").join("");
        return send(200, "text/html", "<!doctype html><meta charset=utf-8><meta http-equiv=refresh content=5><title>Live</title><body style='font:15px sans-serif;max-width:46em;margin:2em auto'>" +
            "<h2>Playing now</h2><p>" + (online.length ? online.map(esc).join(", ") : "nobody in the last five minutes") + "</p>" +
            "<h3>Best scores</h3><table cellpadding=5 border=1 style='border-collapse:collapse'>" + (tops.map((x) => "<tr><td>" + esc(x.l.name) + "</td><td>" + esc(x.b.username) + "</td><td>" + x.b.score + "</td></tr>").join("") || "<tr><td>no scores yet</td></tr>") + "</table>" +
            "<h3>What just happened</h3><table cellpadding=5 border=1 style='border-collapse:collapse'>" + (act || "<tr><td>nothing yet</td></tr>") + "</table><p><a href='/classic'>Back</a></p>");
    }
    if (url.pathname === "/join") {                     // the page a friend opens to play on this server
        const ip = publicHost(req);
        return send(200, "text/html", "<!doctype html><meta charset=utf-8><title>Join</title><body style='font:15px sans-serif;max-width:46em;margin:2em auto'>" +
            "<h2>Play on this server</h2><ol><li>Download <a href='/join.bat'>join.bat</a> and run it as administrator once. It adds two lines to the hosts file (" + esc(ip) + " for sl.angry-birds-social.appspot.com and angry-birds-social.appspot.com).</li>" +
            "<li>In a browser with the Flash plugin open <a href='http://sl.angry-birds-social.appspot.com/'>http://sl.angry-birds-social.appspot.com/</a> and log in with any e-mail and password (the first login makes your account).</li>" +
            "<li>For the engine: set <code>socialLevels.server = \"http://" + esc(ip) + "\"</code> and <code>socialLevels.player = \"your name\"</code> in extra_scripts.lua.</li></ol>" +
            "<p><a href='/classic/live'>Who is playing now</a></p>");
    }
    const dl = url.pathname.match(/^\/classic\/(\w+)\.lua$/);
    if (dl) {
        const l = db.levels[dl[1]]; if (!l || !l.data) return send(404, "text/plain", "no such level");
        const r = classic.socialToClassic(Buffer.from(l.data, "base64").toString("utf8"), "Social_" + l.key + ".lua");
        console.log("classic export", l.key, r.dropped.length ? "(left out: " + r.dropped.join(", ") + ")" : "");
        return send(200, "text/plain", r.lua, { "Content-Disposition": 'attachment; filename="Social_' + l.key + '.lua"' });
    }
    if (url.pathname === "/classic/import" && req.method === "POST") {
        const form = parseForm(body), lua = form.lua || "";
        if (!/world\s*=\s*\{/.test(lua)) return send(400, "text/html", "That does not look like a Classic level file. <a href='/classic'>Back</a>");
        const name = String(form.name || "Imported level").slice(0, 40);
        const r = classic.classicToSocial(lua, name);
        const user = Object.values(db.users)[0];
        if (!user) return send(400, "text/html", "Log in to the game once first. <a href='/classic'>Back</a>");
        const key = "L" + (db.nextLevel++);
        db.levels[key] = { key, user: user.name, name, data: Buffer.from(r.xml).toString("base64"), screenShot: thumbnail.render(Buffer.from(r.xml).toString("base64")) || "", published: true, plays: 0, likes: 0, silver: 0, gold: 0, cost: 0 };
        save();
        console.log("classic import", key, r.dropped.length ? "(left out: " + r.dropped.join(", ") + ")" : "");
        return send(200, "text/html", "<p>Imported as " + key + " (" + esc(name) + ")." + (r.dropped.length ? " Left out, no Social equivalent: " + esc(r.dropped.join(", ")) : "") + "</p><p><a href='/classic'>Back</a></p>");
    }
    const rows = Object.values(db.levels).filter((l) => l.data).map((l) => "<tr><td>" + esc(l.name) + "</td><td>" + esc(l.user) + "</td><td>" + l.plays + "</td><td><a href='/classic/" + l.key + ".lua'>Download for Classic (.lua)</a></td></tr>").join("");
    send(200, "text/html", "<!doctype html><meta charset=utf-8><title>Social levels for Classic</title><body style='font:15px sans-serif;max-width:46em;margin:2em auto'>" +
        "<h2>Social levels for Angry Birds Classic</h2><table cellpadding=6 border=1 style='border-collapse:collapse'><tr><th>Level</th><th>By</th><th>Plays</th><th></th></tr>" + rows + "</table>" +
        "<p>Put the downloaded file in the engine window (drop it on it while the level editor is open) to place the level, or save it into the game's data/levels folder.</p>" +
        "<h3>Send a Classic level to Social</h3><form method=post action=/classic/import>Name: <input name=name value='Classic level'><br><textarea name=lua rows=8 cols=70 placeholder='paste the text of a Classic level file (.lua) here'></textarea><br><button>Import</button></form>" +
        "<p><a href='/classic/live'>Who is playing now</a> &middot; <a href='/join'>Invite a friend</a> &middot; <a href='/'>Back to the game</a></p>");
}

http.createServer((req, res) => {
    if (req.method === "OPTIONS") { res.writeHead(204, { "Access-Control-Allow-Origin": "*", "Access-Control-Allow-Headers": "*" }); return res.end(); }
    // the Flash player asks for the policy file first when the page and the server differ
    if (req.url === "/favicon.ico") { res.writeHead(204); return res.end(); }
    if (req.url.startsWith("/services/downloadProfileImage")) {
        const q = new URL(req.url, "http://x").searchParams; let png = null;
        for (const v of q.values()) { const u = db.users[v] || Object.values(db.users).find(x => x.name === v || x.id === v); if (u && u.profileImage) { png = Buffer.from(u.profileImage, "base64"); break; } }
        res.writeHead(200, { "Content-Type": png && png[0] === 0xff && png[1] === 0xd8 ? "image/jpeg" : "image/png", "Cache-Control": "no-store" });          // the V1 creator sends a JPEG, the V2 one a PNG
        return res.end(png || Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==", "base64"));
    }
    if (req.url === "/crossdomain.xml") {
        res.writeHead(200, { "Content-Type": "text/xml" });
        return res.end('<?xml version="1.0"?><cross-domain-policy><allow-access-from domain="*"/></cross-domain-policy>');
    }
    const chunks = [];
    let size = 0;
    req.on("data", (c) => { size += c.length; if (size > LIMIT_BODY) { res.writeHead(413); res.end(); req.destroy(); } else chunks.push(c); });
    req.on("end", () => {
        if (size > LIMIT_BODY) return;
        callerIp = req.socket.remoteAddress.replace(/^::ffff:/, "");
        const url = new URL(req.url, "http://localhost");
        const m = url.pathname.match(/^\/services\/(\w+)\/?$/);
        const params = Object.assign({}, Object.fromEntries(url.searchParams), parseForm(Buffer.concat(chunks).toString("utf8")));
        const cmd = m ? m[1] : params.C;
        const cookie = (/(?:^|; )abs=(\w+)/.exec(req.headers.cookie || "") || [])[1];
        const keys = () => (cookie ? ["c:" + cookie] : []).concat(["i:" + callerIp + "|" + (req.headers["user-agent"] || "").slice(0, 80)]);     // a cookie when the browser keeps it, else the address and browser
        if (!m && url.pathname === "/session") {          // who signed in last from this computer: every Social page signs in again by itself, on every domain
            const who = keys().map((k) => (db.sessions || {})[k]).find((x) => x && db.tokens[x.token]);
            res.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store" });
            return res.end(JSON.stringify(who && db.tokens[who.token] ? who : {}));
        }
        if (!m && url.pathname === "/signout") {          // forget who signed in from this computer (the pages stop signing in by themselves)
            if (db.sessions) keys().forEach((k) => delete db.sessions[k]);
            save();
            res.writeHead(200, { "Content-Type": "text/plain" });
            return res.end("ok");
        }
        if (!m && url.pathname === "/join.bat") {
            const ip = publicHost(req);
            const bat = "@echo off\r\nnet session >nul 2>&1 || (echo Run this file as administrator: right-click it, Run as administrator. & pause & exit /b 1)\r\n" +
                "set H=%SystemRoot%\\System32\\drivers\\etc\\hosts\r\n" +
                "findstr /v /c:\"angry-birds-social.appspot.com\" /c:\"dev.angrybirds.com\" \"%H%\" > \"%TEMP%\\hosts.new\"\r\n" +
                DOMAINS.filter((d) => d.includes(".") && !/^[0-9.]+$/.test(d)).map((d) => "echo " + ip + " " + d + ">> \"%TEMP%\\hosts.new\"\r\n").join("") +
                "copy /y \"%TEMP%\\hosts.new\" \"%H%\" >nul\r\necho Done. Open http://sl.angry-birds-social.appspot.com/ in the browser with Flash.\r\npause\r\n";
            res.writeHead(200, { "Content-Type": "application/octet-stream", "Content-Disposition": 'attachment; filename="join.bat"' });
            return res.end(bat);
        }
        if (!m && (url.pathname.startsWith("/classic") || url.pathname === "/join")) return classicRoutes(req, res, url, Buffer.concat(chunks).toString("utf8"));
        if (!m && !params.C && (req.method === "GET" || req.method === "HEAD")) {          // the game's page and files
            let rel = decodeURIComponent(url.pathname); if (rel.endsWith("/")) rel += "index.html";
            const file = path.normalize(path.join(WWW, rel));
            if (file.startsWith(WWW) && fs.existsSync(file) && fs.statSync(file).isFile()) {
                if (path.extname(file).toLowerCase() === ".html") { res.writeHead(200, { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" }); return res.end(withBar(fs.readFileSync(file, "utf8"))); }
                res.writeHead(200, { "Content-Type": MIME[path.extname(file).toLowerCase()] || "application/octet-stream", "Cache-Control": "no-store" });
                return fs.createReadStream(file).pipe(res);
            }
        }
        if (!cmd || !commands[cmd]) {
            console.log("unknown request:", req.method, req.url);
            res.writeHead(404, { "Content-Type": "text/plain" });
            return res.end("unknown command");
        }
        let out;
        try { out = commands[cmd](params); } catch (e) { console.log(cmd, "failed:", e); out = { E: "server error" }; }
        const shown = Object.keys(params).filter((k) => k !== "token").map((k) => k + "=" + String(params[k]).slice(0, 40)).join(" ");
        console.log(cmd, out && out.E ? "-> error: " + out.E : "ok", "|", shown);
        if (cmd === "login" && out && out.token) {
            db.sessions = db.sessions || {};
            const id = cookie || crypto.randomBytes(12).toString("hex");
            if (!cookie) res.setHeader("Set-Cookie", "abs=" + id + "; Path=/; Max-Age=31536000");
            const who = { token: out.token, username: out.username, email: String(params.auth || "") };
            db.sessions["c:" + id] = who; db.sessions["i:" + callerIp + "|" + (req.headers["user-agent"] || "").slice(0, 80)] = who;
            save();
        }
        reply(res, cmd, out);
    });
}).listen(PORT, "0.0.0.0", () => {
    console.log("Angry Birds Social server on port " + PORT + " (data: " + FILE + ")");
    const nets = require("os").networkInterfaces(), mine = [];
    for (const list of Object.values(nets)) for (const n of list) if (n.family === "IPv4" && !n.internal) mine.push(n.address);
    if (mine.length) console.log("Friends on your network open  http://" + mine[0] + (PORT === 80 ? "" : ":" + PORT) + "/join  to play here" + (mine.length > 1 ? "  (other addresses: " + mine.slice(1).join(", ") + ")" : ""));
});
