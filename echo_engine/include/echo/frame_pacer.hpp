#pragma once
#include <chrono>
#include <algorithm>
#include <windows.h>
namespace echo {
// Rendering has its own deadline; low caps never stop 60 Hz input/audio/simulation servicing.
class FrameGate {
    int fps_ = -1;
    double next_{};

  public:
    bool due(double now, int fps) {
        if (fps != fps_) {
            fps_ = fps;
            next_ = now;
        }
        if (fps == 0)
            return true;
        if (now + 1e-9 < next_)
            return false;
        next_ += 1. / fps;
        if (next_ <= now)
            next_ = now + 1. / fps;
        return true;
    }
};
// Pure presentation scheduler. It cannot inspect or change WorldState / fixedDt.
class FramePacer {
    HANDLE timer_{};

  public:
    FramePacer() {
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 2, TIMER_ALL_ACCESS);
        if (!timer_)
            timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    ~FramePacer() {
        if (timer_)
            CloseHandle(timer_);
    }
    FramePacer(const FramePacer&) = delete;
    void wait(std::chrono::steady_clock::time_point start, int fps) {
        using Clock = std::chrono::steady_clock;
        if (fps == 0) {
            SwitchToThread();
            return;
        }
        auto deadline =
            start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1. / fps));
        while (Clock::now() < deadline) {
            auto left = deadline - Clock::now();
            if (timer_) {
                LARGE_INTEGER due;
                due.QuadPart = -std::max<LONGLONG>(
                    1, std::chrono::duration_cast<std::chrono::nanoseconds>(left).count() / 100);
                if (!SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                    Sleep(1);
                    continue;
                }
                auto result =
                    MsgWaitForMultipleObjectsEx(1, &timer_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (result == WAIT_OBJECT_0)
                    break;
                if (result == WAIT_FAILED) {
                    Sleep(1);
                    continue;
                }
            } else {
                Sleep(1);
            }
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    PostQuitMessage(int(message.wParam));
                    return;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
};
} // namespace echo
