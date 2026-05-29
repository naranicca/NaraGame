#include "NaraGame/assets/MSDFFontLoader.h"

#include "NaraGame/assets/TextureLoader.h"
#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

// ============================================================================
// Tiny JSON parser
// ----------------------------------------------------------------------------
// Just enough to consume msdf-atlas-gen output: objects, arrays, strings,
// numbers. No escape-sequence decoding (the schema never carries escapes),
// no Unicode in strings (ASCII keys only), no error recovery (a malformed
// JSON returns a null Value and the caller surfaces the failure). ~120 lines
// avoids dragging in nlohmann/json for one file.
// ============================================================================

namespace {

struct JsonValue;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;
using JsonArray  = std::vector<JsonValue>;

struct JsonValue {
    enum Type { Null, Number, String, Object, Array };
    Type type = Null;
    double number = 0.0;
    std::string str;
    JsonObject obj;
    JsonArray  arr;

    bool IsObject() const { return type == Object; }
    bool IsArray()  const { return type == Array; }
    bool IsNumber() const { return type == Number; }
    bool IsString() const { return type == String; }

    double AsNumber(double fallback = 0.0) const { return IsNumber() ? number : fallback; }
    const JsonValue* Get(const std::string& key) const {
        if (!IsObject()) return nullptr;
        for (const auto& [k, v] : obj) if (k == key) return &v;
        return nullptr;
    }
    double GetNumber(const std::string& key, double fallback = 0.0) const {
        const JsonValue* v = Get(key);
        return v ? v->AsNumber(fallback) : fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : m_src(s) {}

    JsonValue Parse() {
        SkipWs();
        JsonValue v = ParseValue();
        SkipWs();
        return v;
    }

private:
    const std::string& m_src;
    size_t m_i = 0;

    void SkipWs() {
        while (m_i < m_src.size() && (m_src[m_i] == ' ' || m_src[m_i] == '\t'
                                   || m_src[m_i] == '\n' || m_src[m_i] == '\r')) ++m_i;
    }
    char Peek() const { return m_i < m_src.size() ? m_src[m_i] : '\0'; }

    JsonValue ParseValue() {
        SkipWs();
        const char c = Peek();
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == '"') return ParseString();
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
        // null / true / false: skip identifier, leave value Null.
        while (m_i < m_src.size() && ((m_src[m_i] >= 'a' && m_src[m_i] <= 'z'))) ++m_i;
        return JsonValue{};
    }

    JsonValue ParseObject() {
        JsonValue v; v.type = JsonValue::Object;
        ++m_i; // '{'
        SkipWs();
        while (Peek() != '}' && m_i < m_src.size()) {
            SkipWs();
            JsonValue key = ParseString();
            SkipWs();
            if (Peek() == ':') ++m_i;
            JsonValue val = ParseValue();
            v.obj.emplace_back(std::move(key.str), std::move(val));
            SkipWs();
            if (Peek() == ',') ++m_i;
            SkipWs();
        }
        if (Peek() == '}') ++m_i;
        return v;
    }

    JsonValue ParseArray() {
        JsonValue v; v.type = JsonValue::Array;
        ++m_i; // '['
        SkipWs();
        while (Peek() != ']' && m_i < m_src.size()) {
            v.arr.push_back(ParseValue());
            SkipWs();
            if (Peek() == ',') ++m_i;
            SkipWs();
        }
        if (Peek() == ']') ++m_i;
        return v;
    }

    JsonValue ParseString() {
        JsonValue v; v.type = JsonValue::String;
        if (Peek() == '"') ++m_i;
        while (m_i < m_src.size() && m_src[m_i] != '"') {
            v.str.push_back(m_src[m_i]);
            ++m_i;
        }
        if (Peek() == '"') ++m_i;
        return v;
    }

    JsonValue ParseNumber() {
        JsonValue v; v.type = JsonValue::Number;
        size_t start = m_i;
        if (Peek() == '-') ++m_i;
        while (m_i < m_src.size() && ((m_src[m_i] >= '0' && m_src[m_i] <= '9')
                                    || m_src[m_i] == '.' || m_src[m_i] == 'e'
                                    || m_src[m_i] == 'E' || m_src[m_i] == '+'
                                    || m_src[m_i] == '-')) ++m_i;
        v.number = std::strtod(m_src.c_str() + start, nullptr);
        return v;
    }
};

} // namespace (anonymous)

// ============================================================================
// LoadMSDFFont
// ============================================================================

namespace NaraGame {

static std::string ReplaceExt(const std::string& path, const std::string& newExt) {
    const size_t dot = path.find_last_of('.');
    return (dot == std::string::npos) ? path + newExt : path.substr(0, dot) + newExt;
}

MSDFFont LoadMSDFFont(RHIDevice& device, const std::string& jsonPath) {
    MSDFFont font;

    std::ifstream in(jsonPath);
    if (!in) {
        NARAGAME_ERROR("LoadMSDFFont: cannot open '%s'", jsonPath.c_str());
        return font;
    }
    std::stringstream ss; ss << in.rdbuf();
    const std::string raw = ss.str();

    JsonParser parser(raw);
    const JsonValue root = parser.Parse();
    if (!root.IsObject()) {
        NARAGAME_ERROR("LoadMSDFFont: '%s' parse failed", jsonPath.c_str());
        return font;
    }

    // atlas: { type, distanceRange, size, width, height, yOrigin }
    const JsonValue* atlas = root.Get("atlas");
    if (!atlas || !atlas->IsObject()) {
        NARAGAME_ERROR("LoadMSDFFont: missing 'atlas' object");
        return font;
    }
    const uint32_t atlasW = static_cast<uint32_t>(atlas->GetNumber("width"));
    const uint32_t atlasH = static_cast<uint32_t>(atlas->GetNumber("height"));
    const float    pxRange = static_cast<float>(atlas->GetNumber("distanceRange", 4.0));

    // metrics: { emSize, lineHeight, ascender, descender, ... }
    const JsonValue* metrics = root.Get("metrics");
    if (!metrics || !metrics->IsObject()) {
        NARAGAME_ERROR("LoadMSDFFont: missing 'metrics' object");
        return font;
    }
    const float lineHeight = static_cast<float>(metrics->GetNumber("lineHeight", 1.2));
    const float ascender   = static_cast<float>(metrics->GetNumber("ascender",   -1.0));
    const float descender  = static_cast<float>(metrics->GetNumber("descender",   0.2));

    font.SetMetrics(lineHeight, ascender, descender);
    font.SetPxRange(pxRange);

    // glyphs: [ { unicode, advance, planeBounds{l,t,r,b}, atlasBounds{l,t,r,b} }, ... ]
    const JsonValue* glyphs = root.Get("glyphs");
    if (!glyphs || !glyphs->IsArray()) {
        NARAGAME_ERROR("LoadMSDFFont: missing 'glyphs' array");
        return font;
    }
    for (const JsonValue& gv : glyphs->arr) {
        if (!gv.IsObject()) continue;
        MSDFGlyph g;
        const uint32_t cp = static_cast<uint32_t>(gv.GetNumber("unicode"));
        g.advance = static_cast<float>(gv.GetNumber("advance"));

        // Whitespace glyphs (e.g. space) have no plane/atlas bounds -- still
        // record them so their advance contributes to layout.
        if (const JsonValue* pb = gv.Get("planeBounds")) {
            g.planeLeft   = static_cast<float>(pb->GetNumber("left"));
            g.planeTop    = static_cast<float>(pb->GetNumber("top"));
            g.planeRight  = static_cast<float>(pb->GetNumber("right"));
            g.planeBottom = static_cast<float>(pb->GetNumber("bottom"));
        }
        if (const JsonValue* ab = gv.Get("atlasBounds")) {
            g.atlasLeft   = static_cast<float>(ab->GetNumber("left"));
            g.atlasTop    = static_cast<float>(ab->GetNumber("top"));
            g.atlasRight  = static_cast<float>(ab->GetNumber("right"));
            g.atlasBottom = static_cast<float>(ab->GetNumber("bottom"));
        }
        font.AddGlyph(cp, g);
    }

    // Derive the PNG path from the JSON path by extension swap. (msdf-atlas-gen
    // doesn't put the image name in the JSON, but we control naming via the
    // build script so the convention holds.)
    const std::string pngPath = ReplaceExt(jsonPath, ".png");
    auto texture = LoadTexture(device, pngPath, TextureFilter::Linear);
    if (!texture) {
        NARAGAME_ERROR("LoadMSDFFont: failed to load atlas '%s'", pngPath.c_str());
        return font;
    }
    font.SetTexture(std::move(texture), atlasW, atlasH);

    NARAGAME_INFO("Loaded MSDF font '%s' (%u glyphs, atlas %ux%u, pxRange %.1f)",
                  jsonPath.c_str(),
                  static_cast<unsigned>(glyphs->arr.size()),
                  atlasW, atlasH, pxRange);
    return font;
}

} // namespace NaraGame
