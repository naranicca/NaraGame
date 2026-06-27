#pragma once

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/math/Vec2.h>
#include <NaraGame/renderer/rhi/RHITypes.h>

#include <string>

namespace SceneEditor {

// Subset of the spec's texture / sprite import settings that the editor
// actually consumes today. Sprite-mode slicing (Multiple / Sliced /
// Tiled) and per-sub-sprite metadata land in M3 with the Sprite
// Editor; what's here is just the bits the SpriteRenderer needs to
// compute its world-space quad correctly.
struct TextureImportSettings {
    // World units per texture pixel: world width = textureWidth / ppu.
    // Lower means the sprite covers more world space (chunkier).
    float pixelsPerUnit = 100.0f;

    enum class PivotMode { Center, TopLeft, TopRight, BottomLeft, BottomRight, Custom };
    PivotMode      pivotMode   = PivotMode::Center;
    NaraGame::Vec2 customPivot = {0.5f, 0.5f};  // 0..1; (0,0) = bottom-left, (1,1) = top-right

    enum class Filter { Linear, Point };
    Filter         filterMode = Filter::Linear;

    // Held for the spec; not yet honoured by the sampler the editor
    // uses for sprite previews. Real wrap support arrives with the
    // Tiled draw mode in M3.
    enum class Wrap { Clamp, Repeat, Mirror };
    Wrap           wrapMode = Wrap::Clamp;

    bool alphaIsTransparency = true;

    // Decode the named pivot anchor into a (0..1) coordinate pair.
    NaraGame::Vec2 ResolvedPivot() const;

    // Maps the filter enum to the engine's RHI filter so the texture
    // cache uploads with the right sampler.
    NaraGame::TextureFilter ToRHIFilter() const {
        return filterMode == Filter::Point
            ? NaraGame::TextureFilter::Point
            : NaraGame::TextureFilter::Linear;
    }

    void FromJson(const NaraGame::MiniJson::Value& obj);
    NaraGame::MiniJson::Value ToJson() const;
};

const char* ToString(TextureImportSettings::PivotMode m);
const char* ToString(TextureImportSettings::Filter f);
const char* ToString(TextureImportSettings::Wrap w);

} // namespace SceneEditor
