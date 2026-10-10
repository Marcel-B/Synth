#pragma once

#include "DxEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tonwerkdx
{
struct NamedDxPatch
{
    std::string name;
    DxPatch patch;
};

/**
 * Reads DX7 voice data the user owns: a bank of 32 voices (`F0 43 0n 09 20 00`, 4096 bytes of packed voices), a single
 * voice (`F0 43 0n 00 01 1B`, 155 bytes) or a bare 4096-byte bank without the SysEx frame. What this plugin has no
 * place for is left out: the pitch envelope, keyboard level scaling, the oscillator key sync and fixed frequencies
 * (an operator in fixed mode gets the ratio nearest its frequency at middle C). Returns nothing for anything else.
 */
inline std::vector<NamedDxPatch> readSysex(const std::vector<std::uint8_t>& data)
{
    std::vector<NamedDxPatch> result;
    auto clampTo = [](int value, int high) { return std::clamp(value, 0, high); };

    // One operator from its fields, whichever layout they came from.
    struct OpFields
    {
        int rates[4], levels[4], rs, ams, kvs, ol, mode, fc, ff, det;
    };
    auto toOperator = [&](const OpFields& f) {
        OperatorPatch op;
        for (int i = 0; i < 4; ++i)
        {
            op.rates[(std::size_t) i] = clampTo(f.rates[i], 99);
            op.levels[(std::size_t) i] = clampTo(f.levels[i], 99);
        }
        op.rateScaling = clampTo(f.rs, 7);
        op.ampModSens = clampTo(f.ams, 3);
        op.velocity = clampTo(f.kvs, 7);
        op.level = clampTo(f.ol, 99);
        op.detune = clampTo(f.det, 14) - 7;
        if (f.mode == 0)
        {
            op.coarse = clampTo(f.fc, 31);
            op.fine = clampTo(f.ff, 99);
        }
        else
        {
            // Fixed: 1, 10, 100 or 1000 Hz times up to ten, as a ratio to middle C.
            const double hz = std::pow(10.0, f.fc % 4) * std::exp(clampTo(f.ff, 99) / 99.0 * std::log(9.772));
            const double ratio = std::clamp(hz / 261.63, 0.5, 31.0 * 1.99);
            op.coarse = ratio < 1.0 ? 0 : std::min(31, (int) ratio);
            op.fine = (int) std::lround(std::clamp((ratio / ratioOf(op.coarse, 0) - 1.0) * 100.0, 0.0, 99.0));
        }
        return op;
    };
    auto nameOf = [](const std::uint8_t* bytes) {
        std::string name;
        for (int i = 0; i < 10; ++i)
            name += (bytes[i] >= 32 && bytes[i] < 127) ? (char) bytes[i] : ' ';
        while (! name.empty() && name.back() == ' ')
            name.pop_back();
        while (! name.empty() && name.front() == ' ')
            name.erase(name.begin());
        return name.empty() ? std::string("DX") : name;
    };

    auto packedVoice = [&](const std::uint8_t* v) {
        NamedDxPatch named;
        auto& p = named.patch;
        for (int k = 0; k < kOperators; ++k)
        {
            const std::uint8_t* o = v + k * 17;
            OpFields f {};
            for (int i = 0; i < 4; ++i)
            {
                f.rates[i] = o[i];
                f.levels[i] = o[4 + i];
            }
            f.rs = o[12] & 7;
            f.det = (o[12] >> 3) & 15;
            f.ams = o[13] & 3;
            f.kvs = (o[13] >> 2) & 7;
            f.ol = o[14];
            f.mode = o[15] & 1;
            f.fc = (o[15] >> 1) & 31;
            f.ff = o[16];
            // Stored from operator 6 down to 1.
            p.ops[(std::size_t) (kOperators - 1 - k)] = toOperator(f);
        }
        p.algorithm = v[110] & 31;
        p.feedback = v[111] & 7;
        p.lfoSpeed = clampTo(v[112], 99);
        p.lfoDelay = clampTo(v[113], 99);
        p.lfoPitchDepth = clampTo(v[114], 99);
        p.lfoAmpDepth = clampTo(v[115], 99);
        p.lfoWave = (LfoWave) clampTo((v[116] >> 1) & 7, 5);
        p.lfoPitchSens = (v[116] >> 4) & 7;
        p.transpose = std::clamp(v[117] - 24, -24, 24);
        named.name = nameOf(v + 118);
        return named;
    };

    auto singleVoice = [&](const std::uint8_t* v) {
        NamedDxPatch named;
        auto& p = named.patch;
        for (int k = 0; k < kOperators; ++k)
        {
            const std::uint8_t* o = v + k * 21;
            OpFields f {};
            for (int i = 0; i < 4; ++i)
            {
                f.rates[i] = o[i];
                f.levels[i] = o[4 + i];
            }
            f.rs = o[13];
            f.ams = o[14];
            f.kvs = o[15];
            f.ol = o[16];
            f.mode = o[17];
            f.fc = o[18];
            f.ff = o[19];
            f.det = o[20];
            p.ops[(std::size_t) (kOperators - 1 - k)] = toOperator(f);
        }
        p.algorithm = clampTo(v[134], 31);
        p.feedback = clampTo(v[135], 7);
        p.lfoSpeed = clampTo(v[137], 99);
        p.lfoDelay = clampTo(v[138], 99);
        p.lfoPitchDepth = clampTo(v[139], 99);
        p.lfoAmpDepth = clampTo(v[140], 99);
        p.lfoWave = (LfoWave) clampTo(v[142], 5);
        p.lfoPitchSens = clampTo(v[143], 7);
        p.transpose = std::clamp((int) v[144] - 24, -24, 24);
        named.name = nameOf(v + 145);
        return named;
    };

    const auto size = data.size();
    if (size >= 4104 && data[0] == 0xF0 && data[1] == 0x43 && data[3] == 0x09)
    {
        for (int i = 0; i < 32; ++i)
            result.push_back(packedVoice(data.data() + 6 + i * 128));
    }
    else if (size == 4096)
    {
        for (int i = 0; i < 32; ++i)
            result.push_back(packedVoice(data.data() + i * 128));
    }
    else if (size >= 163 && data[0] == 0xF0 && data[1] == 0x43 && data[3] == 0x00)
    {
        result.push_back(singleVoice(data.data() + 6));
    }
    return result;
}
} // namespace tonwerkdx
