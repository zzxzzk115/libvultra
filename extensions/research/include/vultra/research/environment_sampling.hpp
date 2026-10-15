#pragma once

#include <vultra/core/image/image.hpp>

#include <glm/vec3.hpp>

namespace vultra
{
    struct EnvironmentAlias
    {
        float    probability;
        uint32_t alias;
        float    density; // Solid-angle PDF of the stored alias distribution in this texel.
        float    padding = 0;
    };

    // Latitude-longitude cells use exact solid angles, with a 5% uniform-sphere mixture for full support.
    class EnvironmentDistribution
    {
    public:
        explicit EnvironmentDistribution(const Image& radiance);
        std::span<const EnvironmentAlias> cells() const;
        float                             pdf(glm::vec3 direction) const;
        glm::vec3 sample(float technique, float cell, float alias, float longitude, float latitude) const;

    private:
        Extent                        m_Size;
        std::vector<EnvironmentAlias> m_Cells;
    };
} // namespace vultra
