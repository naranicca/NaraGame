#pragma once

#include <string>
#include <vector>

namespace SceneEditor {

// In-memory project state. Maps to a `ProjectSettings/project.json`
// alongside the standard Assets/ Library/ folder tree the editor expects.
// One Project is open at a time; the editor swaps the active Project
// when the user picks File > New / Open / Close.
class Project {
public:
    Project() = default;

    // Create a fresh project at `rootDir`. The directory must either not
    // exist yet, or exist and be empty -- we refuse to write into an
    // arbitrary folder so a typo doesn't carpet-bomb the user's home.
    // On success the project folder tree is created and project.json is
    // written; the Project instance is left in the open state.
    bool Create(const std::string& rootDir, const std::string& name);

    // Open the project rooted at `rootDir` -- the directory that
    // contains ProjectSettings/project.json. Reads the JSON; populates
    // name + version. Missing folders (Assets/, Library/, ...) get
    // created on the fly so opening a half-broken project still leaves
    // a complete tree on disk.
    bool Open(const std::string& rootDir);

    // Write the in-memory state back to project.json.
    bool Save() const;

    // Drop the project; the editor returns to the "no project open" state.
    void Close();

    bool IsOpen() const { return !m_rootDir.empty(); }

    const std::string& RootDir() const { return m_rootDir; }
    const std::string& Name()    const { return m_name; }
    const std::string& Version() const { return m_version; }
    const std::string& ApplicationId() const { return m_applicationId; }
    void SetVersion(std::string value) { m_version = std::move(value); }
    void SetApplicationId(std::string value) {
        m_applicationId = std::move(value);
    }
    int DefaultScreenWidth() const { return m_defaultScreenWidth; }
    int DefaultScreenHeight() const { return m_defaultScreenHeight; }
    bool DefaultFullscreen() const { return m_defaultFullscreen; }
    bool ResizableWindow() const { return m_resizableWindow; }
    void SetDefaultScreenWidth(int v) { m_defaultScreenWidth = v < 320 ? 320 : v; }
    void SetDefaultScreenHeight(int v) { m_defaultScreenHeight = v < 180 ? 180 : v; }
    void SetDefaultFullscreen(bool v) { m_defaultFullscreen = v; }
    void SetResizableWindow(bool v) { m_resizableWindow = v; }

    // Conventional paths inside the project. All come back with forward
    // slashes regardless of platform.
    std::string AssetsDir()       const;
    std::string LibraryDir()      const;
    std::string SettingsDir()     const;
    std::string ProjectJsonPath() const;

    // Asset subfolders (one per asset kind the editor knows about).
    static std::vector<std::string> AssetSubfolders();

    // Sorting layers control the draw order across SpriteRenderer
    // components. The vector is back-to-front: index 0 draws first
    // (deepest), the last entry draws last (closest). Fresh projects
    // start with the same default list Unity uses; the editor's
    // "Sorting Layers..." dialog edits these.
    const std::vector<std::string>& SortingLayers() const { return m_sortingLayers; }
    void SetSortingLayers(std::vector<std::string> layers);

    // Resolve a layer name to its priority. Returns the index of `name`
    // when present, the index of "Default" when name is unknown but
    // "Default" exists, and 0 otherwise. Used by the viewport sort.
    int LayerPriority(const std::string& name) const;

    static std::vector<std::string> DefaultSortingLayers();

    // M7c11: Collision layers + the NxN matrix the runtime checks
    // before generating any overlap pair. Layers default to
    // {Default, Player, Enemy, Item} + everything collides; a
    // pair is skipped when CollisionLayersCollide(a, b) returns
    // false. The matrix is symmetric -- writing (a, b) also
    // writes (b, a).
    const std::vector<std::string>& CollisionLayers() const { return m_collisionLayers; }
    void SetCollisionLayers(std::vector<std::string> layers);

    // Layer-pair query. Unknown layer names fall back to
    // "Default" the same way LayerPriority does. Returns true
    // when nothing in the matrix forbids the pair.
    bool CollisionLayersCollide(const std::string& a,
                                const std::string& b) const;
    void SetCollisionLayersCollide(const std::string& a,
                                   const std::string& b,
                                   bool collide);

    static std::vector<std::string> DefaultCollisionLayers();

    // M8c12 audio mixer state. Per-bus volumes the editor's Audio
    // Mixer modal edits + persists into project.json so each
    // project keeps its own SFX/music balance. All values are
    // clamped to [0, 1] on set; defaults are 1.0 (unity gain) so
    // legacy projects (with no `audioMixer` block in their JSON)
    // load to the same sound level as before this milestone.
    float AudioMasterVolume() const { return m_audioMasterVolume; }
    float AudioSfxVolume()    const { return m_audioSfxVolume; }
    float AudioMusicVolume()  const { return m_audioMusicVolume; }
    // M8c13 reverb wet-send. 0 = dry, 1 = full reverb tail on
    // top of dry signal. Lives next to the bus volumes inside
    // `audioMixer` so re-opening a project restores the same
    // ambience.
    float AudioReverbWet()    const { return m_audioReverbWet; }

    // M9c12 startup scene. Path (relative to Assets/) of the
    // scene a Build Game run boots into. Empty = no default;
    // a standalone exe launched without --scene against a
    // project whose mainScene is empty stays on the empty
    // scene the editor's default constructor produces.
    const std::string& MainScenePath() const { return m_mainScenePath; }
    void SetMainScenePath(std::string p)     { m_mainScenePath = std::move(p); }

    // Path (relative to Assets/) of the scene the editor was last
    // working on. Written on every scene load/save so re-opening the
    // project drops the user back on the same file instead of the
    // empty default scene. Distinct from MainScenePath, which is the
    // stable "boot scene" for standalone builds.
    const std::string& LastOpenedScenePath() const { return m_lastOpenedScenePath; }
    void SetLastOpenedScenePath(std::string p)     { m_lastOpenedScenePath = std::move(p); }

    // Editor Play-mode toggle at last save. Restored on project open
    // so the user comes back to the same simulate / paused state they
    // left in. Defaults to true (unchanged from the pre-persistence
    // behaviour), so a legacy project.json with no `editorPlayState`
    // block boots exactly as it did before.
    bool EditorPlayState() const { return m_editorPlayState; }
    void SetEditorPlayState(bool on) { m_editorPlayState = on; }
    void  SetAudioMasterVolume(float v);
    void  SetAudioSfxVolume(float v);
    void  SetAudioMusicVolume(float v);
    void  SetAudioReverbWet(float v);

private:
    bool EnsureFolderTree() const;
    bool WriteProjectJson() const;
    bool ReadProjectJson();

    std::string m_rootDir;
    std::string m_name;
    std::string m_version = "0.1.0";
    std::string m_applicationId = "com.naragame.player";
    int m_defaultScreenWidth = 1280;
    int m_defaultScreenHeight = 720;
    bool m_defaultFullscreen = false;
    bool m_resizableWindow = true;
    std::vector<std::string> m_sortingLayers = DefaultSortingLayers();
    std::vector<std::string> m_collisionLayers = DefaultCollisionLayers();
    // 2D bit-set indexed by layer pair (i, j) with i <= j. A
    // missing entry is "true" (collide); SetCollisionLayersCollide
    // populates it when the user un-ticks a pair.
    std::vector<std::pair<std::string, std::string>> m_collisionMatrixOff;
    // M8c12. Defaults to unity so omitting the JSON block leaves
    // playback unchanged.
    float m_audioMasterVolume = 1.0f;
    float m_audioSfxVolume    = 1.0f;
    float m_audioMusicVolume  = 1.0f;
    float m_audioReverbWet    = 0.0f;
    std::string m_mainScenePath;
    std::string m_lastOpenedScenePath;
    bool m_editorPlayState = true;
};

} // namespace SceneEditor
