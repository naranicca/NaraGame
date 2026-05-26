#include "NaraGame/assets/TileLevel.h"

#include "NaraGame/core/Log.h"

#include <fstream>
#include <string>

namespace NaraGame {

TileLevel LoadTileLevel(const std::string& path, float tileWidth, float tileHeight,
                        const SpriteSheet& tileset,
                        const std::unordered_map<char, int>& legend) {
    TileLevel level;

    std::ifstream file(path);
    if (!file) {
        NARAGAME_ERROR("LoadTileLevel: cannot open '%s'", path.c_str());
        return level;
    }

    std::vector<std::string> lines;
    std::string line;
    size_t width = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') // tolerate CRLF
            line.pop_back();
        width = (line.size() > width) ? line.size() : width;
        lines.push_back(line);
    }

    if (lines.empty() || width == 0) {
        NARAGAME_ERROR("LoadTileLevel: '%s' is empty", path.c_str());
        return level;
    }

    const int w = static_cast<int>(width);
    const int h = static_cast<int>(lines.size());
    level.map = Tilemap(w, h, tileWidth, tileHeight);
    level.map.SetTileset(tileset);

    for (int y = 0; y < h; ++y) {
        const std::string& row = lines[static_cast<size_t>(y)];
        for (int x = 0; x < static_cast<int>(row.size()); ++x) {
            const char c = row[static_cast<size_t>(x)];
            if (c == ' ' || c == '.')
                continue; // empty space
            const auto it = legend.find(c);
            if (it != legend.end())
                level.map.SetTile(x, y, it->second);
            else
                level.objects.push_back(LevelObject{c, x, y}); // entity marker
        }
    }

    level.valid = true;
    NARAGAME_INFO("Loaded level '%s' (%dx%d, %zu objects)", path.c_str(), w, h,
              level.objects.size());
    return level;
}

} // namespace NaraGame
