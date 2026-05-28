#pragma once

#include "NaraGame/math/Vec2.h"

#include <cstddef>
#include <vector>

namespace NaraGame {

// ---------------------------------------------------------------------------
// IsoTerrain — a 2:1 isometric heightfield.
//
// Stores per-CORNER heights on a (W+1) x (H+1) lattice. Each cell's slope is
// derived from its 4 surrounding corners (TOP, RIGHT, BOTTOM, LEFT in screen
// terms), giving one of 16 SC2000-style slope variants. The 1-step cascade
// (EnforceSlopeLimit / AdjustCorner) keeps |neighbour - self| <= 1 across the
// 8-connected corner neighbourhood so every cell stays renderable by the
// slope-bit lookup; raising or lowering a single corner therefore drags its
// neighbours along in a SimCity-like ripple.
//
// The class is purely data + math — it owns no GPU resources and does no
// drawing. The caller (a game) composes its own sprite atlas with the slope
// bits, picks colours per cell, and adds its own origin (or camera) offset to
// the projected positions. Iso projection here is relative to (0, 0).
// ---------------------------------------------------------------------------

struct IsoTerrainConfig {
    int   gridW      = 16;     // cells across
    int   gridH      = 16;     // cells down
    float tileW      = 64.0f;  // diamond bounding-box width in pixels
    float tileH      = 32.0f;  // diamond bounding-box height in pixels (2:1)
    int   stepHeight = 8;      // screen pixels per elevation step
    int   minHeight  = 0;      // lower clamp; negative if the game has water
    int   maxHeight  = 8;      // upper clamp
};

// Result of inspecting a cell's 4 surrounding corners.
//   slopeBits: bit 0 TOP raised, 1 RIGHT, 2 BOTTOM, 3 LEFT (above baseElev).
//   baseElev = minH; index 0..15 over slopeBits is the slope sprite to draw.
struct IsoCellSlope {
    int baseElev;
    int slopeBits;
    int minH;
    int maxH;
};

class IsoTerrain {
public:
    IsoTerrain() = default;
    explicit IsoTerrain(const IsoTerrainConfig& cfg) { Init(cfg); }

    void Init(const IsoTerrainConfig& cfg);

    // ------- Config accessors (hot path — defined inline). -------
    int   Width()       const { return m_cfg.gridW; }
    int   Height()      const { return m_cfg.gridH; }
    int   CornerCols()  const { return m_cfg.gridW + 1; }
    int   CornerRows()  const { return m_cfg.gridH + 1; }
    float TileW()       const { return m_cfg.tileW; }
    float TileH()       const { return m_cfg.tileH; }
    float HalfW()       const { return m_cfg.tileW * 0.5f; }
    float HalfH()       const { return m_cfg.tileH * 0.5f; }
    int   StepHeight()  const { return m_cfg.stepHeight; }
    int   MinHeight()   const { return m_cfg.minHeight; }
    int   MaxHeight()   const { return m_cfg.maxHeight; }
    int   HeightRange() const { return m_cfg.maxHeight - m_cfg.minHeight + 1; }

    // ------- Corner-height data. (i, j) in [0, CornerCols-1] x [0, CornerRows-1]. -------
    int  Corner(int i, int j) const;
    void SetCorner(int i, int j, int h);           // clamps + writes; no cascade
    void Fill(int h = 0);                           // reset every corner

    // ------- Per-cell slope info derived from the 4 corner heights. -------
    IsoCellSlope CellSlope(int cellX, int cellY) const;

    // ------- Iso math (caller adds its own origin / camera offset). -------
    // Tile (cellX, cellY)'s ground-plane center.
    Vec2 TileCenterToScreen(int cellX, int cellY) const;
    // Corner (i, j) at the given elevation. The screen y is shifted up by
    // height * stepHeight (height > 0 = higher, height < 0 = below).
    Vec2 CornerToScreen(int i, int j, int height) const;
    // Inverse of the flat-plane projections (elevation ignored). Returns false
    // and writes -1 to the out params if the point lies outside the grid.
    bool ScreenToCellFlat(Vec2 screen, int& outX, int& outY) const;
    bool ScreenToCornerFlat(Vec2 screen, int& outI, int& outJ) const;

    // ------- Diagonal painter-sort iteration. -------
    int MaxDiagonalSum() const { return (m_cfg.gridW - 1) + (m_cfg.gridH - 1); }
    int DiagonalXMin(int sum) const;
    int DiagonalXMax(int sum) const;

    // ------- Slope-limit cascade (1-step rule across 8-connected corners). -------
    // After modifying a corner directly with SetCorner, call this on its (i, j)
    // to propagate the constraint. AdjustCorner does the combined edit.
    void EnforceSlopeLimit(int startI, int startJ);
    void AdjustCorner(int i, int j, int delta);

private:
    IsoTerrainConfig m_cfg;
    std::vector<int> m_corners;  // row-major; (CornerCols * CornerRows) ints
};

// ---------------------------------------------------------------------------
// Free helpers — useful for iso games beyond IsoTerrain itself.
// ---------------------------------------------------------------------------

// Triangle / quad point-in-polygon for hit-testing slope tiles. The quad
// version splits on the (a, c) diagonal; correct for convex slopes (every
// mask except the rare butterfly masks 5 and 10 produced by saddle corners).
bool PointInTri(Vec2 p, Vec2 a, Vec2 b, Vec2 c);
bool PointInQuad(Vec2 p, Vec2 a, Vec2 b, Vec2 c, Vec2 d);

// Lambert shading for a slope mask: surface normal x light direction
// (upper-left in screen = NW + up in world). Returns a tint multiplier in
// roughly [0.78, 1.10]. Flat tiles (mask 0 or 15) return the same value so
// plateaus read as one consistent brightness.
float SlopeBrightness(int slopeBits);

} // namespace NaraGame
