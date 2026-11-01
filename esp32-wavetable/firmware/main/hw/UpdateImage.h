#pragma once

#include <algorithm>
#include <cstdint>

/**
 * Where the bytes of a whole image (the .bin flashed at 0x0 over USB) go when it arrives over Wi-Fi, so one file
 * serves both ways. Plain C++ for the host test; the updater (../updater) does the writing.
 */
namespace twm::update
{
enum class Target { skip, partitionTable, app, tables };

struct Region
{
    uint32_t start = 0, size = 0;
    uint32_t end() const { return start + size; }
    bool contains(uint32_t at) const { return at >= start && at - start < size; }
};

/** The module's flash as partitions.csv lays it out. The bootloader, the boot data and the updater itself are not
 *  written: the updater runs from its slot, and the bootloader of every image is the same. */
struct Layout
{
    /** Compared with the module's own, so an image made for another layout is refused. */
    Region partitionTable { 0x8000, 0xc00 };
    /** The instrument: the partition "factory". */
    Region app;
    /** Data an image may carry (Tonwerk Granular's sources), the partition "tables". */
    Region tables;
};

/** One run of bytes going to one place. */
struct Span
{
    Target target = Target::skip;
    /** From the start of that place. */
    uint32_t offset = 0;
    uint32_t length = 0;
};

/** The run of at most `available` bytes from image offset `at` that goes to one place. */
inline Span spanAt(const Layout& layout, uint32_t at, uint32_t available)
{
    const Region* regions[] = { &layout.partitionTable, &layout.app, &layout.tables };
    const Target targets[] = { Target::partitionTable, Target::app, Target::tables };
    uint32_t next = UINT32_MAX;
    for (int i = 0; i < 3; ++i)
    {
        const Region& r = *regions[i];
        if (r.contains(at))
            return { targets[i], at - r.start, std::min(available, r.end() - at) };
        if (r.start > at)
            next = std::min(next, r.start);
    }
    return { Target::skip, 0, std::min(available, next - at) };
}
} // namespace twm::update
