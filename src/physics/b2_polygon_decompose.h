// The polygon decomposition Angry Birds Stella links in (iOS 1.x, 0x5c7230 to 0x5cb000): Box2D's contributed
// b2Polygon code (Eric Jordan). decomposePolygon (0x392fc) builds a b2Polygon from the points and calls
// DecomposeConvex(poly, results, n - 2) (0x5c9a4c):
//   - a polygon whose signed area is above 0 is reversed first (the ear test wants the other winding);
//   - TriangulatePolygon (0x5c8afc) splits at a pinch point (two vertices within 0.001 that are not neighbours), then clips
//     ears, each time the ear whose smallest angle is the largest (start -10); with no ear left it keeps the triangles it has;
//   - PolygonizeTriangles joins neighbouring triangles while the result stays convex and has at most 8 vertices, then merges
//     parallel edges (2 degrees, b2_angularSlop); a piece left with fewer than 3 vertices is dropped.
// Everything is float, as in the game. Returns the number of pieces (-1: no triangle could be made).
#pragma once
#include <cmath>
#include <utility>
#include <vector>

namespace ab {
namespace b2poly {

struct Poly { std::vector<float> x, y; int n() const { return (int)x.size(); } };

struct Tri {
    float x[3], y[3];
    Tri() { for (int i = 0; i < 3; i++) x[i] = y[i] = 0; }
    Tri(float x1, float y1, float x2, float y2, float x3, float y3) {
        float dx1 = x2 - x1, dx2 = x3 - x1, dy1 = y2 - y1, dy2 = y3 - y1;
        float cross = dx1 * dy2 - dx2 * dy1;
        if (cross > 0) { x[0] = x1; x[1] = x2; x[2] = x3; y[0] = y1; y[1] = y2; y[2] = y3; }
        else { x[0] = x1; x[1] = x3; x[2] = x2; y[0] = y1; y[1] = y3; y[2] = y2; }
    }
    bool IsInside(float px, float py) const {
        if (px < x[0] && px < x[1] && px < x[2]) return false;
        if (px > x[0] && px > x[1] && px > x[2]) return false;
        if (py < y[0] && py < y[1] && py < y[2]) return false;
        if (py > y[0] && py > y[1] && py > y[2]) return false;
        float vx2 = px - x[0], vy2 = py - y[0];
        float vx1 = x[1] - x[0], vy1 = y[1] - y[0];
        float vx0 = x[2] - x[0], vy0 = y[2] - y[0];
        float dot00 = vx0 * vx0 + vy0 * vy0, dot01 = vx0 * vx1 + vy0 * vy1, dot02 = vx0 * vx2 + vy0 * vy2;
        float dot11 = vx1 * vx1 + vy1 * vy1, dot12 = vx1 * vx2 + vy1 * vy2;
        float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
        float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        float v = (dot00 * dot12 - dot01 * dot02) * invDenom;
        return (u > 0) && (v > 0) && (u + v < 1);
    }
};

inline int Rem(int x, int modulus) { int r = x % modulus; return r < 0 ? r + modulus : r; }

inline float Area(const Poly& p) {
    float a = 0;
    const int n = p.n();
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; a += p.x[i] * p.y[j] - p.x[j] * p.y[i]; }
    return a * 0.5f;
}

inline bool IsEar(int i, const std::vector<float>& xv, const std::vector<float>& yv) {
    const int n = (int)xv.size();
    if (i >= n || i < 0 || n < 3) return false;
    int upper = i + 1, lower = i - 1;
    float dx0, dy0, dx1, dy1;
    if (i == 0) { dx0 = xv[0] - xv[n - 1]; dy0 = yv[0] - yv[n - 1]; dx1 = xv[1] - xv[0]; dy1 = yv[1] - yv[0]; lower = n - 1; }
    else if (i == n - 1) { dx0 = xv[i] - xv[i - 1]; dy0 = yv[i] - yv[i - 1]; dx1 = xv[0] - xv[i]; dy1 = yv[0] - yv[i]; upper = 0; }
    else { dx0 = xv[i] - xv[i - 1]; dy0 = yv[i] - yv[i - 1]; dx1 = xv[i + 1] - xv[i]; dy1 = yv[i + 1] - yv[i]; }
    float cross = dx0 * dy1 - dx1 * dy0;
    if (cross > 0) return false;
    Tri t(xv[i], yv[i], xv[upper], yv[upper], xv[lower], yv[lower]);
    for (int j = 0; j < n; j++) {
        if (j == i || j == lower || j == upper) continue;
        if (t.IsInside(xv[j], yv[j])) return false;
    }
    return true;
}

inline bool ResolvePinchPoint(const Poly& pin, Poly& a, Poly& b) {
    const int n = pin.n();
    if (n < 3) return false;
    const float tol = 0.001f;
    int ia = -1, ib = -1;
    for (int i = 0; i < n && ia < 0; i++)
        for (int j = i + 1; j < n; j++)
            if (std::fabs(pin.x[i] - pin.x[j]) < tol && std::fabs(pin.y[i] - pin.y[j]) < tol && j != i + 1) { ia = i; ib = j; break; }
    if (ia < 0) return false;
    int sizeA = ib - ia;
    if (sizeA == n) return false;
    a = Poly(); b = Poly();
    for (int i = 0; i < sizeA; i++) { int k = Rem(ia + i, n); a.x.push_back(pin.x[k]); a.y.push_back(pin.y[k]); }
    int sizeB = n - sizeA;
    for (int i = 0; i < sizeB; i++) { int k = Rem(ib + i, n); b.x.push_back(pin.x[k]); b.y.push_back(pin.y[k]); }
    return true;
}

inline int Triangulate(const std::vector<float>& xv, const std::vector<float>& yv, std::vector<Tri>& out) {
    out.clear();
    if (xv.size() < 3) return 0;
    Poly pin; pin.x = xv; pin.y = yv;
    Poly pa, pb;
    if (ResolvePinchPoint(pin, pa, pb)) {
        std::vector<Tri> ta, tb;
        int na = Triangulate(pa.x, pa.y, ta), nb = Triangulate(pb.x, pb.y, tb);
        if (na == -1 || nb == -1) return -1;
        out = ta; out.insert(out.end(), tb.begin(), tb.end());
        return na + nb;
    }
    std::vector<float> xr = xv, yr = yv;
    std::vector<Tri> buffer;
    int n = (int)xr.size();
    while (n > 3) {
        int ear = -1;
        float best = -10.0f;
        for (int i = 0; i < n; i++) {
            if (!IsEar(i, xr, yr)) continue;
            int lower = Rem(i - 1, n), upper = Rem(i + 1, n);
            float d1x = xr[upper] - xr[i], d1y = yr[upper] - yr[i];
            float d2x = xr[i] - xr[lower], d2y = yr[i] - yr[lower];
            float d3x = xr[lower] - xr[upper], d3y = yr[lower] - yr[upper];
            auto norm = [](float& dx, float& dy) { float l = std::sqrt(dx * dx + dy * dy); if (l >= 1.1920929e-07f) { float inv = 1.0f / l; dx *= inv; dy *= inv; } };
            norm(d1x, d1y); norm(d2x, d2y); norm(d3x, d3y);
            float c12 = std::fabs(d1x * d2y - d1y * d2x), c23 = std::fabs(d2x * d3y - d2y * d3x), c31 = std::fabs(d3x * d1y - d3y * d1x);
            float m = std::fmin(c12, std::fmin(c23, c31));
            if (m > best) { ear = i; best = m; }
        }
        if (ear == -1) { out = buffer; return (int)buffer.size(); }
        n--;
        std::vector<float> nx((size_t)n), ny((size_t)n);
        int src = 0;
        for (int i = 0; i < n; i++) { if (src == ear) src++; nx[(size_t)i] = xr[(size_t)src]; ny[(size_t)i] = yr[(size_t)src]; src++; }
        int under = ear == 0 ? n : ear - 1;
        int over = ear == n ? 0 : ear + 1;
        buffer.push_back(Tri(xr[(size_t)ear], yr[(size_t)ear], xr[(size_t)over], yr[(size_t)over], xr[(size_t)under], yr[(size_t)under]));
        xr.swap(nx); yr.swap(ny);
    }
    buffer.push_back(Tri(xr[1], yr[1], xr[2], yr[2], xr[0], yr[0]));
    out = buffer;
    return (int)buffer.size();
}

inline bool IsConvex(const Poly& p) {
    bool pos = false;
    const int n = p.n();
    for (int i = 0; i < n; i++) {
        int lo = i == 0 ? n - 1 : i - 1, up = i == n - 1 ? 0 : i + 1;
        float dx0 = p.x[i] - p.x[lo], dy0 = p.y[i] - p.y[lo], dx1 = p.x[up] - p.x[i], dy1 = p.y[up] - p.y[i];
        bool np = dx0 * dy1 - dx1 * dy0 >= 0;
        if (i == 0) pos = np; else if (pos != np) return false;
    }
    return true;
}

// b2Polygon::Add: the polygon with the triangle that shares one of its edges joined on (false: no shared edge)
inline bool AddTri(const Poly& p, const Tri& t, Poly& out) {
    int firstP = -1, firstT = -1, secondP = -1, secondT = -1;
    const int n = p.n();
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 3; k++) {
            if (t.x[k] == p.x[i] && t.y[k] == p.y[i]) {
                if (firstP == -1) { firstP = i; firstT = k; } else { secondP = i; secondT = k; }
                break;
            }
        }
    }
    if (firstP == 0 && secondP == n - 1) { firstP = n - 1; secondP = 0; int tmp = firstT; firstT = secondT; secondT = tmp; }
    if (secondP == -1) return false;
    int tip = 0;
    if (tip == firstT || tip == secondT) tip = 1;
    if (tip == firstT || tip == secondT) tip = 2;
    out = Poly();
    for (int i = 0; i < n; i++) {
        out.x.push_back(p.x[i]); out.y.push_back(p.y[i]);
        if (i == firstP) { out.x.push_back(t.x[tip]); out.y.push_back(t.y[tip]); }
    }
    return true;
}

inline void MergeParallelEdges(Poly& p, float tolerance) {
    const int n = p.n();
    if (n <= 3) return;
    std::vector<bool> merge((size_t)n, false);
    int newN = n;
    for (int i = 0; i < n; i++) {
        int lo = i == 0 ? n - 1 : i - 1, up = i == n - 1 ? 0 : i + 1;
        float dx0 = p.x[i] - p.x[lo], dy0 = p.y[i] - p.y[lo], dx1 = p.x[up] - p.x[i], dy1 = p.y[up] - p.y[i];
        float n0 = std::sqrt(dx0 * dx0 + dy0 * dy0), n1 = std::sqrt(dx1 * dx1 + dy1 * dy1);
        if (!(n0 > 0.0f && n1 > 0.0f) && newN > 3) { merge[(size_t)i] = true; newN--; }
        dx0 /= n0; dy0 /= n0; dx1 /= n1; dy1 /= n1;
        float cross = dx0 * dy1 - dx1 * dy0, dot = dx0 * dx1 + dy0 * dy1;
        if (std::fabs(cross) < tolerance && dot > 0 && newN > 3) { merge[(size_t)i] = true; newN--; }
        else merge[(size_t)i] = false;
    }
    if (newN == n || newN == 0) return;
    Poly q;
    for (int i = 0; i < n; i++) {
        if (merge[(size_t)i] || (int)q.x.size() == newN) continue;
        q.x.push_back(p.x[i]); q.y.push_back(p.y[i]);
    }
    p = q;
}

inline int Polygonize(const std::vector<Tri>& tris, int maxPolys, std::vector<Poly>& polys) {
    polys.clear();
    const int nt = (int)tris.size();
    if (nt <= 0) return 0;
    std::vector<char> covered((size_t)nt, 0);
    int count = 0;
    for (;;) {
        int cur = -1;
        for (int i = 0; i < nt; i++) if (!covered[(size_t)i]) { cur = i; break; }
        if (cur == -1) break;
        Poly poly;
        for (int k = 0; k < 3; k++) { poly.x.push_back(tris[(size_t)cur].x[k]); poly.y.push_back(tris[(size_t)cur].y[k]); }
        covered[(size_t)cur] = 1;
        int index = 0;
        for (int i = 0; i < 2 * nt; i++, index++) {
            while (index >= nt) index -= nt;
            if (covered[(size_t)index]) continue;
            Poly np;
            if (!AddTri(poly, tris[(size_t)index], np)) continue;
            if (np.n() > 8) continue;
            if (IsConvex(np)) { poly = np; covered[(size_t)index] = 1; }
        }
        if (count < maxPolys) {
            MergeParallelEdges(poly, 0.0349065848f);
            if (poly.n() >= 3) polys.push_back(poly);
        }
        if (poly.n() >= 3) count++;
    }
    return count;
}

// xy: x,y pairs. out: the pieces as x,y pairs. Returns what DecomposeConvex returns (the piece count, -1 for no triangle).
inline int DecomposeConvex(const std::vector<float>& xy, std::vector<std::vector<float>>& out) {
    out.clear();
    Poly p;
    for (size_t i = 0; i + 1 < xy.size(); i += 2) { p.x.push_back(xy[i]); p.y.push_back(xy[i + 1]); }
    if (p.n() < 3) return 0;
    if (Area(p) > 0.0f) {
        for (int i = 0, j = p.n() - 1; i < j; i++, j--) { std::swap(p.x[(size_t)i], p.x[(size_t)j]); std::swap(p.y[(size_t)i], p.y[(size_t)j]); }
    }
    std::vector<Tri> tris;
    int nt = Triangulate(p.x, p.y, tris);
    if (nt < 1) return -1;
    std::vector<Poly> polys;
    int np = Polygonize(tris, p.n() - 2, polys);
    for (const Poly& q : polys) {
        out.emplace_back();
        for (int i = 0; i < q.n(); i++) { out.back().push_back(q.x[(size_t)i]); out.back().push_back(q.y[(size_t)i]); }
    }
    return np;
}

// The triangles of a polygon for drawing it filled (drawPolygon): the same ear clipping, either winding
inline void FillTriangles(const std::vector<float>& xy, std::vector<float>& tri) {
    tri.clear();
    Poly p;
    for (size_t i = 0; i + 1 < xy.size(); i += 2) { p.x.push_back(xy[i]); p.y.push_back(xy[i + 1]); }
    if (p.n() < 3) return;
    if (Area(p) > 0.0f) {
        for (int i = 0, j = p.n() - 1; i < j; i++, j--) { std::swap(p.x[(size_t)i], p.x[(size_t)j]); std::swap(p.y[(size_t)i], p.y[(size_t)j]); }
    }
    std::vector<Tri> tris;
    Triangulate(p.x, p.y, tris);
    for (const Tri& t : tris) for (int k = 0; k < 3; k++) { tri.push_back(t.x[k]); tri.push_back(t.y[k]); }
}

}  // namespace b2poly
}  // namespace ab
