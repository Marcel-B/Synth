#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

/** How loud a stereo take comes across, for the tests that level factory sounds. 48 kHz only. */
namespace tonwerktest
{
/**
 * ITU-R BS.1770's K-weighting at 48 kHz, its published coefficients: a shelf of about +4 dB above 2 kHz and a
 * highpass near 40 Hz, so a bass counts about as loud as it sounds rather than by its energy alone.
 */
inline std::vector<double> kWeighted(const std::vector<float>& signal)
{
    struct Biquad
    {
        double b0, b1, b2, a1, a2;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        double process(double x)
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
    Biquad highpass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
    std::vector<double> out(signal.size());
    for (std::size_t i = 0; i < signal.size(); ++i)
        out[i] = highpass.process(shelf.process(signal[i]));
    return out;
}

/** EBU R128's momentary loudness (400 ms, every 100 ms) at its loudest, in LUFS: how loud a sound comes across. */
inline double momentaryMax(const std::vector<float>& leftSignal, const std::vector<float>& rightSignal)
{
    constexpr double rate = 48000.0;
    const auto left = kWeighted(leftSignal);
    const auto right = kWeighted(rightSignal);
    const auto window = (std::size_t) (0.4 * rate);
    const auto hop = (std::size_t) (0.1 * rate);
    double loudest = 0.0;
    for (std::size_t at = 0; at + window <= left.size(); at += hop)
    {
        double sum = 0.0;
        for (std::size_t i = at; i < at + window; ++i)
            sum += left[i] * left[i] + right[i] * right[i];
        loudest = std::max(loudest, sum / (double) window);
    }
    return -0.691 + 10.0 * std::log10(std::max(loudest, 1.0e-12));
}
} // namespace tonwerktest
