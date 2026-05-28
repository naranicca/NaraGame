#include "NaraGame/renderer/IsoTerrain.h"

#include "NaraGame/math/MathUtil.h"

#include <cmath>
#include <utility>

namespace NaraGame {

void IsoTerrain::Init(const IsoTerrainConfig& cfg) {
    m_cfg = cfg;
    m_corners.assign(static_cast<std::size_t>(CornerCols()) * CornerRows(), 0);
}

int IsoTerrain::Corner(int i, int j) const {
    return m_corners[static_cast<std::size_t>(j) * CornerCols() + i];
}

void IsoTerrain::SetCorner(int i, int j, int h) {
    m_corners[static_cast<std::size_t>(j) * CornerCols() + i] =
        Clamp(h, m_cfg.minHeight, m_cfg.maxHeight);
}

void IsoTerrain::Fill(int h) {
    const int clamped = Clamp(h, m_cfg.minHeight, m_cfg.maxHeight);
    for (int& v : m_corners) v = clamped;
}

IsoCellSlope IsoTerrain::CellSlope(int cellX, int cellY) const {
    const int hT = Corner(cellX,     cellY    );
    const int hR = Corner(cellX + 1, cellY    );
    const int hB = Corner(cellX + 1, cellY + 1);
    const int hL = Corner(cellX,     cellY + 1);
    IsoCellSlope s;
    s.minH = Min(Min(hT, hR), Min(hB, hL));
    s.maxH = Max(Max(hT, hR), Max(hB, hL));
    s.baseElev = s.minH;
    s.slopeBits = ((hT > s.baseElev) ? 1 : 0)
                | ((hR > s.baseElev) ? 2 : 0)
                | ((hB > s.baseElev) ? 4 : 0)
                | ((hL > s.baseElev) ? 8 : 0);
    return s;
}

Vec2 IsoTerrain::TileCenterToScreen(int cellX, int cellY) const {
    return { (cellX - cellY) * HalfW(), (cellX + cellY) * HalfH() };
}

Vec2 IsoTerrain::CornerToScreen(int i, int j, int height) const {
    // Corner (i, j) sits at tile-space (i - 0.5, j - 0.5), so its flat-plane
    // screen y trails the cell-center map by halfH. The height term shifts it
    // upward in screen by stepHeight per elevation step.
    return {
        (i - j) * HalfW(),
        (i + j - 1) * HalfH() - static_cast<float>(height * m_cfg.stepHeight)
    };
}

bool IsoTerrain::ScreenToCellFlat(Vec2 screen, int& outX, int& outY) const {
    const float dx = screen.x / HalfW();
    const float dy = screen.y / HalfH();
    outX = static_cast<int>(std::floor((dx + dy) * 0.5f));
    outY = static_cast<int>(std::floor((dy - dx) * 0.5f));
    if (outX < 0 || outX >= m_cfg.gridW || outY < 0 || outY >= m_cfg.gridH) {
        outX = outY = -1;
        return false;
    }
    return true;
}

bool IsoTerrain::ScreenToCornerFlat(Vec2 screen, int& outI, int& outJ) const {
    const float dx = screen.x / HalfW();
    const float dy = screen.y / HalfH();
    const float fi = (dx + dy + 1.0f) * 0.5f;
    const float fj = (dy - dx + 1.0f) * 0.5f;
    if (fi < -0.5f || fi > m_cfg.gridW + 0.5f ||
        fj < -0.5f || fj > m_cfg.gridH + 0.5f) {
        outI = outJ = -1;
        return false;
    }
    outI = static_cast<int>(std::lround(fi));
    outJ = static_cast<int>(std::lround(fj));
    outI = Clamp(outI, 0, m_cfg.gridW);
    outJ = Clamp(outJ, 0, m_cfg.gridH);
    return true;
}

int IsoTerrain::DiagonalXMin(int sum) const {
    return Max(0, sum - (m_cfg.gridH - 1));
}

int IsoTerrain::DiagonalXMax(int sum) const {
    return Min(m_cfg.gridW - 1, sum);
}

void IsoTerrain::EnforceSlopeLimit(int startI, int startJ) {
    // 8-connected BFS. Any neighbour outside [self-1, self+1] gets dragged
    // into range; if that changed it, its own neighbours are re-checked.
    // Symmetric across raise/lower direction.
    std::vector<std::pair<int, int>> queue;
    queue.emplace_back(startI, startJ);
    while (!queue.empty()) {
        const auto [ci, cj] = queue.back();
        queue.pop_back();
        const int h = Corner(ci, cj);
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di == 0 && dj == 0) continue;
                const int ni = ci + di;
                const int nj = cj + dj;
                if (ni < 0 || nj < 0 || ni >= CornerCols() || nj >= CornerRows())
                    continue;
                const int nh = Corner(ni, nj);
                int target = nh;
                if (nh < h - 1)      target = h - 1;
                else if (nh > h + 1) target = h + 1;
                target = Clamp(target, m_cfg.minHeight, m_cfg.maxHeight);
                if (target != nh) {
                    m_corners[static_cast<std::size_t>(nj) * CornerCols() + ni] = target;
                    queue.emplace_back(ni, nj);
                }
            }
        }
    }
}

void IsoTerrain::AdjustCorner(int i, int j, int delta) {
    const int before = Corner(i, j);
    const int after  = Clamp(before + delta, m_cfg.minHeight, m_cfg.maxHeight);
    if (after == before) return;
    m_corners[static_cast<std::size_t>(j) * CornerCols() + i] = after;
    EnforceSlopeLimit(i, j);
}

// ---------- Free helpers ----------

bool PointInTri(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    // Consistent cross-product sign test — accepts CW and CCW windings.
    const float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    const float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    const float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNeg && hasPos);
}

bool PointInQuad(Vec2 p, Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    return PointInTri(p, a, b, c) || PointInTri(p, a, c, d);
}

float SlopeBrightness(int slopeBits) {
    // Surface normal of the slope from the 4 corner heights via
    // (north-vec) x (east-vec), then dotted with an upper-left light.
    const float hT = (slopeBits & 1) ? 1.0f : 0.0f;
    const float hR = (slopeBits & 2) ? 1.0f : 0.0f;
    const float hB = (slopeBits & 4) ? 1.0f : 0.0f;
    const float hL = (slopeBits & 8) ? 1.0f : 0.0f;
    const float nx = -2.0f * (hR - hL);
    const float ny =  2.0f * (hT - hB);
    const float nz =  4.0f;
    const float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
    constexpr float lx = -1.0f, ly = -1.0f, lz = 1.0f;
    constexpr float lLen = 1.7320508f; // sqrt(3)
    const float dot = (nx * lx + ny * ly + nz * lz) / (nLen * lLen);
    return 0.78f + 0.32f * Max(0.0f, dot);
}

} // namespace NaraGame
