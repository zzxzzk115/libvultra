#pragma once

#include <vultra/assets/texture_data.hpp>
#include <vultra/drivers/rhi/resources.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace vultra
{
    class AssetSource;
    class ShaderProgram;

    enum class EnvironmentPrecision
    {
        eFloat32,
        // Quantize to RGBA16F when magnitudes fit its finite range (small values may round to zero);
        // retain RGBA32F for brighter HDR inputs.
        ePreferFloat16
    };

    // Empty input selects the analytic studio environment; nonempty input must be Radiance HDR bytes.
    std::expected<TextureData, std::string>
    decodeEnvironmentExpected(std::span<const std::byte> bytes,
                              EnvironmentPrecision       precision = EnvironmentPrecision::eFloat32);

    // Latitude-longitude HDR environment. GPU preprocessing runs once at construction.
    class Environment
    {
    public:
        // An empty path selects the built-in analytic studio environment.
        // Sources are borrowed through setSource()/rebuild(); an explicit shader source supplies cooked programs.
        explicit Environment(Device&                      device,
                             const std::filesystem::path& hdr     = {},
                             const AssetSource*           source  = nullptr,
                             const AssetSource*           shaders = nullptr);
        // A null program creates only radiance for sky rendering. Otherwise preprocessing waits for completion.
        static std::expected<std::unique_ptr<Environment>, std::string>
        create(Device& device, const TextureData& radiance, const ShaderProgram* program = nullptr);
        // Previous GPU use must be complete. Failure keeps existing textures; an unchanged path is a no-op.
        void                         setSource(const std::filesystem::path& hdr);
        const std::filesystem::path& source() const;
        void rebuild(); // Reprocess the current source after its contents change; prior GPU use must be complete.
        std::unique_ptr<Texture> radiance;
        std::unique_ptr<Texture> diffuse;
        std::unique_ptr<Texture> specular;
        std::unique_ptr<Texture> brdfLut;

    private:
        struct Uninitialized
        {};

        Environment(Device& device, Uninitialized) :
            m_Device(device),
            m_AssetSource(nullptr),
            m_ShaderSource(nullptr)
        {
        }

        void                  replace(const std::filesystem::path& hdr);
        Device&               m_Device;
        std::filesystem::path m_Source;
        const AssetSource*    m_AssetSource;
        const AssetSource*    m_ShaderSource;
    };
} // namespace vultra
