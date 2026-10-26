// ============================================================================
// core/media_manager.cpp
// The "Now playing" integration over GSMTC. See media_manager.h.
// ============================================================================
#include "media_manager.h"
#include "media_app_filter.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "chat_text.h"
#include "thread_detach_grace.h"
#include "../diagnostics/logger.h"

// C++/WinRT for Windows.Media.Control (Windows 10 1809+). MSVC-only, like the
// controller-name lookup in xinput_reader.cpp: mingw ships neither the WinRT
// headers nor windowsapp.lib, so the test build compiles the manager without a
// worker and isSupported() is false.
#ifdef _MSC_VER
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <cmath>
#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>   // GetSessions() in a range-for
#include <winrt/Windows.Media.Control.h>

#pragma comment(lib, "windowsapp.lib")
#endif

namespace {

#ifdef _MSC_VER
// UTF-16 -> CP1252 into a fixed buffer, dropping what the font cannot draw.
void wideToCp1252(const winrt::hstring& in, char* out, size_t outSize) {
    // hstring indexes with uint32_t; a size_t index is C4267 (an error) on MSVC.
    const winrt::hstring::size_type n = in.size();
    std::vector<uint32_t> cps;
    cps.reserve(n);
    for (winrt::hstring::size_type i = 0; i < n; ++i) {
        uint32_t c = static_cast<uint16_t>(in[i]);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n) {
            const uint32_t lo = static_cast<uint16_t>(in[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        cps.push_back(c);
    }
    snprintf(out, outSize, "%s", ChatText::codepointsToCp1252(cps).c_str());
}

// "1:23" / "1:02:03"
void formatClock(char* out, size_t outSize, int ms) {
    const int total = ms / 1000;
    const int h = total / 3600;
    const int m = (total / 60) % 60;
    const int s = total % 60;
    if (h > 0) {
        snprintf(out, outSize, "%d:%02d:%02d", h, m, s);
    } else {
        snprintf(out, outSize, "%d:%02d", m, s);
    }
}

// The process name whose audio sessions carry the player's volume: the app id
// itself for a desktop app ("Spotify.exe"), the part after '!' plus ".exe" for
// a Store app ("SpotifyAB.SpotifyMusic_...!Spotify" -> "Spotify.exe").
std::wstring audioExeFor(const winrt::hstring& source) {
    std::wstring s(source.c_str());
    const size_t bang = s.rfind(L'!');
    if (bang != std::wstring::npos) s = s.substr(bang + 1);
    if (s.size() < 4 || _wcsicmp(s.c_str() + s.size() - 4, L".exe") != 0) s += L".exe";
    return s;
}

bool processIs(DWORD pid, const std::wstring& exe) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[MAX_PATH];
    DWORD length = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &length) != 0;
    CloseHandle(process);
    if (!ok) return false;
    const wchar_t* base = wcsrchr(path, L'\\');
    return _wcsicmp(base ? base + 1 : path, exe.c_str()) == 0;
}

// Sets (setPercent >= 0) and reads the mixer volume of every audio session on
// the default output whose process is `exe`. False when there is none.
bool sessionVolume(const std::wstring& exe, int setPercent, int& outPercent) {
    winrt::com_ptr<IMMDeviceEnumerator> devices;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), devices.put_void()))) return false;
    winrt::com_ptr<IMMDevice> device;
    if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, device.put()))) return false;
    winrt::com_ptr<IAudioSessionManager2> sessions;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                sessions.put_void()))) return false;
    winrt::com_ptr<IAudioSessionEnumerator> list;
    if (FAILED(sessions->GetSessionEnumerator(list.put()))) return false;
    int count = 0;
    if (FAILED(list->GetCount(&count))) return false;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        winrt::com_ptr<IAudioSessionControl> control;
        if (FAILED(list->GetSession(i, control.put()))) continue;
        auto control2 = control.try_as<IAudioSessionControl2>();
        DWORD pid = 0;
        if (!control2 || FAILED(control2->GetProcessId(&pid)) || pid == 0 || !processIs(pid, exe)) continue;
        auto volume = control.try_as<ISimpleAudioVolume>();
        if (!volume) continue;
        if (setPercent >= 0) volume->SetMasterVolume(static_cast<float>(setPercent) / 100.0f, nullptr);
        float level = 0.0f;
        if (!found && SUCCEEDED(volume->GetMasterVolume(&level))) {
            outPercent = static_cast<int>(std::lround(level * 100.0f));
            found = true;
        }
    }
    return found;
}
#endif

bool sameSnapshot(const MediaManager::Snapshot& a, const MediaManager::Snapshot& b) {
    return a.state == b.state
        && a.positionMs / 1000 == b.positionMs / 1000
        && a.durationMs == b.durationMs
        && a.volume == b.volume
        && strcmp(a.title, b.title) == 0
        && strcmp(a.artist, b.artist) == 0;
}

}  // namespace

MediaManager& MediaManager::getInstance() {
    static MediaManager instance;
    return instance;
}

MediaManager::~MediaManager() {
    // Static-teardown backstop: fires ONLY when the orchestrated shutdown() was
    // skipped. Never joins (loader lock); see thread_detach_grace.h.
    if (m_worker.joinable()) {
        m_abandonCleanup.store(true);
        m_workerStop.store(true);
        m_cv.notify_all();
        ThreadTeardown::spinThenDetach(m_worker, m_workerFinished);
    }
}

bool MediaManager::isSupported() {
#ifdef _MSC_VER
    return true;
#else
    return false;
#endif
}

void MediaManager::initialize() {
    m_initialized.store(true);
    if (m_enabled.load()) {
        startWorker();
    }
}

void MediaManager::shutdown() {
    if (!m_worker.joinable()) {
        return;
    }
    m_workerStop.store(true);
    m_cv.notify_all();
    m_worker.join();
}

void MediaManager::setEnabled(bool enabled) {
    if (m_enabled.load() == enabled) {
        return;
    }
    m_enabled.store(enabled);

    Snapshot next;
    next.state = enabled ? (isSupported() ? State::OFF : State::UNAVAILABLE) : State::OFF;
    publish(next);

    // Settings load runs before initialize(); the worker starts there instead.
    if (enabled && m_initialized.load()) {
        startWorker();
    }
    m_cv.notify_all();
}

void MediaManager::sendCommand(Command command) {
    m_pendingCommand.store(static_cast<int>(command));
    m_cv.notify_all();
}

void MediaManager::setVolume(int percent) {
    m_pendingVolume.store(percent < 0 ? 0 : percent > 100 ? 100 : percent);
    m_cv.notify_all();
}

void MediaManager::setAppFilter(const char* filter) {
    MutexLock lock(m_mutex);
    snprintf(m_appFilter, sizeof(m_appFilter), "%s", filter ? filter : "");
}

void MediaManager::getAppFilter(char* out, size_t outSize) const {
    MutexLock lock(m_mutex);
    snprintf(out, outSize, "%s", m_appFilter);
}

void MediaManager::getSnapshot(Snapshot& out) const {
    MutexLock lock(m_mutex);
    out = m_snapshot;
}

void MediaManager::publish(const Snapshot& next) {
    MutexLock lock(m_mutex);
    if (sameSnapshot(m_snapshot, next) && strcmp(m_snapshot.time, next.time) == 0) {
        return;
    }
    m_snapshot = next;
    m_revision.fetch_add(1, std::memory_order_release);
}

void MediaManager::startWorker() {
    if (!isSupported() || m_worker.joinable()) {
        return;
    }
    m_workerStop.store(false);
    m_workerFinished.store(false);
    m_worker = std::thread([this] { workerLoop(); });
}

void MediaManager::workerLoop() {
    // Top-level barrier: an uncaught throw here calls std::terminate() and takes
    // the host game down with it.
    try {
#ifdef _MSC_VER
        using namespace winrt::Windows::Media::Control;
        using winrt::Windows::Foundation::AsyncStatus;

        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        // Bounded wait on one async call: cancelled rather than waited out, so
        // a player that never answers cannot hold up shutdown()'s join.
        auto finished = [](auto const& op) {
            if (op.wait_for(std::chrono::milliseconds(ASYNC_TIMEOUT_MS)) == AsyncStatus::Completed) {
                return true;
            }
            op.Cancel();
            return false;
        };

        GlobalSystemMediaTransportControlsSessionManager manager{ nullptr };
        State lastLogged = State::OFF;
        bool errorLogged = false;   // one log line per failure streak, not one a second
        winrt::hstring lastCurrent; // Windows' current player's app id, logged when it changes
        winrt::hstring lastSource;  // the followed player's app id
        std::wstring audioExe;      // the process its volume lives on (audioExeFor)
        Snapshot last;              // as last published, for a volume-only change
        std::chrono::steady_clock::time_point lastPoll{};

        while (!m_workerStop.load()) {
            const bool pollDue = m_pendingCommand.load() != static_cast<int>(Command::NONE)
                || std::chrono::steady_clock::now() - lastPoll >= std::chrono::milliseconds(POLL_INTERVAL_MS);
            if (!m_enabled.load()) {
                manager = nullptr;
                m_pendingCommand.store(static_cast<int>(Command::NONE));
                m_pendingVolume.store(-1);
                lastPoll = std::chrono::steady_clock::now();
            } else if (!pollDue) {
                // Woken for a volume change only (a drag sends many): apply the
                // newest, without polling the player.
                const int volume = m_pendingVolume.exchange(-1);
                int now = -1;
                if (volume >= 0 && !audioExe.empty() && sessionVolume(audioExe, volume, now)) {
                    last.volume = now;
                    publish(last);
                }
            } else {
                lastPoll = std::chrono::steady_clock::now();
                Snapshot next;
                next.state = State::NO_PLAYER;
                try {
                    if (!manager) {
                        auto op = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
                        if (finished(op)) manager = op.GetResults();
                    }
                    char filter[sizeof(m_appFilter)];
                    getAppFilter(filter, sizeof(filter));
                    GlobalSystemMediaTransportControlsSession session{ nullptr };
                    if (manager) {
                        // Windows' current player, logged when it changes: its app id
                        // (Spotify.exe, SpotifyAB.SpotifyMusic_...!Spotify, a browser's)
                        // is what [General] nowPlayingApps matches.
                        session = manager.GetCurrentSession();
                        const winrt::hstring current = session ? session.SourceAppUserModelId() : winrt::hstring{};
                        char app[128];
                        wideToCp1252(current, app, sizeof(app));
                        if (current != lastCurrent) {
                            lastCurrent = current;
                            if (!current.empty()) {
                                DEBUG_INFO_F("MediaManager: player is now '%s'%s", app,
                                             MediaAppFilter::matches(app, filter) ? "" : " (not in nowPlayingApps)");
                            }
                        }
                        // Not one of the named players: the first session that is.
                        if (!session || !MediaAppFilter::matches(app, filter)) {
                            session = nullptr;
                            for (const auto& candidate : manager.GetSessions()) {
                                wideToCp1252(candidate.SourceAppUserModelId(), app, sizeof(app));
                                if (MediaAppFilter::matches(app, filter)) {
                                    session = candidate;
                                    break;
                                }
                            }
                        }
                    }
                    if (session) {
                        const winrt::hstring source = session.SourceAppUserModelId();
                        if (source != lastSource) {
                            lastSource = source;
                            audioExe = audioExeFor(source);
                        }
                        const int volume = m_pendingVolume.exchange(-1);
                        int level = -1;
                        if (sessionVolume(audioExe, volume, level)) next.volume = level;

                        const Command command = static_cast<Command>(
                            m_pendingCommand.exchange(static_cast<int>(Command::NONE)));
                        if (command == Command::PREVIOUS) {
                            finished(session.TrySkipPreviousAsync());
                        } else if (command == Command::TOGGLE_PLAY) {
                            finished(session.TryTogglePlayPauseAsync());
                        } else if (command == Command::NEXT) {
                            finished(session.TrySkipNextAsync());
                        }

                        auto propsOp = session.TryGetMediaPropertiesAsync();
                        if (finished(propsOp)) {
                            auto props = propsOp.GetResults();
                            wideToCp1252(props.Title(), next.title, sizeof(next.title));
                            wideToCp1252(props.Artist(), next.artist, sizeof(next.artist));
                        }

                        const bool playing = session.GetPlaybackInfo().PlaybackStatus()
                            == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                        next.state = playing ? State::PLAYING : State::PAUSED;

                        // Players update the timeline only now and then (Spotify on
                        // a seek or a track change), so carry the position forward
                        // from when it was reported while playing.
                        auto timeline = session.GetTimelineProperties();
                        const auto end = timeline.EndTime() - timeline.StartTime();
                        auto pos = timeline.Position() - timeline.StartTime();
                        if (playing) {
                            const auto since = winrt::clock::now() - timeline.LastUpdatedTime();
                            // A player that never stamps the timeline reports the epoch.
                            if (since.count() > 0 && since < end) pos += since;
                        }
                        if (pos > end) pos = end;
                        if (pos.count() < 0) pos = {};
                        next.durationMs = static_cast<int>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(end).count());
                        next.positionMs = static_cast<int>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(pos).count());
                        if (next.durationMs > 0) {
                            char p[12], d[12];
                            formatClock(p, sizeof(p), next.positionMs);
                            formatClock(d, sizeof(d), next.durationMs);
                            snprintf(next.time, sizeof(next.time), "%s / %s", p, d);
                        }
                    } else {
                        // Nothing to send a command to; don't fire it at the next player.
                        m_pendingCommand.store(static_cast<int>(Command::NONE));
                    }
                    errorLogged = false;
                } catch (const winrt::hresult_error& e) {
                    if (!errorLogged) {
                        DEBUG_WARN_F("MediaManager: GSMTC call failed (0x%08X), retrying",
                                     static_cast<unsigned>(e.code()));
                        errorLogged = true;
                    }
                    manager = nullptr;
                    m_pendingCommand.store(static_cast<int>(Command::NONE));
                }
                if (m_enabled.load()) {
                    last = next;
                    publish(next);
                }
                if (next.state != lastLogged) {
                    DEBUG_INFO_F("MediaManager: state %d", static_cast<int>(next.state));
                    lastLogged = next.state;
                }
            }

            CvLock lk(m_mutex);
            // Explicit while-loop rather than the predicate overload: TSA cannot
            // see through the lambda (see thread_safety.h).
            const auto deadline = lastPoll + std::chrono::milliseconds(POLL_INTERVAL_MS);
            while (!m_workerStop.load()
                   && m_pendingCommand.load() == static_cast<int>(Command::NONE)
                   && m_pendingVolume.load() < 0
                   && m_cv.wait_until(lk.native(), deadline) != std::cv_status::timeout) {
            }
        }

        if (m_abandonCleanup.load()) {
            // Static teardown under the loader lock: leak rather than release.
            winrt::detach_abi(manager);
        } else {
            manager = nullptr;
            winrt::uninit_apartment();
        }
#endif
    } catch (const std::exception& e) {
        DEBUG_WARN_F("MediaManager: worker thread stopped on exception: %s", e.what());
    } catch (...) {
        DEBUG_WARN("MediaManager: worker thread stopped on unknown exception");
    }
    m_workerFinished.store(true, std::memory_order_release);
}
