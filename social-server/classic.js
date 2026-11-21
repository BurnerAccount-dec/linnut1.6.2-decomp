// Converts levels between Angry Birds Social (XML) and Angry Birds Classic (Lua level files).
//
// Social measures in grid cells (a WOOD_BLOCK_8X1 is 8 cells long, y grows downwards). Classic measures in world
// units with y = 0 on the ground and negative upwards; one cell is 1.033 units (WoodBlock4 is 83 px = 4.1 units).
// Blocks, pigs and birds are matched by size; things Classic has no equivalent for are dropped and reported.

"use strict";

const K = 1.033;            // Classic units per Social cell
const GROUND_Y = 50;        // Social y of the ground line (the 10x10 gravel blocks sit centred on it)
const SLING_X = -12.25;     // Classic x of the first bird (the slingshot)

// Social background -> Classic theme (change here if you prefer another look)
const THEME_TO_CLASSIC = { ThemeHills: "theme1", ThemeForest: "theme2", ThemeVolcano: "theme3", ThemeIceCave: "theme4" };
const THEME_FROM_CLASSIC = { theme1: "ThemeHills", theme2: "ThemeForest", theme3: "ThemeVolcano", theme4: "ThemeIceCave" };   // other themes -> hills

// shape suffix -> Classic block number (1 = 1x1, 2 = 2x1, 3 = 2x2, 4 = 4x1, 5 = 4x2, 6 = 8x1, 7/8 = circles, 9 = 4x4 box,
// 10 = 10x1, 11/12 = triangles)
const SHAPES = {
    "1X1": 1, "2X1": 2, "2X2": 3, "4X1": 4, "4X2": 5, "8X1": 6, "10X1": 10,
    "CIRCLE_4X4": 7, "CIRCLE_2X2": 8, "4X4_HOLLOW": 9, "TRIANGLE_4X4": 11, "TRIANGLE_4X4_HOLLOW": 12,
};
const MATERIALS = { WOOD: "WoodBlock", STONE: "StoneBlock", GLASS: "LightBlock", ICE: "LightBlock" };

const SOCIAL_TO_CLASSIC = {
    PIG_BASIC_SMALL: "SmallPiglette", PIG_BASIC_MEDIUM: "MediumPiglette", PIG_BASIC_BIG: "LargePiglette",
    PIG_KING: "KingPiglette", PIG_HELMET: "HelmetPiglette", PIG_MUSTACHE: "GrandpaPiglette",
    BIRD_RED: "RedBird", BIRD_BLUE: "SmallBlueBird", BIRD_YELLOW: "YellowBird", BIRD_BLACK: "BlackBird",
    BIRD_WHITE: "BasicBird2", BIRD_GREEN: "BoomerangBird", BIRD_RED_BIG: "RedBigBird",
    MISC_CHEST: "ExtraTreasureChest", MISC_DICE: "ExtraBlockDice", MISC_DUCK: "ExtraRubberDuck", MISC_BALL: "ExtraBeachBall",
    MISC_FOOD_DONUT: "ExtraDonut01", MISC_FOOD_APPLE: "ExtraApple", MISC_FOOD_BANANA: "ExtraBanana", MISC_FOOD_HAM: "ExtraHam",
    MISC_FOOD_STRAWBERRY: "ExtraStrawberry", MISC_STRAWBERRY: "ExtraStrawberry", MISC_FOOD_WATERMELLON: "ExtraMelon",
    MISC_GOBLET: "ExtraHolyGrail", MISC_GOLDEN_EGG: "ExtraGoldenEgg", MISC_METAL_DIAMOND: "ExtraDiamond",
    MISC_PILLAR: "ExtraPillar", MISC_SMILEY: "ExtraBlockSmiley", MISC_TNT: "ExtraBlockTNT",
    MISC_WHEEL_BASIC: "ExtraTire02", MISC_WHEEL_DENTED: "ExtraTire02", MISC_WHEEL_TIRE: "ExtraTire01",
    MISC_ESTRADE_1: "Estrade01", MISC_ESTRADE_2: "Estrade01", MISC_ESTRADE_2X2: "Estrade02", MISC_ESTRADE_7X2: "Estrade03",
    MISC_ESTRADE_9X3: "Estrade03", MISC_ESTRADE_11: "Estrade03", MISC_ESTRADE_11X3: "Estrade03", TEXTURE_ESTRADE_STATIC: "Estrade02",
    TEXTURE_GRAVEL_BLOCK_10X10: "STATIC_05", TEXTURE_GRAVEL_BLOCK_5X5: "STATIC_04", TEXTURE_GRAVEL_BLOCK_5X2: "STATIC_01",
    TEXTURE_GRAVEL_STATIC: "STATIC_03",
};
for (const [mat, prefix] of Object.entries(MATERIALS)) {
    const add = (id, n) => { SOCIAL_TO_CLASSIC[id] = prefix + n; };
    for (const s of ["1X1", "2X1", "2X2", "4X1", "4X2", "8X1", "10X1"]) add(`${mat}_BLOCK_${s}`, SHAPES[s]);
    add(`${mat}_BLOCK_4X4_HOLLOW`, 9);
    add(`${mat}_CIRCLE_4X4`, 7); add(`${mat}_CIRCLE_2X2`, 8);
    add(`${mat}_TRIANGLE_4X4`, 11); add(`${mat}_TRIANGLE_4X4_HOLLOW`, 12);
}

// Classic definition -> Social id (first wins; Social's ice and glass are the same Classic block)
const CLASSIC_TO_SOCIAL = {};
for (const [soc, cl] of Object.entries(SOCIAL_TO_CLASSIC)) {
    if (/^ICE_/.test(soc) || /^(MISC_ESTRADE_(1|11|9X3|11X3|7X2)|MISC_STRAWBERRY|MISC_WHEEL_BASIC|TEXTURE_ESTRADE_STATIC|MISC_ESTRADE_2X2)$/.test(soc)) continue;
    if (!(cl in CLASSIC_TO_SOCIAL)) CLASSIC_TO_SOCIAL[cl] = soc;
}
for (const [cl, soc] of [["Estrade01", "MISC_ESTRADE_2"], ["Estrade02", "MISC_ESTRADE_2X2"], ["Estrade03", "MISC_ESTRADE_9X3"]]) CLASSIC_TO_SOCIAL[cl] = soc;
CLASSIC_TO_SOCIAL.LightBlock1 = "GLASS_BLOCK_1X1";
const BIRD_IDS = new Set(["BasicBird2", "RedBird", "SmallBlueBird", "YellowBird", "BlackBird", "BoomerangBird", "RedBigBird"]);

// Social's other gravel pieces. [Classic static, dx, dy] in Social cells from the piece's centre (turned with it).
// 1x1 and 2x2 use Classic's small 20x20 static block (STATIC_06); the circles are the engine's round static rocks (StaticBlockThemeNN_07 / _08 / _09, which the engine adds; the retail game has none).
const SOCIAL_STATIC_TILES = {
    TEXTURE_GRAVEL_BLOCK_1X1: [["STATIC_06", 0, 0]],
    TEXTURE_GRAVEL_BLOCK_2X2: [["STATIC_06", -0.5, -0.5], ["STATIC_06", 0.5, -0.5], ["STATIC_06", -0.5, 0.5], ["STATIC_06", 0.5, 0.5]],
    TEXTURE_GRAVEL_BLOCK_10X2: [["STATIC_02", 0, 0]],
    TEXTURE_GRAVEL_BLOCK_32X2: [["STATIC_03", 0, 0]],
    TEXTURE_GRAVEL_CIRCLE_2X2: [["STATIC_07", 0, 0]],
    TEXTURE_GRAVEL_CIRCLE_5X5: [["STATIC_08", 0, 0]],
    TEXTURE_GRAVEL_CIRCLE_10X10: [["STATIC_09", 0, 0]],
};

function staticName(code, theme) {
    const n = String(theme || "theme1").replace("theme", "").padStart(2, "0");
    return "StaticBlockTheme" + n + "_" + code.split("_")[1];
}

const f = (v) => String(+(+v).toFixed(5));
const attr = (s, k) => { const m = new RegExp("\\b" + k + "\\s*=\\s*\"([^\"]*)\"").exec(s); return m ? m[1] : null; };

function parseSocial(xml) {
    const level = { items: [], birds: [], background: attr(xml, "background") || "ThemeHills" };
    const head = /<Level\b[^>]*>/.exec(xml);
    level.silver = +attr(head ? head[0] : "", "scoreSilver") || 0;
    level.gold = +attr(head ? head[0] : "", "scoreGold") || 0;
    const sl = /<Slingshot\b[^>]*>/.exec(xml);
    level.sling = { x: sl ? +attr(sl[0], "x") : 27.78, y: sl ? +attr(sl[0], "y") : 36.84 };
    for (const m of xml.matchAll(/<Bird\b[^>]*>/g)) level.birds.push({ id: attr(m[0], "id"), x: +attr(m[0], "x"), y: +attr(m[0], "y") });
    for (const m of xml.matchAll(/<Item\b[^>]*>/g)) level.items.push({ id: attr(m[0], "id"), x: +attr(m[0], "x"), y: +attr(m[0], "y"), rot: +attr(m[0], "rotation") || 0 });
    return level;
}

// Camera blocks. Like the original levels: a view is its centre (px, py, in pixels = world units x 20) and a zoom;
// the top-level block is the 1680x1024 view, "ipad" 1024x768 and "iphone" 480x320.
function view(sw, sh, cx, cy, wPx, hPx) {
    const s = +Math.min(sw / wPx, sh / hPx).toFixed(5);
    return { s, px: +cx.toFixed(2), py: +cy.toFixed(2), sw, sh, left: +(cx - sw / s / 2).toFixed(2), right: +(cx + sw / s / 2).toFixed(2), top: +(cy - sh / s / 2).toFixed(2), bottom: +(cy + sh / s / 2).toFixed(2) };
}
function cameras(minX, maxX, topY) {
    minX = Math.max(minX, -20); const w = (maxX - minX) * 20 + 500, topPx = Math.min(topY * 20 - 120, -450), bottomPx = 230;
    const cx = (minX + maxX) / 2 * 20, cy = (topPx + bottomPx) / 2, h = bottomPx - topPx;
    const base = view(1680, 1024, cx, cy, w, h), pad = view(1024, 768, cx, cy, w, h), phone = view(480, 320, cx, cy, w, h);
    const sub = (v) => `{bottom = ${v.bottom}, screenWidth = ${v.sw}, py = ${v.py}, screenHeight = ${v.sh}, top = ${v.top}, sy = ${v.s}, sx = ${v.s}, px = ${v.px}, left = ${v.left}, right = ${v.right}}`;
    return `birdCameraData = {
  bottom = -57.1832, px = -121.32, py = -192.773, left = -494.189, screenWidth = 1680, top = -420.046,
  iphone = {bottom = -57.1832, screenWidth = 480, py = -192.773, screenHeight = 320, top = -420.046, sy = 0.704, sx = 0.704, px = -121.32, left = -462.229, right = 1091.26},
  sy = 2.2528, sx = 2.2528, version = "0.02", right = 1117.51,
  ipad = {bottom = 282.004, screenWidth = 1024, py = -256.525, screenHeight = 768, top = -757.973, sy = 0.765783, sx = 0.765783, right = 1545.5, left = -539.421, px = 129.176},
  screenHeight = 1024,
}
castleCameraData = {
  bottom = ${base.bottom}, px = ${base.px}, py = ${base.py}, left = ${base.left}, screenWidth = 1680, top = ${base.top},
  iphone = ${sub(phone)},
  sy = ${base.s}, sx = ${base.s}, right = ${base.right}, version = "0.02",
  ipad = ${sub(pad)},
  screenHeight = 1024,
}
`;
}

// Social XML text -> { lua, dropped:[ids], counts }
function socialToClassic(xml, fileName) {
    const lv = parseSocial(xml);
    const theme = THEME_TO_CLASSIC[lv.background] || "theme1";
    const dropped = [], counts = {}, entries = [];
    const nextId = {};
    const add = (def, x, y, angle, extra) => {
        counts[def] = (counts[def] || 0) + 1;
        nextId[def] = (nextId[def] || 0) + 1;
        entries.push({ name: def + "_" + nextId[def], def, x, y, angle, extra });
    };
    const b1 = lv.birds[0] || { x: 24.92, y: 43 };
    let n = 0;
    for (const b of lv.birds) {
        const def = SOCIAL_TO_CLASSIC[b.id];
        if (!def) { dropped.push(b.id); continue; }
        n++;
        add(def, SLING_X + (b.x - b1.x) * K, (b.y - GROUND_Y) * K, 0, "startNumber = " + n + ",");
    }
    let minX = 0, maxX = 60, topY = -20;
    for (const it of lv.items) {
        const tiles = SOCIAL_STATIC_TILES[String(it.id).toUpperCase()];
        if (tiles) {
            const a = it.rot * Math.PI / 180, c = Math.cos(a), sn = Math.sin(a);
            for (const [code, dx, dy] of tiles) {
                const x = (it.x + dx * c - dy * sn - lv.sling.x) * K + SLING_X, y = (it.y + dx * sn + dy * c - GROUND_Y) * K;
                minX = Math.min(minX, x); maxX = Math.max(maxX, x); topY = Math.min(topY, y);
                add(staticName(code, theme), x, y, a, "");
            }
            continue;
        }
        let def = SOCIAL_TO_CLASSIC[it.id];
        if (!def) { dropped.push(it.id); continue; }
        if (def.startsWith("STATIC_")) def = staticName(def, theme);
        const x = (it.x - lv.sling.x) * K + SLING_X, y = (it.y - GROUND_Y) * K;
        minX = Math.min(minX, x); maxX = Math.max(maxX, x); topY = Math.min(topY, y);
        add(def, x, y, it.rot * Math.PI / 180, "");
    }
    let out = `theme = "${theme}"\nfilename = "${fileName || "SocialLevel.lua"}"\nphysicsToWorld = 20\njoints = {}\nthemeSprites = {}\n`;
    out += cameras(minX, maxX, topY);
    out += "counts = {\n" + Object.entries(counts).map(([k, v]) => `  ${k} = ${v},`).join("\n") + "\n  Ground = 1,\n}\n";
    out += "world = {\n  ground = {y = 5, x = 20.9039, name = \"ground\", angle = 0, definition = \"Ground\"},\n";
    for (const e of entries) {
        out += `  ${e.name} = {y = ${f(e.y)}, x = ${f(e.x)}, name = "${e.name}", ${e.extra ? e.extra + " " : ""}angle = ${f(e.angle)}, definition = "${e.def}"},\n`;
    }
    out += "}\n";
    return { lua: out, dropped, counts };
}

// Classic static rocks (StaticBlockThemeNN_0k) are built from Social's gravel pieces: [id, how many side by side, spacing in cells]
const STATIC_TILES = { 1: ["TEXTURE_GRAVEL_BLOCK_5X2", 1, 0], 2: ["TEXTURE_GRAVEL_BLOCK_5X2", 2, 4.75], 3: ["TEXTURE_GRAVEL_BLOCK_5X2", 6, 5.2], 4: ["TEXTURE_GRAVEL_BLOCK_5X5", 1, 0], 5: ["TEXTURE_GRAVEL_BLOCK_10X10", 1, 0], 6: ["TEXTURE_GRAVEL_BLOCK_1X1", 1, 0], 7: ["TEXTURE_GRAVEL_CIRCLE_2X2", 1, 0], 8: ["TEXTURE_GRAVEL_CIRCLE_5X5", 1, 0], 9: ["TEXTURE_GRAVEL_CIRCLE_10X10", 1, 0] };

// Classic level text -> { xml, dropped }. limits = {silverScore, goldScore} from starLimits.lua, when known
function classicToSocial(lua, name, limits) {
    const theme = (/^theme\s*=\s*"(\w+)"/m.exec(lua) || [])[1] || "theme1";
    const wi = lua.indexOf("\nworld = {");
    const blocks = [];
    const re = /\{([^{}]*definition\s*=\s*"(\w+)"[^{}]*)\}/g;
    re.lastIndex = wi < 0 ? 0 : wi;
    let m;
    const num = (s, k) => { const r = new RegExp("\\b" + k + "\\s*=\\s*(-?[\\d.]+(?:[eE][-+]?\\d+)?)").exec(s); return r ? +r[1] : 0; };
    while ((m = re.exec(lua))) blocks.push({ def: m[2], x: num(m[1], "x"), y: num(m[1], "y"), angle: num(m[1], "angle"), start: num(m[1], "startNumber") });
    const dropped = [], birds = [], items = [];
    const first = blocks.filter((b) => BIRD_IDS.has(b.def)).sort((p, q) => (p.start || 99) - (q.start || 99))[0];
    const sx = first ? first.x : SLING_X;                       // the slingshot stands where the first bird is
    const toX = (x) => (x - sx) / K + 27.78, toY = (y) => y / K + GROUND_Y;
    for (const b of blocks) {
        if (b.def === "Ground") continue;
        const rot = ((b.angle * 180 / Math.PI) % 360 + 360) % 360;
        const sm = /^StaticBlockTheme\d+_0(\d)$/.exec(b.def);
        if (sm && STATIC_TILES[sm[1]]) {
            const [id, count, step] = STATIC_TILES[sm[1]], a = b.angle;
            for (let i = 0; i < count; i++) {
                const d = (i - (count - 1) / 2) * step;
                items.push({ id, x: toX(b.x) + d * Math.cos(a), y: toY(b.y) + d * Math.sin(a), rot });
            }
            continue;
        }
        const id = CLASSIC_TO_SOCIAL[b.def];
        if (!id) { dropped.push(b.def); continue; }
        if (BIRD_IDS.has(b.def)) birds.push({ id, order: b.start || 99, x: toX(b.x), y: toY(b.y) });
        else items.push({ id, x: toX(b.x), y: toY(b.y), rot });
    }
    birds.sort((p, q) => p.order - q.order);
    const xs = items.map((i) => i.x), minX = Math.min(27.78, ...xs), maxX = Math.max(60, ...xs);
    const slingY = birds.length ? birds[0].y - 6.16 : 36.84;
    let xml = `<Level id="${name || "Imported"}" background="${THEME_FROM_CLASSIC[theme] || "ThemeHills"}" LevelExtension="2" scoreSilver="${limits ? limits.silverScore : 0}" scoreGold="${limits ? limits.goldScore : 0}" AutoCamera="True">\n`;
    xml += `  <Cameras>\n     <Camera id = "Slingshot" leftBorder = "-8.62" topBorder = "12.5" rightBorder = "54.7" bottomBorder = "52.5"></Camera>\n`;
    xml += `     <Camera id = "Castle" leftBorder = "${f(minX - 10)}" topBorder = "15.25" rightBorder = "${f(maxX + 10)}" bottomBorder = "65.25"></Camera>\n  </Cameras>\n`;
    xml += `<Slingshot x = "27.78" y = "${f(slingY)}">\n    <Birds>\n`;
    birds.forEach((b) => { xml += `        <Bird id = "${b.id}" x = "${f(b.x - 2.86)}" y = "${f(b.y)}"></Bird>\n`; });
    xml += `    </Birds>\n</Slingshot>\n`;
    for (const it of items) xml += `    <Item id = "${it.id}" x = "${f(it.x)}" y = "${f(it.y)}" rotation = "${f(it.rot)}"></Item>\n`;
    xml += "</Level>";
    return { xml, dropped };
}

module.exports = { socialToClassic, classicToSocial, parseSocial };
