#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <thread>

// SDK/DLL initialization belongs to the host's control plane. Its progress
// must not look like a stalled audio processor to the APO supervisor. The
// absolute budget matches the existing 10 s offline initialization budget;
// periodic progress never extends it and never publishes audio readiness.
class HostStartupProgress {
    HANDLE stopEvent = nullptr;
    std::thread watcher;
public:
    explicit HostStartupProgress(std::atomic<std::uint64_t>& heartbeat) {
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent) return;
        const auto started = GetTickCount64();
        heartbeat.fetch_add(1, std::memory_order_release);
        try {
            watcher = std::thread([this, &heartbeat, started] {
                while (WaitForSingleObject(stopEvent, 250) == WAIT_TIMEOUT) {
                    if (GetTickCount64() - started >= 10000) {
                        // Only this host exits; the APO retains aligned audio
                        // and its existing bounded recovery/quarantine policy.
                        TerminateProcess(GetCurrentProcess(), 27);
                        return;
                    }
                    heartbeat.fetch_add(1, std::memory_order_release);
                }
            });
        } catch (...) { CloseHandle(stopEvent); stopEvent = nullptr; }
    }
    HostStartupProgress(const HostStartupProgress&) = delete;
    HostStartupProgress& operator=(const HostStartupProgress&) = delete;
    bool valid() const noexcept { return stopEvent != nullptr; }
    void stop() noexcept {
        if (stopEvent) SetEvent(stopEvent);
        if (watcher.joinable()) watcher.join();
        if (stopEvent) { CloseHandle(stopEvent); stopEvent = nullptr; }
    }
    ~HostStartupProgress() { stop(); }
};
