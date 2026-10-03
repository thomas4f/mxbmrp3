// ============================================================================
// core/winhttp_guard.h
// Scope ownership for the chat workers' WinHTTP handles.
//
// The workers manage a session, a connection and one request (or WebSocket)
// at a time. Closed by hand on every return path, they leaked on an exception
// (a std::bad_alloc while appending a response, say): the worker's top-level
// catch ended the thread, ensureWorker() restarted it, and the abandoned set
// stayed open. These two close on unwind as well as on return.
//
// WinHttpHandle owns a handle nobody else can close. ScopeExit runs a cleanup
// that must stay custom: a request published for cancellation is closed only
// if the canceller has not already closed it, which is the manager's check.
// ============================================================================
#pragma once

#include <windows.h>
#include <winhttp.h>

#include <utility>

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET h = nullptr) : m_h(h) {}
    ~WinHttpHandle() { if (m_h) WinHttpCloseHandle(m_h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    HINTERNET get() const { return m_h; }
    explicit operator bool() const { return m_h != nullptr; }

private:
    HINTERNET m_h;
};

template <class F>
class ScopeExit {
public:
    explicit ScopeExit(F f) : m_f(std::move(f)) {}
    ~ScopeExit() { m_f(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    F m_f;
};
