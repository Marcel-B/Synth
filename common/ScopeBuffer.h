#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace tonwerkui
{
/**
 * The last samples a plugin played, for an oscilloscope: written by the audio thread, read by the editor's timer.
 * Atomics per sample, so the reader never sees a torn value; a trace that mixes two blocks is fine for a picture.
 */
class ScopeBuffer
{
public:
    static constexpr int kSize = 4096;

    void write(const float* samples, int count)
    {
        int at = position.load(std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
        {
            data[(std::size_t) at].store(samples[i], std::memory_order_relaxed);
            at = (at + 1) % kSize;
        }
        position.store(at, std::memory_order_release);
    }

    /**
     * The newest `count` samples, oldest first, leaving out the newest `lag`: an effect's input read `lag` behind
     * lines up with its output when `lag` is the effect's latency.
     */
    void read(float* out, int count, int lag = 0) const
    {
        const int end = position.load(std::memory_order_acquire) - lag;
        for (int i = 0; i < count; ++i)
            out[i] = data[(std::size_t) (((end - count + i) % kSize + kSize) % kSize)].load(std::memory_order_relaxed);
    }

private:
    std::array<std::atomic<float>, kSize> data {};
    std::atomic<int> position { 0 };
};
} // namespace tonwerkui
