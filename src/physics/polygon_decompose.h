// Concave polygons for Box2D, which only takes convex ones: ear clipping into triangles, then the triangles are merged back into as few convex pieces
// as the vertex limit allows (the Hertel-Mehlhorn method). Header only; used by the physics (bodies and shape casts).
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>

namespace ab {
namespace polydecomp {

struct Pt { float x, y; };

inline float Cross(const Pt& o, const Pt& a, const Pt& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

inline float Area2(const std::vector<Pt>& p) {   // twice the signed area, positive when counter-clockwise (y up)
    float s = 0;
    for (size_t i = 0; i < p.size(); i++) { const Pt& a = p[i]; const Pt& b = p[(i + 1) % p.size()]; s += a.x * b.y - b.x * a.y; }
    return s;
}

// counter-clockwise polygon: every turn is left (or straight)
inline bool IsConvex(const std::vector<Pt>& p, float eps = 1e-6f) {
    size_t n = p.size();
    if (n < 3) return false;
    for (size_t i = 0; i < n; i++) if (Cross(p[i], p[(i + 1) % n], p[(i + 2) % n]) < -eps) return false;
    return true;
}

inline bool InTriangle(const Pt& p, const Pt& a, const Pt& b, const Pt& c) {
    return Cross(a, b, p) >= -1e-7f && Cross(b, c, p) >= -1e-7f && Cross(c, a, p) >= -1e-7f;
}

// Splits a simple polygon (any winding, convex or concave) into convex pieces of at most maxVerts vertices, each counter-clockwise.
// Returns false (and a fan of triangles from the first point) when the outline does not triangulate (it crosses itself).
inline bool Decompose(std::vector<Pt> pts, size_t maxVerts, std::vector<std::vector<Pt>>& out) {
    out.clear();
    if (maxVerts < 3) maxVerts = 3;
    // drop repeated points
    std::vector<Pt> p;
    for (const Pt& q : pts) if (p.empty() || std::fabs(q.x - p.back().x) > 1e-6f || std::fabs(q.y - p.back().y) > 1e-6f) p.push_back(q);
    while (p.size() > 1 && std::fabs(p.front().x - p.back().x) < 1e-6f && std::fabs(p.front().y - p.back().y) < 1e-6f) p.pop_back();
    if (p.size() < 3) return false;
    if (Area2(p) < 0) std::reverse(p.begin(), p.end());
    if (IsConvex(p) && p.size() <= maxVerts) { out.push_back(p); return true; }

    // ear clipping
    std::vector<int> idx;
    for (size_t i = 0; i < p.size(); i++) idx.push_back((int)i);
    std::vector<std::vector<int>> tris;
    bool ok = true;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 100000) {
        size_t n = idx.size();
        bool clipped = false;
        for (size_t i = 0; i < n && !clipped; i++) {
            int ia = idx[(i + n - 1) % n], ib = idx[i], ic = idx[(i + 1) % n];
            const Pt &a = p[ia], &b = p[ib], &c = p[ic];
            float cr = Cross(a, b, c);
            if (cr <= 1e-7f) continue;                                    // reflex or straight: not an ear
            bool empty = true;
            for (size_t j = 0; j < n && empty; j++) {
                int k = idx[j];
                if (k == ia || k == ib || k == ic) continue;
                const Pt& q = p[k];
                if ((std::fabs(q.x - a.x) < 1e-6f && std::fabs(q.y - a.y) < 1e-6f) || (std::fabs(q.x - c.x) < 1e-6f && std::fabs(q.y - c.y) < 1e-6f) || (std::fabs(q.x - b.x) < 1e-6f && std::fabs(q.y - b.y) < 1e-6f)) continue;
                if (InTriangle(q, a, b, c)) empty = false;
            }
            if (!empty) continue;
            tris.push_back({ia, ib, ic});
            idx.erase(idx.begin() + i);
            clipped = true;
        }
        if (!clipped) {
            // no ear: a straight vertex can go (it only adds a point to a line), otherwise the outline is not simple
            bool removed = false;
            for (size_t i = 0; i < idx.size() && !removed; i++) {
                size_t n2 = idx.size();
                if (std::fabs(Cross(p[idx[(i + n2 - 1) % n2]], p[idx[i]], p[idx[(i + 1) % n2]])) <= 1e-7f) { idx.erase(idx.begin() + i); removed = true; }
            }
            if (!removed) { ok = false; break; }
        }
    }
    if (idx.size() == 3 && Cross(p[idx[0]], p[idx[1]], p[idx[2]]) > 1e-7f) tris.push_back({idx[0], idx[1], idx[2]});
    if (!ok || tris.empty()) {
        out.clear();
        for (size_t i = 1; i + 1 < p.size(); i++) {
            std::vector<Pt> t = {p[0], p[i], p[i + 1]};
            if (Area2(t) > 1e-7f) out.push_back(t);
        }
        return false;
    }

    // merge neighbours while the union stays convex and within the vertex limit
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t a = 0; a < tris.size() && !merged; a++) {
            for (size_t b = a + 1; b < tris.size() && !merged; b++) {
                std::vector<int>& A = tris[a];
                std::vector<int>& B = tris[b];
                if (A.size() + B.size() - 2 > maxVerts) continue;
                for (size_t i = 0; i < A.size() && !merged; i++) {
                    int a0 = A[i], a1 = A[(i + 1) % A.size()];
                    for (size_t j = 0; j < B.size() && !merged; j++) {
                        if (B[j] != a1 || B[(j + 1) % B.size()] != a0) continue;       // the shared edge runs the other way round in B
                        std::vector<int> m;
                        for (size_t k = 1; k <= A.size(); k++) m.push_back(A[(i + k) % A.size()]);   // a1 ... a0
                        for (size_t k = 2; k < B.size(); k++) m.push_back(B[(j + k) % B.size()]);    // the rest of B
                        std::vector<Pt> mp;
                        for (int v : m) mp.push_back(p[v]);
                        if (!IsConvex(mp)) continue;
                        tris[a] = m;
                        tris.erase(tris.begin() + b);
                        merged = true;
                    }
                }
            }
        }
    }
    for (const auto& t : tris) {
        std::vector<Pt> piece;
        for (int v : t) piece.push_back(p[v]);
        out.push_back(piece);
    }
    return true;
}

}  // namespace polydecomp
}  // namespace ab
