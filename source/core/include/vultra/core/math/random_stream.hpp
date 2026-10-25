#pragma once

#include <cstdint>

namespace vultra
{
    struct RandomState
    {
        uint64_t state;
        uint64_t increment;

        friend bool operator==(RandomState, RandomState) = default;
    };

    // Owned PCG32 stream with explicit, portable state. No process-global RNG is used.
    class RandomStream
    {
    public:
        explicit RandomStream(uint64_t seed = 0, uint64_t sequence = 0);
        uint32_t    nextUInt();
        uint32_t    nextUInt(uint32_t bound); // Uniform [0, bound); zero is invalid.
        double      nextUnit();               // Exactly representable multiples of 2^-32 in [0, 1).
        RandomState state() const;
        void        restore(RandomState state);

    private:
        uint64_t m_State;
        uint64_t m_Increment;
    };
} // namespace vultra
