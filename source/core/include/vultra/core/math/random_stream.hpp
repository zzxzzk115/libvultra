#pragma once

#include <vultra/core/base/error.hpp>

#include <cstdint>
#include <expected>

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
        uint32_t nextUInt();
        // Uniform [0, bound); zero is invalid.
        [[nodiscard]] std::expected<uint32_t, Error> nextUInt(uint32_t bound);
        double                                       nextUnit(); // Exactly representable multiples of 2^-32 in [0, 1).
        RandomState                                  state() const;
        [[nodiscard]] std::expected<void, Error>     restore(RandomState state);

    private:
        uint64_t m_State;
        uint64_t m_Increment;
    };
} // namespace vultra
