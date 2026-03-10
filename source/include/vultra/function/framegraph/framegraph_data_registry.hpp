#pragma once

#include <fg/FrameGraphResource.hpp>

#include <cstdint>
#include <unordered_map>

namespace vultra
{
    struct FrameGraphResourceKey
    {
        uint64_t id;
    };

    class FrameGraphDataRegistry
    {
    public:
        void set(FrameGraphResourceKey key, FrameGraphResource r) { m_Data[key.id] = r; }

        FrameGraphResource get(FrameGraphResourceKey key) const { return m_Data.at(key.id); }

    private:
        std::unordered_map<uint64_t, FrameGraphResource> m_Data;
    };
} // namespace vultra