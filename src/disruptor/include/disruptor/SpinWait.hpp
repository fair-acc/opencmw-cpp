#pragma once

#include <cstdint>
#include <functional>

namespace opencmw::disruptor {

class SpinWait {
    using Clock                                            = std::conditional_t<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock, std::chrono::steady_clock>;
    std::int32_t              m_count                      = 0;

    static const std::int32_t YIELD_THRESHOLD              = 10;
    static const std::int32_t SLEEP_0_EVERY_HOW_MANY_TIMES = 5;
    static const std::int32_t SLEEP_1_EVERY_HOW_MANY_TIMES = 20;

public:
    SpinWait() = default;

    std::int32_t count() const {
        return m_count;
    }

    bool nextSpinWillYield() const {
        return m_count > YIELD_THRESHOLD;
    }

    void spinOnce() {
        if (nextSpinWillYield()) {
            auto num = m_count >= YIELD_THRESHOLD ? m_count - 10 : m_count;
            if (num % SLEEP_1_EVERY_HOW_MANY_TIMES == SLEEP_1_EVERY_HOW_MANY_TIMES - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                if (num % SLEEP_0_EVERY_HOW_MANY_TIMES == SLEEP_0_EVERY_HOW_MANY_TIMES - 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(0));
                } else {
                    std::this_thread::yield();
                }
            }
        } else {
            spinWaitInternal(4 << m_count);
        }

        if (m_count == std::numeric_limits<std::int32_t>::max()) {
            m_count = YIELD_THRESHOLD;
        } else {
            ++m_count;
        }
    }

    void reset() { m_count = 0; }
    void spinUntil(const std::function<bool()> &condition) const { spinUntil(condition, -1); }
    bool spinUntil(const std::function<bool()> &condition, std::int64_t millisecondsTimeout) const {
        if (millisecondsTimeout < -1) {
            throw std::out_of_range("Timeout value is out of range");
        }

        std::int64_t num = 0;
        if (millisecondsTimeout != 0 && millisecondsTimeout != -1) {
            num = getTickCount();
        }

        SpinWait spinWait;
        while (!condition()) {
            if (millisecondsTimeout == 0) {
                return false;
            }

            spinWait.spinOnce();

            if (millisecondsTimeout != 1 && spinWait.nextSpinWillYield() && millisecondsTimeout <= (getTickCount() - num)) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] static std::int64_t getTickCount() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    }

private:
    static void spinWaitInternal(std::int32_t iterationCount) {
        for (auto i = 0; i < iterationCount; i++) {
            yieldProcessor();
        }
    }
    static void yieldProcessor() {
        asm volatile("rep\n"
                     "nop");
    }
};

} // namespace opencmw::disruptor
