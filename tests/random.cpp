#include "support/check.hpp"

#include <vultra/core/math/random_stream.hpp>

#include <array>
#include <iostream>

using support::must;
using support::require;

int main()
{
    using namespace vultra;
    // Published PCG demo vector: https://www.pcg-random.org/using-pcg-c-basic.html
    RandomStream                  stream(42, 54);
    const std::array<uint32_t, 6> expected {0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e};
    for (const auto value : expected)
    {
        require(stream.nextUInt() == value, "PCG32 output differs from the published reference vector");
    }
    const auto                saved = stream.state();
    std::array<uint32_t, 100> sequence {};
    for (auto& value : sequence)
    {
        value = must(stream.nextUInt(0x80000001));
        require(value < 0x80000001, "Bounded random output exceeded the exclusive upper bound");
    }
    must(stream.restore(saved));
    for (const auto value : sequence)
    {
        require(must(stream.nextUInt(0x80000001)) == value, "Restoring PCG state changed subsequent bounded outputs");
    }
    const auto before = stream.state();
    support::rejects(stream.nextUInt(0), "Invalid random bounds were accepted");
    require(stream.state() == before, "Invalid random bounds changed stream state");
    support::rejects(stream.restore({0, 2}), "Invalid PCG state was accepted");
    require(stream.state() == before, "Invalid PCG state changed the live stream");
    RandomStream units(42, 54);
    for (const auto value : expected)
    {
        const auto unit = units.nextUnit();
        require(unit >= 0 && unit < 1 && unit * 4294967296.0 == value,
                "Unit random values differ from the shared 32-bit reference sequence");
    }
    require(must(stream.nextUInt(1)) == 0, "A unit bound returned a nonzero random index");
    std::cout << "PCG32 reference, bounded output, state recovery and exact unit conversion passed\n";
    return 0;
}
