#pragma once

#include <vultra/core/rhi/resources.hpp>

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
        std::unique_ptr<Texture> radiance;
        std::unique_ptr<Texture> diffuse;
        std::unique_ptr<Texture> specular;
        std::unique_ptr<Texture> brdfLut;
    };
} // namespace vultra
