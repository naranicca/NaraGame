#include "NaraGame/assets/AssetWatcher.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/core/Platform.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#if defined(NARAGAME_PLATFORM_WINDOWS)
#include <Windows.h>
#endif

namespace NaraGame {

#if defined(NARAGAME_PLATFORM_WINDOWS)

namespace {
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

std::string WideToUtf8(const wchar_t* w, size_t len) {
    if (len == 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len),
                        s.data(), n, nullptr, nullptr);
    return s;
}
} // namespace

struct AssetWatcher::Impl {
    std::string directory;
    Callback callback;
    HANDLE dirHandle = INVALID_HANDLE_VALUE;
    HANDLE cancelEvent = nullptr;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::mutex mu;
    std::vector<std::string> pending;
};

AssetWatcher::AssetWatcher() : m_impl(std::make_unique<Impl>()) {}
AssetWatcher::~AssetWatcher() { Stop(); }

bool AssetWatcher::Watch(const std::string& directory, Callback onChange) {
    Stop();
    m_impl->directory = directory;
    m_impl->callback = std::move(onChange);

    const std::wstring wpath = Utf8ToWide(directory);
    m_impl->dirHandle = CreateFileW(
        wpath.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (m_impl->dirHandle == INVALID_HANDLE_VALUE) {
        NARAGAME_WARN("AssetWatcher: failed to open '%s' (err=%lu)",
                      directory.c_str(), GetLastError());
        return false;
    }

    m_impl->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_impl->stop.store(false);

    m_impl->worker = std::thread([impl = m_impl.get()] {
        // Single-shot OVERLAPPED reads in a loop. We wait on the I/O event
        // OR the cancel event; either wakes us, but only the cancel event
        // means we should exit. ReadDirectoryChangesW's buffer can overflow
        // on heavy save bursts -- bytes==0 in that case and we just retry.
        constexpr DWORD kBufSize = 16 * 1024;
        std::vector<BYTE> buffer(kBufSize);
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE handles[2] = { ov.hEvent, impl->cancelEvent };
        const DWORD filter = FILE_NOTIFY_CHANGE_LAST_WRITE
                           | FILE_NOTIFY_CHANGE_FILE_NAME
                           | FILE_NOTIFY_CHANGE_DIR_NAME
                           | FILE_NOTIFY_CHANGE_SIZE;

        while (!impl->stop.load(std::memory_order_relaxed)) {
            ResetEvent(ov.hEvent);
            DWORD bytes = 0;
            BOOL ok = ReadDirectoryChangesW(impl->dirHandle, buffer.data(), kBufSize,
                                            TRUE, filter, &bytes, &ov, nullptr);
            if (!ok) break;

            const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (waited != WAIT_OBJECT_0) {
                // cancel event fired (or wait failed) -- cancel the pending I/O
                // and drain its result so the handle is safe to close.
                CancelIoEx(impl->dirHandle, &ov);
                GetOverlappedResult(impl->dirHandle, &ov, &bytes, TRUE);
                break;
            }

            if (!GetOverlappedResult(impl->dirHandle, &ov, &bytes, FALSE) || bytes == 0)
                continue;

            BYTE* p = buffer.data();
            for (;;) {
                auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
                if (info->Action == FILE_ACTION_MODIFIED ||
                    info->Action == FILE_ACTION_ADDED ||
                    info->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                    std::string name = WideToUtf8(info->FileName,
                        info->FileNameLength / sizeof(WCHAR));
                    for (char& c : name) if (c == '\\') c = '/';
                    std::lock_guard<std::mutex> g(impl->mu);
                    impl->pending.push_back(std::move(name));
                }
                if (info->NextEntryOffset == 0) break;
                p += info->NextEntryOffset;
            }
        }
        CloseHandle(ov.hEvent);
    });

    NARAGAME_INFO("AssetWatcher: watching '%s'", directory.c_str());
    return true;
}

void AssetWatcher::Poll() {
    std::vector<std::string> drained;
    {
        std::lock_guard<std::mutex> g(m_impl->mu);
        drained.swap(m_impl->pending);
    }
    if (!m_impl->callback) return;
    // Coalesce consecutive duplicates so the typical "editor writes file,
    // OS emits 2-3 modifies" pattern reloads once. Not a full dedupe --
    // two different files in the same drain still fire separately.
    std::string last;
    for (const std::string& path : drained) {
        if (path == last) continue;
        m_impl->callback(path);
        last = path;
    }
}

void AssetWatcher::Stop() {
    if (!m_impl) return;
    m_impl->stop.store(true);
    if (m_impl->cancelEvent) SetEvent(m_impl->cancelEvent);
    if (m_impl->worker.joinable()) m_impl->worker.join();
    if (m_impl->dirHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_impl->dirHandle);
        m_impl->dirHandle = INVALID_HANDLE_VALUE;
    }
    if (m_impl->cancelEvent) {
        CloseHandle(m_impl->cancelEvent);
        m_impl->cancelEvent = nullptr;
    }
    m_impl->callback = nullptr;
    {
        std::lock_guard<std::mutex> g(m_impl->mu);
        m_impl->pending.clear();
    }
}

#else // !NARAGAME_PLATFORM_WINDOWS

struct AssetWatcher::Impl {};
AssetWatcher::AssetWatcher() : m_impl(std::make_unique<Impl>()) {}
AssetWatcher::~AssetWatcher() = default;
bool AssetWatcher::Watch(const std::string&, Callback) {
    NARAGAME_WARN("AssetWatcher: not implemented on this platform");
    return false;
}
void AssetWatcher::Poll() {}
void AssetWatcher::Stop() {}

#endif

} // namespace NaraGame
