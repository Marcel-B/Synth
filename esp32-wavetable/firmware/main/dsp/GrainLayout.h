#pragma once

#include <array>
#include <cstdint>
#include <cstring>

/**
 * How Tonwerk Granular's sources lie in the partition "tables": a 4096-byte header sector, then the 16-bit full-rate
 * samples of the nine sources one after the other. Version 1 (what the image flashes) has nine sources of 4 s each.
 * Version 2 (written by the updater's page with the user's own sounds) names each source's length and name in the
 * header sector, the lengths summing to at most the nine times 4 s of version 1, so the lower copies still fit the
 * PSRAM. Plain C++: the firmware, the updater and the host test read it alike.
 */
namespace twm::grain::layout
{
inline constexpr uint32_t kMagic = 0x52475754; // "TWGR"
inline constexpr uint32_t kVersionFixed = 1;
inline constexpr uint32_t kVersionOwn = 2;
inline constexpr int kCount = 9;
/** 4 s at 48 kHz, every source of version 1. */
inline constexpr uint32_t kFixedSamples = 192000;
/** All sources together: 36 s, what the PSRAM holds with the lower copies. */
inline constexpr uint32_t kBudget = kCount * kFixedSamples;
/** Half a second: shorter makes little sense for grains, and every lower copy keeps some samples. */
inline constexpr uint32_t kMinSamples = 24000;
inline constexpr uint32_t kHeaderBytes = 4096;
inline constexpr int kNameBytes = 32;

/** The header sector as written: magic, version, bytes of samples after the sector; version 2 adds the lengths in
 *  samples and the names (UTF-8, zero padded). */
struct Header
{
    uint32_t magic = kMagic;
    uint32_t version = kVersionOwn;
    uint32_t bytes = 0;
    std::array<uint32_t, kCount> lengths {};
    std::array<std::array<char, kNameBytes>, kCount> names {};
};
static_assert(sizeof(Header) <= kHeaderBytes);

/** The lengths the header describes, or false when it is no valid set of sources. */
inline bool lengthsOf(const Header& h, std::array<uint32_t, kCount>& lengths)
{
    if (h.magic != kMagic)
        return false;
    if (h.version == kVersionFixed)
    {
        lengths.fill(kFixedSamples);
        return h.bytes == kBudget * sizeof(int16_t);
    }
    if (h.version != kVersionOwn)
        return false;
    uint64_t total = 0;
    for (const uint32_t n : h.lengths)
    {
        if (n < kMinSamples || n > kBudget)
            return false;
        total += n;
    }
    if (total > kBudget || h.bytes != total * sizeof(int16_t))
        return false;
    lengths = h.lengths;
    return true;
}

/** Reads the header from the start of the header sector (the sector's bytes past a version 1 header may be 0xff). */
inline Header readHeader(const uint8_t* sector)
{
    Header h {};
    std::memcpy(&h.magic, sector, 12);
    if (h.version == kVersionOwn)
        std::memcpy(&h, sector, sizeof h);
    return h;
}
} // namespace twm::grain::layout
