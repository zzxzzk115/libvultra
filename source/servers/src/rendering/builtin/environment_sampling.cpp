#include <vultra/servers/rendering/builtin/environment_sampling.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        constexpr double pi              = std::numbers::pi_v<double>;
        constexpr float  uniformFraction = 0.05f;

        double solidAngle(Extent size, uint32_t row)
        {
            return 2 * pi / size.width * (std::cos(pi * row / size.height) - std::cos(pi * (row + 1) / size.height));
        }

        void validateSample(float value)
        {
            if (!std::isfinite(value) || value < 0 || value >= 1)
            {
                throw std::invalid_argument("Environment sample must be finite and in [0, 1)");
            }
        }
    } // namespace

    EnvironmentDistribution::EnvironmentDistribution(const Image& image) :
        m_Size(image.size)
    {
        validateImage(image);
        const auto count = image.rgba.size() / 4;
        if (count > UINT32_MAX)
        {
            throw std::invalid_argument("Environment distribution exceeds uint32 indexing");
        }
        std::vector<double> weights(count);
        double              sum = 0;
        for (size_t i = 0; i < count; ++i)
        {
            const auto* rgb = image.rgba.data() + i * 4;
            if (rgb[0] < 0 || rgb[1] < 0 || rgb[2] < 0)
            {
                throw std::invalid_argument("Environment radiance cannot be negative");
            }
            weights[i] =
                (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) * solidAngle(m_Size, uint32_t(i / m_Size.width));
            sum += weights[i];
        }
        if (sum == 0)
        {
            for (size_t i = 0; i < count; ++i)
            {
                weights[i] = solidAngle(m_Size, uint32_t(i / m_Size.width));
            }
            sum = 4 * pi;
        }
        m_Cells.resize(count);
        std::vector<uint32_t> small;
        std::vector<uint32_t> large;
        for (uint32_t i = 0; i < count; ++i)
        {
            weights[i] *= double(count) / sum;
            (weights[i] < 1 ? small : large).push_back(i);
            m_Cells[i] = {1, i, 0, 0};
        }
        while (!small.empty() && !large.empty())
        {
            const auto lower = small.back();
            small.pop_back();
            const auto upper = large.back();
            large.pop_back();
            m_Cells[lower].probability = float(weights[lower]);
            m_Cells[lower].alias       = upper;
            weights[upper]             = weights[upper] + weights[lower] - 1;
            (weights[upper] < 1 ? small : large).push_back(upper);
        }
        // Derive PDFs from the actual stored float alias probabilities, not pre-rounding weights.
        std::vector<double> masses(count);
        for (const auto& cell : m_Cells)
        {
            const auto index = size_t(&cell - m_Cells.data());
            masses[index] += double(cell.probability) / count;
            masses[cell.alias] += (1.0 - double(cell.probability)) / count;
        }
        for (size_t i = 0; i < count; ++i)
        {
            m_Cells[i].density = float(masses[i] / solidAngle(m_Size, uint32_t(i / m_Size.width)));
        }
    }

    std::span<const EnvironmentAlias> EnvironmentDistribution::cells() const
    {
        return m_Cells;
    }

    float EnvironmentDistribution::pdf(glm::vec3 direction) const
    {
        const auto length = std::sqrt(glm::dot(direction, direction));
        if (!std::isfinite(length) || length == 0)
        {
            throw std::invalid_argument("Environment PDF requires a finite nonzero direction");
        }
        direction /= length;
        double u = std::atan2(direction.z, direction.x) / (2 * pi) + 0.5;
        u -= std::floor(u);
        const double v = std::acos(std::clamp(double(direction.y), -1.0, 1.0)) / pi;
        const auto   x = std::min(uint32_t(u * m_Size.width), m_Size.width - 1);
        const auto   y = std::min(uint32_t(v * m_Size.height), m_Size.height - 1);
        return (1 - uniformFraction) * m_Cells[size_t(y) * m_Size.width + x].density + uniformFraction / float(4 * pi);
    }

    glm::vec3
    EnvironmentDistribution::sample(float technique, float cell, float alias, float longitude, float latitude) const
    {
        for (float value : {technique, cell, alias, longitude, latitude})
        {
            validateSample(value);
        }
        double phi;
        double cosine;
        if (technique < uniformFraction)
        {
            phi    = (double(longitude) - 0.5) * 2 * pi;
            cosine = 1 - 2 * double(latitude);
        }
        else
        {
            auto index = std::min(size_t(cell * m_Cells.size()), m_Cells.size() - 1);
            if (alias >= m_Cells[index].probability)
            {
                index = m_Cells[index].alias;
            }
            const auto x     = index % m_Size.width;
            const auto y     = index / m_Size.width;
            phi              = ((double(x) + longitude) / m_Size.width - 0.5) * 2 * pi;
            const auto upper = std::cos(pi * y / m_Size.height);
            const auto lower = std::cos(pi * (y + 1) / m_Size.height);
            cosine           = upper + (lower - upper) * latitude;
        }
        const auto sine = std::sqrt(std::max(0.0, 1 - cosine * cosine));
        return {float(std::cos(phi) * sine), float(cosine), float(std::sin(phi) * sine)};
    }
} // namespace vultra
