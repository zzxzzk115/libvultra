#include <vultra/gpu/device.hpp>
#include <vultra/render/render_light.hpp>

#include <cmath>
#include <cstring>
#include <numbers>
#include <utility>

namespace vultra
{
    static_assert(sizeof(GpuLight) == 64);

    LightBuffer::LightBuffer(Device& device, Buffer buffer) :
        m_Device(device),
        m_Buffer(std::move(buffer))
    {
    }

    std::expected<std::unique_ptr<LightBuffer>, Error> LightBuffer::create(Device& device)
    {
        auto buffer = Buffer::create(device,
                                     VriBufferDesc {sizeof(GpuLight) * kMaxRenderLights,
                                                    sizeof(GpuLight),
                                                    VriBufferUsage_StorageBuffer,
                                                    VriMemoryLocation_HostUpload});
        if (!buffer)
        {
            return std::unexpected(buffer.error());
        }
        std::unique_ptr<LightBuffer> result(new LightBuffer(device, std::move(*buffer)));
        const VriBufferViewDesc      view {result->m_Buffer.handle,
                                           VriDescriptorType_StructuredBuffer,
                                           VriFormat_Unknown,
                                           0,
                                           result->m_Buffer.desc.size};
        if (auto created =
                check(device.core.CreateBufferView(device.handle, &view, &result->m_View), "Create scene light view");
            !created)
        {
            return std::unexpected(created.error());
        }
        return result;
    }

    LightBuffer::~LightBuffer()
    {
        m_Device.waitIdleForDestruction();
        if (m_View)
        {
            m_Device.core.DestroyDescriptor(m_View);
        }
    }

    std::expected<void, Error> LightBuffer::update(std::span<const RenderLight> lights)
    {
        if (lights.size() > kMaxRenderLights)
        {
            return makeError("A frame has more than {} lights", kMaxRenderLights);
        }
        const auto finite = [](glm::vec3 value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        };
        std::array<GpuLight, kMaxRenderLights> packed {};
        int32_t                                mainDirectional = -1;
        for (size_t i = 0; i < lights.size(); ++i)
        {
            const auto& light = lights[i];
            if (light.kind > RenderLightKind::eSpot || !finite(light.position) || !finite(light.directionToLight) ||
                !finite(light.color) || glm::any(glm::lessThan(light.color, glm::vec3(0))) ||
                !std::isfinite(light.intensity) || light.intensity < 0 || !std::isfinite(light.range) ||
                light.range <= 0 || !std::isfinite(light.innerCone) || !std::isfinite(light.outerCone) ||
                light.innerCone < 0 || light.innerCone >= light.outerCone ||
                light.outerCone >= std::numbers::pi_v<float> * 0.5f)
            {
                return makeError("Invalid render light at index {}", i);
            }
            auto direction = glm::vec3(0, 0, 1);
            if (light.kind != RenderLightKind::ePoint)
            {
                const auto length = glm::length(light.directionToLight);
                if (length == 0)
                {
                    return makeError("Render light direction is singular at index {}", i);
                }
                direction = light.directionToLight / length;
            }
            packed[i] = {{light.position, light.range},
                         {direction, float(light.kind)},
                         {light.color, light.intensity},
                         {std::cos(light.innerCone), std::cos(light.outerCone), 0, 0}};
            if (light.kind == RenderLightKind::eDirectional && mainDirectional < 0)
            {
                mainDirectional = int32_t(i);
            }
        }
        auto* mapped = m_Device.core.MapBuffer(m_Buffer.handle, 0, sizeof(packed));
        if (!mapped)
        {
            return makeError("Map scene light buffer");
        }
        std::memcpy(mapped, packed.data(), sizeof(packed));
        m_Device.core.UnmapBuffer(m_Buffer.handle);
        m_Lights          = packed;
        m_Count           = uint32_t(lights.size());
        m_MainDirectional = mainDirectional;
        return {};
    }

    Buffer& LightBuffer::buffer()
    {
        return m_Buffer;
    }

    VriDescriptor* LightBuffer::view() const
    {
        return m_View;
    }

    std::span<const GpuLight> LightBuffer::lights() const
    {
        return std::span(m_Lights).first(m_Count);
    }

    int32_t LightBuffer::mainDirectional() const
    {
        return m_MainDirectional;
    }
} // namespace vultra
