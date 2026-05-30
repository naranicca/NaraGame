#pragma once

#include <functional>
#include <memory>
#include <string>

namespace NaraGame {

// Filesystem watcher for hot-reloading assets while the game is running.
// Watch() starts a background thread on the supplied directory (recursive)
// and queues every modify/add/rename-target event it sees; Poll() drains
// that queue on whatever thread the caller invokes it from (intended use:
// once per frame on the main thread) and fires the user callback per file
// with the path RELATIVE to the watched root and using forward slashes.
//
// The callback runs synchronously inside Poll(), so it's safe to touch GPU
// resources from there. Editor saves often emit a burst of duplicate
// MODIFIED events for the same path; Poll() collapses consecutive
// duplicates within one drain, but the caller's reload should still be
// idempotent.
//
// Implementation is Win32 ReadDirectoryChangesW; other platforms are
// no-ops (Watch returns false).
class AssetWatcher {
public:
    using Callback = std::function<void(const std::string& relativePath)>;

    AssetWatcher();
    ~AssetWatcher();

    AssetWatcher(const AssetWatcher&) = delete;
    AssetWatcher& operator=(const AssetWatcher&) = delete;

    // Start watching `directory` recursively. Replaces any previously
    // installed watch. Returns false if the directory can't be opened.
    bool Watch(const std::string& directory, Callback onChange);

    // Drain queued events and fire the callback for each. Cheap when
    // nothing changed -- a lock acquire and an empty vector swap.
    void Poll();

    // Stop the watcher thread and release the handle. Called by the
    // destructor; safe to call manually or repeatedly.
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace NaraGame
