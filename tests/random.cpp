#include <vultra/core/math/random_stream.hpp>

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
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
        value = stream.nextUInt(0x80000001);
        require(value < 0x80000001, "Bounded random output exceeded the exclusive upper bound");
    }
    stream.restore(saved);
    for (const auto value : sequence)
    {
        require(stream.nextUInt(0x80000001) == value, "Restoring PCG state changed subsequent bounded outputs");
    }
    const auto before   = stream.state();
    bool       rejected = false;
    try
    {
        stream.nextUInt(0);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected && stream.state() == before, "Invalid random bounds changed stream state");
    rejected = false;
    try
    {
        stream.restore({0, 2});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected && stream.state() == before, "Invalid PCG state was accepted or changed the live stream");
    RandomStream units(42, 54);
    for (const auto value : expected)
    {
        const auto unit = units.nextUnit();
        require(unit >= 0 && unit < 1 && unit * 4294967296.0 == value,
                "Unit random values differ from the shared 32-bit reference sequence");
    }
    require(stream.nextUInt(1) == 0, "A unit bound returned a nonzero random index");
    std::cout << "PCG32 reference, bounded output, state recovery and exact unit conversion passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
