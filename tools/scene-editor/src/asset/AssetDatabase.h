#pragma once

#include "AssetGuid.h"

#include <NaraGame/assets/MiniJson.h>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace SceneEditor {

class Project;

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

    const std::string& AssetsRoot() const { return m_assetsRoot; }

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
};

} // namespace SceneEditor
