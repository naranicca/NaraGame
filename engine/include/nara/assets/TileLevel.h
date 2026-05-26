#pragma once

#include "nara/renderer/SpriteSheet.h"
#include "nara/renderer/Tilemap.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace nara {

// A non-tile marker found in a level file (player spawn, enemy, ...), reported
// at its tile coordinates for the game to interpret.
struct LevelObject {
    char symbol = 0;
    int tileX = 0;
    int tileY = 0;
};

struct TileLevel {
    Tilemap map;
    std::vector<LevelObject> objects;
    bool valid = false;
};

// Load a text-grid level: one character per tile, top row first. Characters in
// `legend` become tiles; spaces and '.' are empty; any other character is left
// empty and reported as a LevelObject. Map width is the longest line.
TileLevel LoadTileLevel(const std::string& path, float tileWidth, float tileHeight,
                        const SpriteSheet& tileset,
                        const std::unordered_map<char, int>& legend);

} // namespace nara
