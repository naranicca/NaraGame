#pragma once

#include "AssetGuid.h"

#include <NaraGame/assets/MiniJson.h>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace NaraGame {
class RHIDevice;
class RHITexture;
}

namespace SceneEditor {

class Project;
class TilesetAsset;
class AutotileAsset;

// One catalogued asset. The same row drives the Project panel display,
// inspector lookups, and the GUID -> data resolution that scene /
// component references go through.
struct AssetEntry {
    AssetGuid               guid;
    std::string             relPath;     // e.g. "Scenes/Sample.nscene"
    std::string             typeName;    // "Texture" / "Scene" / "Script" / ...
    NaraGame::MiniJson::Value importSettings;  // type-specific JSON object
};

// Tree node for the Project panel UI. Folders own a list of children;
// files point back into AssetDatabase::Entries() via `entryIndex`.
struct AssetTreeNode {
    std::string                 name;        // last path component
    bool                        isFolder = false;
    std::vector<std::unique_ptr<AssetTreeNode>> children;  // folders only
    size_t                      entryIndex = static_cast<size_t>(-1);  // valid for files
};

// In-memory catalogue of every asset under <project>/Assets. Each asset
// gets a `.meta` sidecar next to it that stores its GUID + import
// settings; missing .meta files are generated on scan so an external
// drag-drop into the folder still picks up a stable id.
class AssetDatabase {
public:
    void Open(const Project& project);
    void Close();
    bool IsOpen() const { return !m_assetsRoot.empty(); }

    // Re-walk Assets/ and rebuild the tree + lookup tables. Cheap to
    // call manually; future commits will route this to a file watcher.
    void Rescan();

    const std::vector<AssetEntry>& Entries() const { return m_entries; }
    const AssetTreeNode*           RootNode() const { return m_root.get(); }

    const AssetEntry* FindByGuid(const AssetGuid& guid) const;
    const AssetEntry* FindByPath(const std::string& relPath) const;
    // Non-const overload so the inspector can mutate importSettings
    // directly without going through a copy + replace dance.
    AssetEntry* FindByGuidMutable(const AssetGuid& guid);

    const std::string& AssetsRoot() const { return m_assetsRoot; }

    // Write the entry's current importSettings (and type / guid) back
    // to its `.meta` sidecar. Returns false on I/O failure; the
    // in-memory entry is unchanged either way.
    bool SaveMeta(const AssetGuid& guid) const;

    // Drop the cached RHITexture for `guid`. Next GetTexture call
    // re-uploads from disk -- used after the user flips filterMode in
    // the inspector so the new sampler kicks in immediately.
    void EvictTexture(const AssetGuid& guid);

    // Lazy texture cache. First call decodes the file from disk via the
    // engine's PNG/JPG path and uploads it as an RHI texture; later
    // calls reuse the cached pointer. Returns nullptr if the guid is
    // unknown, classifies as a non-texture, or fails to decode.
    NaraGame::RHITexture* GetTexture(NaraGame::RHIDevice& device,
                                     const AssetGuid& guid);

    // Lazy tileset cache. First call reads the .ntileset file from
    // disk; later calls reuse the cached pointer. Returns nullptr if
    // the guid is unknown or classifies as a non-tileset.
    TilesetAsset* GetTileset(const AssetGuid& guid);
    // Persist the in-memory tileset back to its .ntileset path.
    bool SaveTileset(const AssetGuid& guid);
    // Drop a tileset from the cache (forces re-read on next access).
    void EvictTileset(const AssetGuid& guid);

    // Autotile cache, same shape as Tileset.
    AutotileAsset* GetAutotile(const AssetGuid& guid);
    bool           SaveAutotile(const AssetGuid& guid);
    void           EvictAutotile(const AssetGuid& guid);

private:
    void ScanDir(const std::string& dirAbs,
                 const std::string& relPrefix,
                 AssetTreeNode* parentNode);
    bool LoadOrCreateMeta(const std::string& absAssetPath,
                          const std::string& relAssetPath,
                          AssetEntry& outEntry);
    bool ReadMeta(const std::string& metaAbsPath, AssetEntry& out) const;
    bool WriteMeta(const std::string& absAssetPath,
                   const AssetEntry& entry) const;

    static std::string ClassifyByExtension(const std::string& relPath);
    static std::string MetaPathFor(const std::string& absAssetPath);

    std::string                                  m_assetsRoot;
    std::vector<AssetEntry>                      m_entries;
    std::unordered_map<std::string, size_t>      m_byPath;
    std::unordered_map<AssetGuid, size_t>        m_byGuid;
    std::unique_ptr<AssetTreeNode>               m_root;

    // Owned GPU textures, keyed by asset guid. Cleared on Close so a
    // project swap releases the GPU memory; a follow-up commit will
    // route this through a proper hot-reload watch.
    std::unordered_map<AssetGuid,
                       std::unique_ptr<NaraGame::RHITexture>> m_textureCache;
    // Loaded .ntileset files, keyed by asset guid. Lazy + cleared on
    // Close like the texture cache.
    std::unordered_map<AssetGuid,
                       std::unique_ptr<TilesetAsset>>          m_tilesetCache;
    std::unordered_map<AssetGuid,
                       std::unique_ptr<AutotileAsset>>         m_autotileCache;
};

} // namespace SceneEditor
