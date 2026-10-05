#pragma once

#include <vultra/drivers/rhi/resources.hpp>

#include <filesystem>
#include <memory>

namespace vultra
{
    // Latitude-longitude HDR environment. GPU preprocessing runs once at construction.
    class Environment
    {
    public:
        // An empty path selects the built-in analytic studio environment.
        explicit Environment(Device& device, const std::filesystem::path& hdr = {});
        // Previous GPU use must be complete. Failure keeps existing textures; an unchanged path is a no-op.
        void                         setSource(const std::filesystem::path& hdr);
        const std::filesystem::path& source() const;
        void rebuild(); // Reprocess the current source after its contents change; prior GPU use must be complete.
        std::unique_ptr<Texture> radiance;
        std::unique_ptr<Texture> diffuse;
        std::unique_ptr<Texture> specular;
        std::unique_ptr<Texture> brdfLut;

    private:
        void                  replace(const std::filesystem::path& hdr);
        Device&               m_Device;
        std::filesystem::path m_Source;
    };
} // namespace vultra
