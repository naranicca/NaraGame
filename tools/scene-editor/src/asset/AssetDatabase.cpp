#include "AssetDatabase.h"

#include "../project/Project.h"

#include <NaraGame/assets/Image.h>
#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>
#include <NaraGame/renderer/rhi/RHIResources.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace SceneEditor {

using NaraGame::MiniJson::Value;

namespace {

constexpr const char* kMetaSuffix = ".meta";

bool ReadFileText(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool WriteFileText(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << content;
    return out.good();
}

std::string LowerExt(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    for (char& c : ext)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool EndsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n &&
           std::strcmp(s.c_str() + s.size() - n, suffix) == 0;
}

std::string ToGeneric(const fs::path& p) {
    return p.generic_string();
}

} // namespace

void AssetDatabase::Open(const Project& project) {
    m_assetsRoot = project.AssetsDir();
    Rescan();
}

void AssetDatabase::Close() {
    m_assetsRoot.clear();
    m_entries.clear();
    m_byPath.clear();
    m_byGuid.clear();
    m_root.reset();
    m_textureCache.clear();
}

NaraGame::RHITexture* AssetDatabase::GetTexture(NaraGame::RHIDevice& device,
                                                const AssetGuid& guid) {
    if (!guid.IsValid()) return nullptr;
    if (auto it = m_textureCache.find(guid); it != m_textureCache.end()) {
        return it->second.get();
    }
    const AssetEntry* entry = FindByGuid(guid);
    if (!entry || entry->typeName != "Texture") return nullptr;
    const std::string abs = m_assetsRoot + "/" + entry->relPath;
    NaraGame::Image img = NaraGame::LoadImageFile(abs);
    if (!img.Valid()) {
        NARAGAME_WARN("AssetDatabase: '%s' failed to decode as a texture",
                      entry->relPath.c_str());
        // Park an empty entry so we don't retry every frame.
        m_textureCache.emplace(guid, nullptr);
        return nullptr;
    }
    auto tex = NaraGame::CreateTexture(device, img,
                                       NaraGame::TextureFilter::Linear);
    if (!tex) return nullptr;
    NaraGame::RHITexture* raw = tex.get();
    m_textureCache.emplace(guid, std::move(tex));
    return raw;
}

void AssetDatabase::Rescan() {
    m_entries.clear();
    m_byPath.clear();
    m_byGuid.clear();
    m_root = std::make_unique<AssetTreeNode>();
    m_root->name     = "Assets";
    m_root->isFolder = true;
    if (m_assetsRoot.empty()) return;

    std::error_code ec;
    if (!fs::exists(m_assetsRoot, ec)) {
        NARAGAME_WARN("AssetDatabase: '%s' missing; nothing to scan",
                      m_assetsRoot.c_str());
        return;
    }
    ScanDir(m_assetsRoot, /*relPrefix*/ {}, m_root.get());
    NARAGAME_INFO("AssetDatabase: scanned %zu assets under '%s'",
                  m_entries.size(), m_assetsRoot.c_str());
}

void AssetDatabase::ScanDir(const std::string& dirAbs,
                            const std::string& relPrefix,
                            AssetTreeNode* parentNode) {
    std::error_code ec;
    // Collect first so we can sort folders before files for stable
    // Project-panel display.
    std::vector<fs::directory_entry> kids;
    for (auto it = fs::directory_iterator(dirAbs, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        kids.push_back(*it);
    }
    std::sort(kids.begin(), kids.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  const bool ad = a.is_directory();
                  const bool bd = b.is_directory();
                  if (ad != bd) return ad;  // folders first
                  return a.path().filename().string() <
                         b.path().filename().string();
              });
    for (const fs::directory_entry& entry : kids) {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory()) {
            auto folder = std::make_unique<AssetTreeNode>();
            folder->name     = name;
            folder->isFolder = true;
            AssetTreeNode* folderPtr = folder.get();
            parentNode->children.push_back(std::move(folder));
            const std::string subRel = relPrefix.empty()
                ? name
                : (relPrefix + "/" + name);
            ScanDir(ToGeneric(entry.path()), subRel, folderPtr);
            continue;
        }
        if (!entry.is_regular_file()) continue;
        if (EndsWith(name, kMetaSuffix)) continue;  // .meta handled alongside the asset

        const std::string absPath = ToGeneric(entry.path());
        const std::string relPath = relPrefix.empty()
            ? name
            : (relPrefix + "/" + name);
        AssetEntry e;
        if (!LoadOrCreateMeta(absPath, relPath, e)) continue;
        const size_t idx = m_entries.size();
        m_entries.push_back(std::move(e));
        m_byPath[m_entries[idx].relPath] = idx;
        m_byGuid[m_entries[idx].guid]    = idx;

        auto fileNode = std::make_unique<AssetTreeNode>();
        fileNode->name       = name;
        fileNode->isFolder   = false;
        fileNode->entryIndex = idx;
        parentNode->children.push_back(std::move(fileNode));
    }
}

bool AssetDatabase::LoadOrCreateMeta(const std::string& absAssetPath,
                                     const std::string& relAssetPath,
                                     AssetEntry& outEntry) {
    const std::string metaPath = MetaPathFor(absAssetPath);
    AssetEntry e;
    std::error_code ec;
    if (fs::exists(metaPath, ec) && ReadMeta(metaPath, e) && e.guid.IsValid()) {
        outEntry         = std::move(e);
        outEntry.relPath = relAssetPath;
        // Classification might change if the user renames the file;
        // re-derive each scan so the type label tracks reality.
        outEntry.typeName = ClassifyByExtension(relAssetPath);
        return true;
    }
    // Mint a new GUID + write the sidecar.
    AssetEntry fresh;
    fresh.guid     = AssetGuid::Generate();
    fresh.relPath  = relAssetPath;
    fresh.typeName = ClassifyByExtension(relAssetPath);
    fresh.importSettings = Value::MakeObject();
    if (!WriteMeta(absAssetPath, fresh)) {
        NARAGAME_WARN("AssetDatabase: cannot write meta for '%s'",
                      relAssetPath.c_str());
        return false;
    }
    outEntry = std::move(fresh);
    return true;
}

bool AssetDatabase::ReadMeta(const std::string& metaAbsPath,
                             AssetEntry& out) const {
    std::string text;
    if (!ReadFileText(metaAbsPath, text)) return false;
    std::string err;
    const Value root = NaraGame::MiniJson::Parse(text, &err);
    if (!root.IsObject()) {
        NARAGAME_WARN("AssetDatabase: '%s' parse failed: %s",
                      metaAbsPath.c_str(), err.c_str());
        return false;
    }
    out.guid = AssetGuid::FromString(root.GetString("guid"));
    out.typeName = root.GetString("type");
    if (const Value* imp = root.Find("importSettings")) {
        out.importSettings = *imp;
    } else {
        out.importSettings = Value::MakeObject();
    }
    return true;
}

bool AssetDatabase::WriteMeta(const std::string& absAssetPath,
                              const AssetEntry& entry) const {
    Value root = Value::MakeObject();
    root.Add("schema", Value::MakeNumber(1));
    root.Add("guid",   Value::MakeString(entry.guid.ToString()));
    root.Add("type",   Value::MakeString(entry.typeName));
    root.Add("importSettings", entry.importSettings);
    const std::string text = NaraGame::MiniJson::Write(root);
    return WriteFileText(MetaPathFor(absAssetPath), text);
}

const AssetEntry* AssetDatabase::FindByGuid(const AssetGuid& guid) const {
    auto it = m_byGuid.find(guid);
    if (it == m_byGuid.end()) return nullptr;
    return &m_entries[it->second];
}

const AssetEntry* AssetDatabase::FindByPath(const std::string& relPath) const {
    auto it = m_byPath.find(relPath);
    if (it == m_byPath.end()) return nullptr;
    return &m_entries[it->second];
}

std::string AssetDatabase::ClassifyByExtension(const std::string& relPath) {
    const std::string ext = LowerExt(relPath);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
        ext == ".tga")
        return "Texture";
    if (ext == ".lua" || ext == ".js" || ext == ".cs")
        return "Script";
    if (ext == ".nscene") return "Scene";
    if (ext == ".nprefab") return "Prefab";
    if (ext == ".tileset") return "Tileset";
    if (ext == ".autotile") return "AutotileRule";
    if (ext == ".sprite")  return "SpriteSheet";
    if (ext == ".hdr")     return "HdrEnvironment";
    if (ext == ".dds")     return "Texture";
    if (ext == ".gltf" || ext == ".glb") return "Model";
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") return "Audio";
    if (ext == ".ttf" || ext == ".otf") return "Font";
    return "Unknown";
}

std::string AssetDatabase::MetaPathFor(const std::string& absAssetPath) {
    return absAssetPath + kMetaSuffix;
}

} // namespace SceneEditor
