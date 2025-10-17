#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/core/rhi/command_buffer.hpp>
#include <vultra/core/rhi/frame_controller.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/render_device.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>

using namespace vultra;

int main()
try
{
    os::Window window = os::Window::Builder {}.setExtent({1024, 768}).build();

    // Event callback
    window.on<os::GeneralWindowEvent>([](const os::GeneralWindowEvent& event, os::Window& wd) {
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            // Press ESC to close the window
            if (event.internalEvent.key.key == SDLK_ESCAPE)
            {
                wd.close();
            }
        }
    });

    rhi::RenderDevice renderDevice(rhi::RenderDeviceFeatureFlagBits::eMeshShader);

    VULTRA_CLIENT_INFO("RenderDevice Name: {}", renderDevice.getName());
    VULTRA_CLIENT_INFO("RenderDevice PhysicalDeviceInfo: {}", renderDevice.getPhysicalDeviceInfo().toString());

    VULTRA_CLIENT_WARN("Press ESC to close the window");

    window.setTitle(std::format("MeshShading Triangle ({})", renderDevice.getName()));

    // Create swapchain
    rhi::Swapchain swapchain = renderDevice.createSwapchain(window);

    // Create frame controller
    rhi::FrameController frameController {renderDevice, swapchain, 3};

    const auto* const meshCode = R"(
#version 460 core
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 3, max_primitives = 1) out;

layout(location = 0) out VertexOutput
{
	vec4 color;
} vertexOutput[];

const vec4[3] positions = {
	vec4( 0.0, -0.5, 0.0, 1.0),
	vec4(-0.5,  0.5, 0.0, 1.0),
	vec4( 0.5,  0.5, 0.0, 1.0)
};

const vec4[3] colors = {
	vec4(0.0, 1.0, 0.0, 1.0),
	vec4(0.0, 0.0, 1.0, 1.0),
	vec4(1.0, 0.0, 0.0, 1.0)
};

void main()
{
	uint iid = gl_LocalInvocationID.x;

	vec4 offset = vec4(0.0, 0.0, gl_GlobalInvocationID.x, 0.0);

	SetMeshOutputsEXT(3, 1);
	gl_MeshVerticesEXT[0].gl_Position = positions[0] + offset;
	gl_MeshVerticesEXT[1].gl_Position = positions[1] + offset;
	gl_MeshVerticesEXT[2].gl_Position = positions[2] + offset;
	vertexOutput[0].color = colors[0];
	vertexOutput[1].color = colors[1];
	vertexOutput[2].color = colors[2];
	gl_PrimitiveTriangleIndicesEXT[gl_LocalInvocationIndex] =  uvec3(0, 1, 2);
}
)";
    const auto* const taskCode = R"(
#version 460 core
#extension GL_EXT_mesh_shader : require

void main()
{
	EmitMeshTasksEXT(3, 1, 1);
}
)";
    const auto* const fragCode = R"(
#version 460 core

layout (location = 0) in VertexInput {
    vec4 color;
} vertexInput;

layout(location = 0) out vec4 outFragColor;

void main()
{
	outFragColor = vertexInput.color;
}
)";

    // Create graphics pipeline
    auto graphicsPipeline = rhi::GraphicsPipeline::Builder {}
                                .setColorFormats({swapchain.getPixelFormat()})
                                .addShader(rhi::ShaderType::eMesh, {.code = meshCode})
                                .addShader(rhi::ShaderType::eTask, {.code = taskCode})
                                .addShader(rhi::ShaderType::eFragment, {.code = fragCode})
                                .setDepthStencil({
                                    .depthTest  = false,
                                    .depthWrite = false,
                                })
                                .setRasterizer({.polygonMode = rhi::PolygonMode::eFill})
                                .setBlending(0, {.enabled = false})
                                .build(renderDevice);

    while (!window.shouldClose())
    {
        window.pollEvents();

        if (!swapchain)
            continue;

        auto& backBuffer        = frameController.getCurrentTarget().texture;
        bool  acquiredNextFrame = frameController.acquireNextFrame();
        if (!acquiredNextFrame)
        {
            continue;
        }

        auto& cb = frameController.beginFrame();

        rhi::prepareForAttachment(cb, backBuffer, false);
        const rhi::FramebufferInfo framebufferInfo {.area             = rhi::Rect2D {.extent = backBuffer.getExtent()},
                                                    .colorAttachments = {
                                                        {
                                                            .target     = &backBuffer,
                                                            .clearValue = glm::vec4 {0.0f, 0.0f, 0.0f, 1.0f},
                                                        },
                                                    }};
        {
            RHI_GPU_ZONE(cb, "MeshShading Triangle");
            cb.beginRendering(framebufferInfo)
                .bindPipeline(graphicsPipeline)
                .drawMeshTask({
                    1,
                    1,
                    1,
                })
                .endRendering();
        }

        frameController.endFrame();
        frameController.present();
    }

    // Remember to wait idle explicitly before any destructors.
    renderDevice.waitIdle();

    return 0;
}
catch (const std::exception& e)
{
    VULTRA_CLIENT_CRITICAL("Exception: {}", e.what());
}