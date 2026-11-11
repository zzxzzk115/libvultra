#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/gpu/resources.hpp>

#include <expected>
#include <filesystem>
#include <memory>

namespace vultra
{
    class AssetSource;

    // Latitude-longitude HDR environment. GPU preprocessing runs once at construction.
    class Environment
    {
    public:
        // An empty path selects the built-in analytic studio environment.
        // Sources are borrowed through setSource()/rebuild(); an explicit shader source supplies cooked programs.
        // Render paths borrow the environment by address, so it lives behind a unique_ptr.
        [[nodiscard]] static std::expected<std::unique_ptr<Environment>, Error>
        create(Device&                      device,
               const std::filesystem::path& hdr     = {},
               const AssetSource*           source  = nullptr,
               const AssetSource*           shaders = nullptr);
        Environment(const Environment&)            = delete;
        Environment& operator=(const Environment&) = delete;
        // Previous GPU use must be complete. Failure keeps existing textures; an unchanged path is a no-op.
        [[nodiscard]] std::expected<void, Error> setSource(const std::filesystem::path& hdr);
        const std::filesystem::path&             source() const;
        // Reprocess the current source after its contents change; prior GPU use must be complete.
        [[nodiscard]] std::expected<void, Error> rebuild();
        std::unique_ptr<Texture>                 radiance;
        std::unique_ptr<Texture>                 diffuse;
        std::unique_ptr<Texture>                 specular;
        std::unique_ptr<Texture>                 brdfLut;

    private:
        Environment(Device&                      device,
                    const std::filesystem::path& hdr,
                    const AssetSource*           source,
                    const AssetSource*           shaders);
        std::expected<void, Error> generate();
        std::expected<void, Error> replace(const std::filesystem::path& hdr);

        Device&               m_Device;
        std::filesystem::path m_Source;
        const AssetSource*    m_AssetSource;
        const AssetSource*    m_ShaderSource;
    };
} // namespace vultra
