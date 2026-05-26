#include "nara/assets/BMFont.h"

#include "nara/assets/TextureLoader.h"
#include "nara/core/Log.h"
#include "nara/renderer/rhi/RHIResources.h" // complete RHITexture for unique_ptr

#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace nara {
namespace {

// Split a BMFont line into "key=value" tokens, respecting quoted values.
std::unordered_map<std::string, std::string> ParseFields(const std::string& line) {
    std::unordered_map<std::string, std::string> fields;
    std::vector<std::string> tokens;
    std::string token;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if ((c == ' ' || c == '\t') && !inQuotes) {
            if (!token.empty()) { tokens.push_back(token); token.clear(); }
        } else {
            token += c;
        }
    }
    if (!token.empty())
        tokens.push_back(token);

    for (const std::string& t : tokens) {
        const size_t eq = t.find('=');
        if (eq != std::string::npos)
            fields[t.substr(0, eq)] = t.substr(eq + 1);
    }
    return fields;
}

float FieldF(const std::unordered_map<std::string, std::string>& f, const std::string& key,
             float def = 0.0f) {
    const auto it = f.find(key);
    return it != f.end() ? static_cast<float>(std::atof(it->second.c_str())) : def;
}

int FieldI(const std::unordered_map<std::string, std::string>& f, const std::string& key,
           int def = 0) {
    const auto it = f.find(key);
    return it != f.end() ? std::atoi(it->second.c_str()) : def;
}

std::string DirOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

} // namespace

Font LoadBMFont(RHIDevice& device, const std::string& fntPath) {
    Font font;

    std::ifstream file(fntPath);
    if (!file) {
        NARA_ERROR("LoadBMFont: cannot open '%s'", fntPath.c_str());
        return font;
    }

    const std::string dir = DirOf(fntPath);
    std::string pageFile;
    std::string line;
    int glyphCount = 0;

    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        const auto fields = ParseFields(line);

        if (line.rfind("common", 0) == 0) {
            font.SetLineHeight(FieldF(fields, "lineHeight"));
            font.SetBase(FieldF(fields, "base"));
        } else if (line.rfind("page", 0) == 0) {
            const auto it = fields.find("file");
            if (it != fields.end())
                pageFile = it->second;
        } else if (line.rfind("char ", 0) == 0 || line.rfind("char\t", 0) == 0) {
            FontGlyph g;
            g.x = FieldF(fields, "x");
            g.y = FieldF(fields, "y");
            g.width = FieldF(fields, "width");
            g.height = FieldF(fields, "height");
            g.xoffset = FieldF(fields, "xoffset");
            g.yoffset = FieldF(fields, "yoffset");
            g.xadvance = FieldF(fields, "xadvance");
            font.AddGlyph(static_cast<uint32_t>(FieldI(fields, "id")), g);
            ++glyphCount;
        }
    }

    if (pageFile.empty()) {
        NARA_ERROR("LoadBMFont: '%s' has no page texture", fntPath.c_str());
        return font;
    }

    auto texture = LoadTexture(device, dir + pageFile);
    if (!texture) {
        NARA_ERROR("LoadBMFont: failed to load page '%s'", (dir + pageFile).c_str());
        return font;
    }
    font.SetTexture(std::move(texture));
    NARA_INFO("Loaded BMFont '%s' (%d glyphs, lineHeight %.0f)", fntPath.c_str(), glyphCount,
              font.LineHeight());
    return font;
}

} // namespace nara
