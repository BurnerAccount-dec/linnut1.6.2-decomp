// draws a small PNG preview of a Social level (base64 XML) - the Browse page shows it as the level's picture
const zlib = require("zlib");
const W = 200, H = 150;
const THEMES = { ThemeHills: [[120, 190, 245], [200, 235, 255], [95, 160, 60]], ThemeForest: [[90, 150, 130], [170, 215, 190], [60, 110, 50]],
                 ThemeVolcano: [[90, 40, 40], [200, 110, 70], [70, 55, 55]], ThemeIceCave: [[120, 170, 210], [215, 240, 255], [150, 190, 215]] };
const attr = (s, n) => { const m = new RegExp("\\b" + n + "\\s*=\\s*\"([^\"]*)\"").exec(s); return m ? m[1] : ""; };

function crc32(buf) { let c, crc = -1; for (let n = 0; n < buf.length; n++) { c = (crc ^ buf[n]) & 255; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xEDB88320 : c >>> 1; crc = (crc >>> 8) ^ c; } return (crc ^ -1) >>> 0; }
function chunk(type, data) { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td)); return Buffer.concat([len, td, c]); }
function png(px) {
    const raw = Buffer.alloc((W * 3 + 1) * H);
    for (let y = 0; y < H; y++) { raw[y * (W * 3 + 1)] = 0; px.copy(raw, y * (W * 3 + 1) + 1, y * W * 3, (y + 1) * W * 3); }
    const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 2;
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", zlib.deflateSync(raw, { level: 9 })), chunk("IEND", Buffer.alloc(0))]);
}

function shape(id) {                       // [halfW, halfH, round, colour] in Social cells
    let m = /(\d+)X(\d+)/.exec(id);
    const col = /^WOOD/.test(id) ? [190, 130, 60] : /^STONE/.test(id) ? [140, 140, 150] : /^GLASS/.test(id) ? [170, 225, 245] : /^ICE/.test(id) ? [190, 235, 255] : /^PIG/.test(id) ? [110, 190, 60] : /^BIRD/.test(id) ? [215, 50, 40] : /^MISC_ESTRADE/.test(id) ? [120, 90, 60] : [160, 120, 90];
    if (/^PIG/.test(id)) { const r = /LARGE|KING|BIG/.test(id) ? 1.9 : /SMALL/.test(id) ? 0.9 : 1.3; return [r, r, true, col]; }
    if (/^BIRD/.test(id)) return [0.9, 0.9, true, col];
    if (/CIRCLE|BALL|ROUND/.test(id)) { const r = /BIG|LARGE/.test(id) ? 1.9 : 1; return [r, r, true, col]; }
    if (m) return [+m[1] / 2, +m[2] / 2, false, col];
    if (/^GRAVEL|ROCK|STATIC|GROUND/.test(id)) return [1.5, 1.5, false, [120, 105, 95]];
    return [1, 1, false, col];
}

function render(xmlB64) {
    let xml; try { xml = Buffer.from(xmlB64, "base64").toString("utf8"); } catch (e) { return null; }
    const theme = THEMES[attr(xml, "background")] || THEMES.ThemeHills;
    const things = [];
    const re = /<(Item|Bird|Slingshot)\b([^>]*)>/g; let m;
    while ((m = re.exec(xml))) {
        const a = m[2], x = parseFloat(attr(a, "x")), y = parseFloat(attr(a, "y"));
        if (isNaN(x) || isNaN(y)) continue;
        const id = m[1] === "Bird" ? attr(a, "id") || "BIRD" : m[1] === "Slingshot" ? "MISC_SLING" : attr(a, "id");
        if (m[1] === "Slingshot") { things.push({ x, y: y + 1, hw: 0.5, hh: 2.5, rot: 0, round: false, col: [90, 60, 30] }); continue; }
        const s = shape(id);
        things.push({ x, y, hw: s[0], hh: s[1], round: s[2], col: s[3], rot: (parseFloat(attr(a, "rotation")) || 0) * Math.PI / 180 });
    }
    if (!things.length) return null;
    let x0 = Infinity, x1 = -Infinity, y0 = Infinity;
    for (const t of things) { const r = Math.max(t.hw, t.hh); x0 = Math.min(x0, t.x - r); x1 = Math.max(x1, t.x + r); y0 = Math.min(y0, t.y - r); }
    const gy = 50.5; y0 = Math.min(y0, gy - 8);
    let sc = Math.min(W / ((x1 - x0) + 6), H / ((gy - y0) + 5)), cx = (x0 + x1) / 2, cy = (gy + y0) / 2 + 0.5;
    const px = Buffer.alloc(W * H * 3);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {                       // sky and ground
        const wy = cy + (y - H / 2) / sc, t = y / H, o = (y * W + x) * 3;
        const c = wy >= gy ? theme[2] : [0, 1, 2].map((i) => theme[0][i] + (theme[1][i] - theme[0][i]) * t);
        px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2];
    }
    for (const t of things) {
        const r = Math.max(t.hw, t.hh) * sc + 2, sx = (t.x - cx) * sc + W / 2, sy = (t.y - cy) * sc + H / 2, cs = Math.cos(t.rot), sn = Math.sin(t.rot);
        for (let y = Math.max(0, Math.floor(sy - r)); y < Math.min(H, Math.ceil(sy + r)); y++) for (let x = Math.max(0, Math.floor(sx - r)); x < Math.min(W, Math.ceil(sx + r)); x++) {
            const dx = (x + 0.5 - sx) / sc, dy = (y + 0.5 - sy) / sc, u = dx * cs + dy * sn, v = -dx * sn + dy * cs;
            const inside = t.round ? (u * u + v * v <= t.hw * t.hw) : (Math.abs(u) <= t.hw && Math.abs(v) <= t.hh);
            if (!inside) continue;
            const edge = t.round ? (u * u + v * v > Math.pow(t.hw - 0.2, 2)) : (Math.abs(u) > t.hw - 0.2 || Math.abs(v) > t.hh - 0.2);
            const o = (y * W + x) * 3, k = edge ? 0.6 : 1;
            px[o] = t.col[0] * k; px[o + 1] = t.col[1] * k; px[o + 2] = t.col[2] * k;
        }
    }
    return png(px).toString("base64");
}
module.exports = { render };
