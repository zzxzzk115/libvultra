#pragma once

#include "vultra/core/rhi/buffer.hpp"
#include "vultra/core/rhi/stride_device_address_region.hpp"

namespace vultra
{
    namespace rhi
    {
        class ShaderBindingTable : public Buffer
        {
        public:
            ShaderBindingTable() = default;

            struct Regions
            {
                StrideDeviceAddressRegion                raygen;
                StrideDeviceAddressRegion                miss;
                StrideDeviceAddressRegion                hit;
                std::optional<StrideDeviceAddressRegion> callable; // Optional
            };

            [[nodiscard]] const Regions& regions() const { return m_Regions; }

        private:
            friend class RenderDevice;
            ShaderBindingTable(Buffer&& buffer, Regions&& regions) :
                Buffer(std::move(buffer)), m_Regions(std::move(regions))
            {}

            Regions m_Regions;
        };
    } // namespace rhi
} // namespace vultra