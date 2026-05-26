#pragma once

#include "nara/renderer/TextureRegion.h"

#include <string>
#include <unordered_map>

namespace nara {

class RHITexture;

// A texture with named sub-regions — the typical output of a sprite packer.
// Register regions by pixel rectangle, then look them up by name at draw time.
class TextureAtlas {
public:
    TextureAtlas() = default;
    explicit TextureAtlas(RHITexture* texture) : m_texture(texture) {}

    void SetTexture(RHITexture* texture) { m_texture = texture; }
    RHITexture* Texture() const { return m_texture; }

    void Add(const std::string& name, float x, float y, float w, float h);

    bool Has(const std::string& name) const;
    TextureRegion Get(const std::string& name) const; // invalid region if unknown

private:
    RHITexture* m_texture = nullptr;
    std::unordered_map<std::string, TextureRegion> m_regions;
};

} // namespace nara
