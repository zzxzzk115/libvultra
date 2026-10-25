#include <vultra/core/math/projection.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void requireNear(float actual, float expected, const char* message)
    {
        if (std::abs(actual - expected) > 0.00001f)
        {
            throw std::runtime_error(message);
        }
    }

    void projectionY()
    {
        auto original            = glm::frustumRH_ZO(-0.7f, 0.9f, -0.4f, 0.6f, 0.1f, 25.0f);
        original[0][1]           = 0.3f;
        original[3][1]           = 0.2f;
        const auto       flipped = vultra::flipProjectionY(original);
        const std::array points {
            glm::vec4(0.2f, 0.4f, -2.0f, 1.0f),
            glm::vec4(-0.5f, -0.3f, -8.0f, 1.0f),
            glm::vec4(0.1f, 0.8f, -0.1f, 1.0f),
        };
        for (const auto& point : points)
        {
            const auto before = original * point;
            const auto after  = flipped * point;
            requireNear(after.x, before.x, "Projection Y flip changed clip X");
            requireNear(after.y, -before.y, "Projection Y flip missed an off-center term");
            requireNear(after.z, before.z, "Projection Y flip changed clip Z");
            requireNear(after.w, before.w, "Projection Y flip changed clip W");
        }
        if (vultra::flipProjectionY(flipped) != original)
        {
            throw std::runtime_error("Two Y flips did not restore the projection");
        }
    }

    void projectionDepth()
    {
        constexpr float nearPlane = 0.1f;
        constexpr float farPlane  = 25.0f;
        const auto      original  = glm::frustumRH_NO(-0.7f, 0.9f, -0.4f, 0.6f, nearPlane, farPlane);
        const auto      zeroOne   = vultra::projectionDepthMinusOneToZeroOne(original);
        const auto      reference = glm::frustumRH_ZO(-0.7f, 0.9f, -0.4f, 0.6f, nearPlane, farPlane);
        const auto      restored  = vultra::projectionDepthZeroOneToMinusOne(zeroOne);
        for (const float depth : {nearPlane, 2.0f, farPlane})
        {
            const glm::vec4 point {0.2f, -0.3f, -depth, 1.0f};
            const auto      before   = original * point;
            const auto      after    = zeroOne * point;
            const auto      expected = reference * point;
            const auto      back     = restored * point;
            requireNear(after.x, before.x, "Depth conversion changed clip X");
            requireNear(after.y, before.y, "Depth conversion changed clip Y");
            requireNear(after.w, before.w, "Depth conversion changed clip W");
            requireNear(after.z / after.w, expected.z / expected.w, "Depth conversion differs from GLM ZO");
            requireNear(back.z / back.w, before.z / before.w, "Depth conversion round trip failed");
        }
        const auto nearClip = zeroOne * glm::vec4(0, 0, -nearPlane, 1);
        const auto farClip  = zeroOne * glm::vec4(0, 0, -farPlane, 1);
        requireNear(nearClip.z / nearClip.w, 0, "Near plane did not map to zero");
        requireNear(farClip.z / farClip.w, 1, "Far plane did not map to one");
    }
} // namespace

int main()
{
    try
    {
        projectionY();
        projectionDepth();
        std::cout << "Projection Y and depth conversion checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
