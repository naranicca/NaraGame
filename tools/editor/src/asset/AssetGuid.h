#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace SceneEditor {

// 128-bit asset identifier. Stable across renames / moves -- scenes and
// components reference assets by guid, not by path, so the user can
// rearrange the project tree without breaking any references. The wire
// form is the canonical hyphenated UUID (8-4-4-4-12 hex), generated via
// the OS UUID source (CoCreateGuid on Windows).
struct AssetGuid {
    std::array<uint8_t, 16> bytes{};

    bool IsValid() const {
        for (uint8_t b : bytes) if (b != 0) return true;
        return false;
    }

    std::string ToString() const;                  // 36 chars, lowercase
    static AssetGuid FromString(const std::string& s);  // invalid on bad input
    static AssetGuid Generate();                    // fresh OS-random GUID

    bool operator==(const AssetGuid& o) const { return bytes == o.bytes; }
    bool operator!=(const AssetGuid& o) const { return bytes != o.bytes; }
};

} // namespace SceneEditor

namespace std {

template <>
struct hash<SceneEditor::AssetGuid> {
    size_t operator()(const SceneEditor::AssetGuid& g) const noexcept {
        // FNV-1a over the 16 bytes -- adequate spread for a small editor
        // database; not crypto.
        size_t h = 1469598103934665603ull;
        for (uint8_t b : g.bytes) {
            h ^= b;
            h *= 1099511628211ull;
        }
        return h;
    }
};

} // namespace std
