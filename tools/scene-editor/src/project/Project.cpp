#include "Project.h"

#include <NaraGame/assets/MiniJson.h>
#include <NaraGame/core/Log.h>

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
    return { "Sprites", "Tilesets", "Scripts", "Scenes", "Prefabs", "Materials" };
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
    // Schema version is the editor's project-file format; bump when the
    // shape changes incompatibly. Distinct from the user-facing
    // `version` above.
    root.Add("schema",  Value::MakeNumber(1));
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
    return true;
}

} // namespace SceneEditor
