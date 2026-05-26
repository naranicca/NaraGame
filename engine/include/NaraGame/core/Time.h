#pragma once

#include <chrono>

namespace NaraGame {

// Frame timer. Tick() returns elapsed seconds since the previous Tick().
class Clock {
public:
    Clock() : m_last(Now()) {}

    float Tick() {
        const auto now = Now();
        const std::chrono::duration<float> delta = now - m_last;
        m_last = now;
        return delta.count();
    }

    // Seconds since construction, without affecting Tick().
    float Elapsed() const {
        const std::chrono::duration<float> delta = Now() - m_start;
        return delta.count();
    }

private:
    using TimePoint = std::chrono::steady_clock::time_point;
    static TimePoint Now() { return std::chrono::steady_clock::now(); }

    TimePoint m_start = Now();
    TimePoint m_last;
};

} // namespace NaraGame
