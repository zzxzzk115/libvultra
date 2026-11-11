#include <vultra/core/math/random_stream.hpp>

#include <pcg_basic.h>

namespace vultra
{
    RandomStream::RandomStream(uint64_t seed, uint64_t sequence)
    {
        pcg32_random_t value {};
        pcg32_srandom_r(&value, seed, sequence);
        m_State     = value.state;
        m_Increment = value.inc;
    }

    uint32_t RandomStream::nextUInt()
    {
        pcg32_random_t value {m_State, m_Increment};
        const auto     result = pcg32_random_r(&value);
        m_State               = value.state;
        return result;
    }

    std::expected<uint32_t, Error> RandomStream::nextUInt(uint32_t bound)
    {
        if (bound == 0)
        {
            return makeError("Random integer bound must be positive");
        }
        pcg32_random_t value {m_State, m_Increment};
        const auto     result = pcg32_boundedrand_r(&value, bound);
        m_State               = value.state;
        return result;
    }

    double RandomStream::nextUnit()
    {
        return double(nextUInt()) * 0x1p-32;
    }

    RandomState RandomStream::state() const
    {
        return {m_State, m_Increment};
    }

    std::expected<void, Error> RandomStream::restore(RandomState state)
    {
        if ((state.increment & 1) == 0)
        {
            return makeError("PCG32 stream increment must be odd");
        }
        m_State     = state.state;
        m_Increment = state.increment;
        return {};
    }
} // namespace vultra
