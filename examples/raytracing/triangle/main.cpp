#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/raytracing_pipeline.hpp>
#include <vultra/core/rhi/render_device.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>

using namespace vultra;

struct SimpleVertex
{
    glm::vec3 position;
};

constexpr auto kTriangle = std::array {
    SimpleVertex {{0.0f, -0.5f, 0.0f}},
    SimpleVertex {{-0.5f, 0.5f, 0.0f}},
    SimpleVertex {{0.5f, 0.5f, 0.0f}},
};

constexpr auto kIndices = std::array {0u, 1u, 2u};
constexpr glm::mat4 kTransform {1.0f};

const char* const raygenCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_shader_image_load_formatted : enable

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0) uniform image2D image;

layout(location = 0) rayPayloadEXT vec3 hitValue;

void main()
{
    vec3 origin = vec3(0.0, 0.0, -2.0);
    vec2 uv = (vec2(gl_LaunchIDEXT.xy) + 0.5) / vec2(gl_LaunchSizeEXT.xy);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 target = vec3(ndc, 0.0);
    vec3 direction = normalize(target - origin);
    hitValue = vec3(0.0);
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, origin, 0.001, direction, 10000.0, 0);
    imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 0.0));
}
)";

const char* const missCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

layout(push_constant) uniform PushConstants
{
    vec4 missColor;
};

void main()
{
    hitValue = missColor.rgb;
}
)";

const char* const closestHitCode = R"(
#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

void main()
{
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    hitValue = barycentricCoords;
}
)";

int main()
try
{
    auto window = os::Window::Builder {}.setExtent({1024, 768}).setTitle("Raytracing Triangle Example").build();

    rhi::RenderDevice renderDevice(
        rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline,
        "Raytracing Triangle Example",
        window->getRequiredVulkanInstanceExtensions());

    VULTRA_CLIENT_INFO("RenderDevice Name: {}", renderDevice.getName());
    window->setTitle(std::format("Raytracing Triangle ({})", renderDevice.getName()));

    auto swapchain = renderDevice.createSwapchain(*window, rhi::SwapchainFormat::esRGB, rhi::VerticalSync::eEnabled);
    rhi::FrameController frameController {renderDevice, swapchain, 3};

    auto vertexBuffer = renderDevice.createVertexBuffer(sizeof(SimpleVertex), kTriangle.size());
    renderDevice.uploadS(vertexBuffer, 0, sizeof(SimpleVertex) * kTriangle.size(), kTriangle.data());
    const auto vertexBufferAddress = renderDevice.getBufferDeviceAddress(vertexBuffer);

    auto indexBuffer = renderDevice.createIndexBuffer(rhi::IndexType::eUInt32, kIndices.size());
    renderDevice.uploadS(indexBuffer, 0, sizeof(uint32_t) * kIndices.size(), kIndices.data());
    const auto indexBufferAddress = renderDevice.getBufferDeviceAddress(indexBuffer);

    auto transformBuffer = renderDevice.createTransformBuffer();
    auto rowMajor = glm::transpose(kTransform);
    void* mapped = transformBuffer.map();
    std::memcpy(mapped, &rowMajor, sizeof(rowMajor));
    transformBuffer.unmap();
    const auto transformBufferAddress = renderDevice.getBufferDeviceAddress(transformBuffer);

    auto blas = renderDevice.createBuildSingleGeometryBLAS(vertexBufferAddress,
                                                           indexBufferAddress,
                                                           transformBufferAddress,
                                                           sizeof(SimpleVertex),
                                                           static_cast<uint32_t>(kTriangle.size()),
                                                           static_cast<uint32_t>(kIndices.size()));
    auto tlas = renderDevice.createBuildSingleInstanceTLAS(blas, kTransform);

    auto pipeline = rhi::RayTracingPipeline::Builder {}
                        .setMaxRecursionDepth(1)
                        .addShader(rhi::ShaderType::eRayGen, {.code = raygenCode})
                        .addShader(rhi::ShaderType::eMiss, {.code = missCode})
                        .addShader(rhi::ShaderType::eClosestHit, {.code = closestHitCode})
                        .addRaygenGroup(0)
                        .addMissGroup(1)
                        .addHitGroup(2)
                        .build(renderDevice);

    auto sbt = renderDevice.createShaderBindingTable(pipeline);

    auto createOutputImage = [&renderDevice](const rhi::Extent2D extent) {
        return rhi::Texture::Builder {}
            .setExtent(extent)
            .setPixelFormat(rhi::PixelFormat::eRGBA16F)
            .setNumMipLevels(1)
            .setNumLayers(std::nullopt)
            .setUsageFlags(rhi::ImageUsage::eStorage | rhi::ImageUsage::eTransferSrc)
            .setupOptimalSampler(false)
            .build(renderDevice);
    };

    auto outputImage = createOutputImage(swapchain.getCurrentBuffer().getExtent());

    while (!window->shouldClose())
    {
        window->pollEvents();

        if (!swapchain || !frameController.acquireNextFrame())
            continue;

        auto& backBuffer = swapchain.getCurrentBuffer();
        if (outputImage.getExtent() != backBuffer.getExtent())
            outputImage = createOutputImage(backBuffer.getExtent());

        auto& cb = frameController.beginFrame();

        rhi::prepareForRaytracing(cb, outputImage);
        auto descriptorSet =
            cb.createDescriptorSetBuilder()
                .bind(0, rhi::bindings::AccelerationStructureKHR {.as = &tlas})
                .bind(1, rhi::bindings::StorageImage {.texture = &outputImage, .imageAspect = rhi::ImageAspect::eColor})
                .build(pipeline.getDescriptorSetLayout(0));

        const glm::vec4 missColor {0.2f, 0.3f, 0.3f, 1.0f};
        cb.bindPipeline(pipeline)
            .bindDescriptorSet(0, descriptorSet)
            .pushConstants(rhi::ShaderStages::eMiss, 0, &missColor)
            .traceRays(sbt, {window->getFrameBufferExtent(), 1});

        cb.blit(outputImage, backBuffer, rhi::TexelFilter::eLinear);

        frameController.endFrame();
        frameController.present();
    }

    renderDevice.waitIdle();
    return 0;
}
catch (const std::exception& e)
{
    VULTRA_CLIENT_CRITICAL("Exception: {}", e.what());
    return 1;
}
