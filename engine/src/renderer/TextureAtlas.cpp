#include "NaraGame/renderer/TextureAtlas.h"

namespace NaraGame {

void TextureAtlas::Add(const std::string& name, float x, float y, float w, float h) {
    if (m_texture != nullptr)
        m_regions[name] = TextureRegion::FromPixels(m_texture, x, y, w, h);
}

bool TextureAtlas::Has(const std::string& name) const {
    return m_regions.find(name) != m_regions.end();
}

TextureRegion TextureAtlas::Get(const std::string& name) const {
    const auto it = m_regions.find(name);
    return it != m_regions.end() ? it->second : TextureRegion{};
}

} // namespace NaraGame
