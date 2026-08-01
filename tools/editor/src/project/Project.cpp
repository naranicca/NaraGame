#include "Project.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/core/Log.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace SceneEditor {

namespace {

constexpr const char* kProjectJsonRel = "ProjectSettings/project.json";

// Path normaliser: convert back-slashes to forward-slashes so log lines
// and serialised paths look the same on Windows as on Unix. We only
// store these in-memory; std::filesystem still works either way.
std::string Normalise(const fs::path& p) {
    std::string s = p.generic_string();
    return s;
}

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

} // namespace

std::vector<std::string> Project::AssetSubfolders() {
    return { "Sprites", "Tilesets", "Scripts", "Scenes", "Prefabs",
             "Materials", "Animations" };
}

std::vector<std::string> Project::DefaultSortingLayers() {
    return { "Background", "Default", "Foreground", "UI" };
}

std::vector<std::string> Project::DefaultCollisionLayers() {
    return { "Default", "Player", "Enemy", "Item" };
}

void Project::SetCollisionLayers(std::vector<std::string> layers) {
    if (layers.empty()) layers = DefaultCollisionLayers();
    m_collisionLayers = std::move(layers);
    // Drop matrix entries that reference layers that no longer
    // exist so the file doesn't accumulate dead pairs across
    // rename / remove cycles.
    auto exists = [&](const std::string& name) {
        for (const std::string& l : m_collisionLayers) {
            if (l == name) return true;
        }
        return false;
    };
    m_collisionMatrixOff.erase(
        std::remove_if(m_collisionMatrixOff.begin(),
                       m_collisionMatrixOff.end(),
                       [&](const auto& kv) {
                           return !exists(kv.first) || !exists(kv.second);
                       }),
        m_collisionMatrixOff.end());
}

namespace {
std::pair<std::string, std::string> CanonLayerPair(
        const std::string& a, const std::string& b) {
    return (a <= b) ? std::pair<std::string, std::string>{a, b}
                    : std::pair<std::string, std::string>{b, a};
}
}

bool Project::CollisionLayersCollide(const std::string& a,
                                     const std::string& b) const {
    const auto key = CanonLayerPair(a, b);
    for (const auto& kv : m_collisionMatrixOff) {
        if (kv == key) return false;
    }
    return true;
}

void Project::SetCollisionLayersCollide(const std::string& a,
                                        const std::string& b,
                                        bool collide) {
    const auto key = CanonLayerPair(a, b);
    auto it = std::find(m_collisionMatrixOff.begin(),
                        m_collisionMatrixOff.end(), key);
    if (collide) {
        if (it != m_collisionMatrixOff.end()) {
            m_collisionMatrixOff.erase(it);
        }
    } else {
        if (it == m_collisionMatrixOff.end()) {
            m_collisionMatrixOff.push_back(key);
        }
    }
}

namespace {
float ClampVol(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}
}

void Project::SetAudioMasterVolume(float v) { m_audioMasterVolume = ClampVol(v); }
void Project::SetAudioSfxVolume(float v)    { m_audioSfxVolume    = ClampVol(v); }
void Project::SetAudioMusicVolume(float v)  { m_audioMusicVolume  = ClampVol(v); }
void Project::SetAudioReverbWet(float v)    { m_audioReverbWet    = ClampVol(v); }

void Project::SetSortingLayers(std::vector<std::string> layers) {
    if (layers.empty()) layers = DefaultSortingLayers();
    m_sortingLayers = std::move(layers);
}

int Project::LayerPriority(const std::string& name) const {
    for (size_t i = 0; i < m_sortingLayers.size(); ++i) {
        if (m_sortingLayers[i] == name) return int(i);
    }
    // Unknown layer -> use the "Default" slot if present so it falls
    // somewhere reasonable instead of leaping to layer 0 (back) and
    // disappearing behind every other sprite.
    for (size_t i = 0; i < m_sortingLayers.size(); ++i) {
        if (m_sortingLayers[i] == "Default") return int(i);
    }
    return 0;
}

std::string Project::AssetsDir()       const { return m_rootDir + "/Assets"; }
std::string Project::LibraryDir()      const { return m_rootDir + "/Library"; }
std::string Project::SettingsDir()     const { return m_rootDir + "/ProjectSettings"; }
std::string Project::ProjectJsonPath() const { return m_rootDir + "/" + kProjectJsonRel; }

bool Project::Create(const std::string& rootDir, const std::string& name) {
    const fs::path root(rootDir);
    std::error_code ec;
    if (fs::exists(root, ec)) {
        if (!fs::is_directory(root, ec)) {
            NARAGAME_ERROR("Project::Create: '%s' exists and isn't a directory",
                           rootDir.c_str());
            return false;
        }
        const bool nonEmpty = fs::directory_iterator(root, ec) !=
                              fs::directory_iterator();
        if (nonEmpty) {
            NARAGAME_ERROR("Project::Create: '%s' exists and is not empty",
                           rootDir.c_str());
            return false;
        }
    } else {
        if (!fs::create_directories(root, ec)) {
            NARAGAME_ERROR("Project::Create: cannot create '%s' (%s)",
                           rootDir.c_str(), ec.message().c_str());
            return false;
        }
    }

    m_rootDir = Normalise(fs::absolute(root, ec));
    m_name    = name.empty() ? root.filename().string() : name;
    m_version = "0.1.0";
    m_applicationId = "com.naragame.player";

    if (!EnsureFolderTree() || !WriteProjectJson()) {
        Close();
        return false;
    }
    NARAGAME_INFO("Project::Create: '%s' at %s",
                  m_name.c_str(), m_rootDir.c_str());
    return true;
}

bool Project::Open(const std::string& rootDir) {
    std::error_code ec;
    const fs::path root = fs::absolute(rootDir, ec);
    if (!fs::is_directory(root, ec)) {
        NARAGAME_ERROR("Project::Open: '%s' is not a directory",
                       rootDir.c_str());
        return false;
    }
    const fs::path pj = root / kProjectJsonRel;
    if (!fs::exists(pj, ec)) {
        NARAGAME_ERROR("Project::Open: '%s' has no ProjectSettings/project.json",
                       rootDir.c_str());
        return false;
    }
    m_rootDir = Normalise(root);
    if (!ReadProjectJson()) {
        Close();
        return false;
    }
    // Recreate any missing subfolders so the rest of the editor can
    // assume the standard tree exists.
    EnsureFolderTree();
    NARAGAME_INFO("Project::Open: '%s' (%s) at %s",
                  m_name.c_str(), m_version.c_str(), m_rootDir.c_str());
    return true;
}

bool Project::Save() const {
    if (!IsOpen()) return false;
    return WriteProjectJson();
}

void Project::Close() {
    m_rootDir.clear();
    m_name.clear();
    m_version = "0.1.0";
    m_applicationId = "com.naragame.player";
    m_audioMasterVolume = 1.0f;
    m_audioSfxVolume    = 1.0f;
    m_audioMusicVolume  = 1.0f;
    m_audioReverbWet    = 0.0f;
    m_mainScenePath.clear();
    m_lastOpenedScenePath.clear();
    m_editorPlayState = true;
}

bool Project::EnsureFolderTree() const {
    std::error_code ec;
    fs::create_directories(AssetsDir(),   ec);
    fs::create_directories(LibraryDir(),  ec);
    fs::create_directories(SettingsDir(), ec);
    for (const std::string& sub : AssetSubfolders()) {
        fs::create_directories(AssetsDir() + "/" + sub, ec);
    }
    // Library mirrors Unity's layout: metadata + thumbnails + a generic
    // cache bucket for imported binaries.
    fs::create_directories(LibraryDir() + "/metadata",   ec);
    fs::create_directories(LibraryDir() + "/cache",      ec);
    fs::create_directories(LibraryDir() + "/thumbnails", ec);
    return true;
}

bool Project::WriteProjectJson() const {
    using NaraGame::MiniJson::Value;
    Value root = Value::MakeObject();
    root.Add("name",    Value::MakeString(m_name));
    root.Add("version", Value::MakeString(m_version));
    root.Add("applicationId", Value::MakeString(m_applicationId));
    // Schema version is the editor's project-file format; bump when the
    // shape changes incompatibly. Distinct from the user-facing
    // `version` above.
    root.Add("schema",  Value::MakeNumber(1));
    Value layers = Value::MakeArray();
    for (const std::string& l : m_sortingLayers) {
        layers.Push(Value::MakeString(l));
    }
    root.Add("sortingLayers", std::move(layers));
    Value clayers = Value::MakeArray();
    for (const std::string& l : m_collisionLayers) {
        clayers.Push(Value::MakeString(l));
    }
    root.Add("collisionLayers", std::move(clayers));
    Value cmatrix = Value::MakeArray();
    for (const auto& kv : m_collisionMatrixOff) {
        Value pair = Value::MakeArray();
        pair.Push(Value::MakeString(kv.first));
        pair.Push(Value::MakeString(kv.second));
        cmatrix.Push(std::move(pair));
    }
    root.Add("collisionMatrixOff", std::move(cmatrix));
    // M8c12 audio mixer block. Always written so a fresh project gets
    // a visible record of its default state in project.json; the read
    // path tolerates the block being absent.
    Value mixer = Value::MakeObject();
    mixer.Add("master",    Value::MakeNumber(double(m_audioMasterVolume)));
    mixer.Add("sfx",       Value::MakeNumber(double(m_audioSfxVolume)));
    mixer.Add("music",     Value::MakeNumber(double(m_audioMusicVolume)));
    mixer.Add("reverbWet", Value::MakeNumber(double(m_audioReverbWet)));
    root.Add("audioMixer", std::move(mixer));
    if (!m_mainScenePath.empty()) {
        root.Add("mainScene", Value::MakeString(m_mainScenePath));
    }
    if (!m_lastOpenedScenePath.empty()) {
        root.Add("lastScene", Value::MakeString(m_lastOpenedScenePath));
    }
    root.Add("editorPlayState", Value::MakeBool(m_editorPlayState));
    const std::string out = NaraGame::MiniJson::Write(root);
    if (!WriteFileText(ProjectJsonPath(), out)) {
        NARAGAME_ERROR("Project: cannot write '%s'", ProjectJsonPath().c_str());
        return false;
    }
    return true;
}

bool Project::ReadProjectJson() {
    std::string source;
    if (!ReadFileText(ProjectJsonPath(), source)) {
        NARAGAME_ERROR("Project: cannot read '%s'", ProjectJsonPath().c_str());
        return false;
    }
    std::string err;
    const NaraGame::MiniJson::Value root =
        NaraGame::MiniJson::Parse(source, &err);
    if (!root.IsObject()) {
        NARAGAME_ERROR("Project: '%s' parse failed: %s",
                       ProjectJsonPath().c_str(), err.c_str());
        return false;
    }
    m_name    = root.GetString("name",    fs::path(m_rootDir).filename().string());
    m_version = root.GetString("version", "0.1.0");
    m_applicationId = root.GetString("applicationId", "com.naragame.player");
    // Sorting layers default to the standard set when missing or empty
    // so legacy projects pick up a sensible draw order automatically.
    const NaraGame::MiniJson::Value* layers = root.Find("sortingLayers");
    if (layers && layers->IsArray() && !layers->arrayValue.empty()) {
        m_sortingLayers.clear();
        for (const NaraGame::MiniJson::Value& v : layers->arrayValue) {
            if (v.IsString()) m_sortingLayers.push_back(v.stringValue);
        }
        if (m_sortingLayers.empty()) m_sortingLayers = DefaultSortingLayers();
    } else {
        m_sortingLayers = DefaultSortingLayers();
    }
    const NaraGame::MiniJson::Value* clayers = root.Find("collisionLayers");
    m_collisionLayers.clear();
    if (clayers && clayers->IsArray()) {
        for (const NaraGame::MiniJson::Value& v : clayers->arrayValue) {
            if (v.IsString()) m_collisionLayers.push_back(v.stringValue);
        }
    }
    if (m_collisionLayers.empty()) {
        m_collisionLayers = DefaultCollisionLayers();
    }
    // M8c12 audio mixer block. Missing object / fields => defaults
    // (1.0). Values out of range get clamped on Set, so a hand-edited
    // 5.0 in JSON loads as 1.0 instead of blowing speakers.
    const NaraGame::MiniJson::Value* mixer = root.Find("audioMixer");
    if (mixer && mixer->IsObject()) {
        SetAudioMasterVolume(float(mixer->GetNumber("master",    1.0)));
        SetAudioSfxVolume   (float(mixer->GetNumber("sfx",       1.0)));
        SetAudioMusicVolume (float(mixer->GetNumber("music",     1.0)));
        SetAudioReverbWet   (float(mixer->GetNumber("reverbWet", 0.0)));
    } else {
        m_audioMasterVolume = 1.0f;
        m_audioSfxVolume    = 1.0f;
        m_audioMusicVolume  = 1.0f;
        m_audioReverbWet    = 0.0f;
    }
    m_mainScenePath       = root.GetString("mainScene", "");
    m_lastOpenedScenePath = root.GetString("lastScene", "");
    m_editorPlayState     = root.GetBool  ("editorPlayState", true);
    m_collisionMatrixOff.clear();
    const NaraGame::MiniJson::Value* cmatrix = root.Find("collisionMatrixOff");
    if (cmatrix && cmatrix->IsArray()) {
        for (const NaraGame::MiniJson::Value& pair : cmatrix->arrayValue) {
            if (pair.IsArray() && pair.arrayValue.size() >= 2 &&
                pair.arrayValue[0].IsString() &&
                pair.arrayValue[1].IsString()) {
                m_collisionMatrixOff.push_back(
                    CanonLayerPair(pair.arrayValue[0].stringValue,
                                   pair.arrayValue[1].stringValue));
            }
        }
    }
    return true;
}

} // namespace SceneEditor
