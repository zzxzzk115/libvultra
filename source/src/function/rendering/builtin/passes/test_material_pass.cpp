#include "vultra/function/rendering/builtin/passes/test_material_pass.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/rhi/geometry_info.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_import.hpp"
#include "vultra/function/rendering/srp/render_context.hpp"
#include "vultra/function/resource/gpu_scene.hpp"

#include <fg/FrameGraph.hpp>

#include <array>

namespace vultra
{
    namespace rendering
    {
        namespace
        {
            // ------------------------------------------------------------
            // Minimal shader pair for the test material pass.
            // ------------------------------------------------------------
            constexpr std::string_view kTestMaterialVert = R"glsl(
#version 460 core

layout(location = 0) in vec3 a_Position;

#ifdef HAS_NORMAL
layout(location = 1) in vec3 a_Normal;
#endif

#ifdef HAS_COLOR
layout(location = 2) in vec3 a_Color;
layout(location = 0) out vec3 v_Color;
#endif

#ifdef HAS_TEXCOORD0
layout(location = 3) in vec2 a_TexCoord0;
layout(location = 1) out vec2 v_TexCoord0;
#endif

layout(push_constant) uniform PushConstants
{
    mat4 model;
    mat4 viewProj;
    uint materialIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} pc;

void main()
{
#ifdef HAS_COLOR
    v_Color = a_Color;
#endif
#ifdef HAS_TEXCOORD0
    v_TexCoord0 = a_TexCoord0;
#endif
    vec4 wpos = pc.model * vec4(a_Position, 1.0);
    gl_Position = pc.viewProj * wpos;
}
)glsl";

            constexpr std::string_view kTestMaterialFrag = R"glsl(
#version 460 core

layout(location = 0) out vec4 FragColor;

#ifdef HAS_COLOR
layout(location = 0) in vec3 v_Color;
#endif

// Packed material table (GPU-side).
struct MaterialEntry
{
    uint model;
    uint blockOffsetBytes;
    uint pad0;
    uint pad1;
};

layout(set = 0, binding = 0, std430) readonly buffer MaterialTable
{
    MaterialEntry materials[];
};

// Raw byte-addressed material parameter pool, stored as uint words.
layout(set = 0, binding = 1, std430) readonly buffer MaterialParams
{
    uint words[];
};

layout(push_constant) uniform PushConstants
{
    mat4 model;
    mat4 viewProj;
    uint materialIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} pc;

vec4 loadVec4(uint byteOffset)
{
    uint i = byteOffset >> 2; // /4
    return vec4(uintBitsToFloat(words[i + 0]),
                uintBitsToFloat(words[i + 1]),
                uintBitsToFloat(words[i + 2]),
                uintBitsToFloat(words[i + 3]));
}

void main()
{
    MaterialEntry m = materials[pc.materialIndex];

    // All current material param blocks start with the primary color.
    // We still branch by model so future extensions can diverge.
    vec4 c = loadVec4(m.blockOffsetBytes);

    // Model routing (matches vultra::resource::GpuMaterialModel)
    // 1: PBRMR, 2: PBRSG, 3: Unlit, 4: Phong
    if (m.model == 1u)
        c = loadVec4(m.blockOffsetBytes); // baseColor
    else if (m.model == 2u)
        c = loadVec4(m.blockOffsetBytes); // diffuseColor
    else if (m.model == 3u)
        c = loadVec4(m.blockOffsetBytes); // unlit color
    else if (m.model == 4u)
        c = loadVec4(m.blockOffsetBytes); // phong diffuse

#ifdef HAS_COLOR
    // If vertex color exists, modulate for quick visual verification.
    c.rgb *= v_Color;
#endif

    FragColor = c;
}
)glsl";

            struct PushConstants
            {
                glm::mat4 model;
                glm::mat4 viewProj;
                uint32_t  materialIndex;
                uint32_t  pad0;
                uint32_t  pad1;
                uint32_t  pad2;
            };
            static_assert(sizeof(PushConstants) % 16 == 0);

            // Keep attribute locations consistent with AssetSystem::buildVertexAttributes.
            constexpr rhi::LocationIndex kLoc_Position  = 0;
            constexpr rhi::LocationIndex kLoc_Normal    = 1;
            constexpr rhi::LocationIndex kLoc_Color     = 2;
            constexpr rhi::LocationIndex kLoc_TexCoord0 = 3;
            constexpr rhi::LocationIndex kLoc_TexCoord1 = 4;
            constexpr rhi::LocationIndex kLoc_Tangent   = 5;
            constexpr rhi::LocationIndex kLoc_Joints    = 6;
            constexpr rhi::LocationIndex kLoc_Weights   = 7;

            inline bool hasAttrib(const rhi::VertexAttributes& a, rhi::LocationIndex loc)
            {
                return a.find(loc) != a.end();
            }
        } // namespace

        size_t TestMaterialPass::hashVertexAttributes(const rhi::VertexAttributes& attrs)
        {
            size_t h = 0;
            for (const auto& [loc, a] : attrs)
            {
                hashCombine(h, loc, static_cast<uint32_t>(a.type), a.offset);
            }
            return h;
        }

        std::unordered_map<std::string, std::optional<std::string>>
        TestMaterialPass::buildVertexDefines(const rhi::VertexAttributes& attrs)
        {
            std::unordered_map<std::string, std::optional<std::string>> d;

            if (hasAttrib(attrs, kLoc_Color))
                d.insert_or_assign("HAS_COLOR", std::nullopt);
            if (hasAttrib(attrs, kLoc_Normal))
                d.insert_or_assign("HAS_NORMAL", std::nullopt);
            if (hasAttrib(attrs, kLoc_TexCoord0))
            {
                d.insert_or_assign("HAS_TEXCOORD0", std::nullopt);
                if (hasAttrib(attrs, kLoc_Tangent))
                    d.insert_or_assign("HAS_TANGENTS", std::nullopt);
            }
            if (hasAttrib(attrs, kLoc_TexCoord1))
                d.insert_or_assign("HAS_TEXCOORD1", std::nullopt);
            if (hasAttrib(attrs, kLoc_Joints) && hasAttrib(attrs, kLoc_Weights))
                d.insert_or_assign("IS_SKINNED", std::nullopt);

            return d;
        }

        rhi::GraphicsPipeline& TestMaterialPass::getOrCreatePipeline(rhi::RenderDevice&           rd,
                                                                     const rhi::VertexAttributes& attrs,
                                                                     rhi::PixelFormat             colorFormat)
        {
            size_t key = hashVertexAttributes(attrs);
            hashCombine(key, static_cast<uint32_t>(colorFormat));
            if (auto it = m_Pipelines.find(key); it != m_Pipelines.end())
                return it->second.pipeline;

            auto defines = buildVertexDefines(attrs);

            // Make a copy of attributes, and ignore ones not consumed by this shader variant.
            // This avoids validation warnings while keeping stride consistent.
            rhi::VertexAttributes ia = attrs;

            auto consumes = [&](rhi::LocationIndex loc) -> bool {
                if (loc == kLoc_Position)
                    return true;
                if (loc == kLoc_Normal)
                    return defines.contains("HAS_NORMAL");
                if (loc == kLoc_Color)
                    return defines.contains("HAS_COLOR");
                if (loc == kLoc_TexCoord0)
                    return defines.contains("HAS_TEXCOORD0");
                if (loc == kLoc_TexCoord1)
                    return defines.contains("HAS_TEXCOORD1");
                if (loc == kLoc_Tangent)
                    return defines.contains("HAS_TANGENTS");
                if (loc == kLoc_Joints || loc == kLoc_Weights)
                    return defines.contains("IS_SKINNED");
                return false;
            };

            for (auto& [loc, a] : ia)
            {
                if (!consumes(loc))
                    a.offset = rhi::kIgnoreVertexAttribute;
            }

            // Pipeline layout is derived from shader reflection.
            // Descriptor set 0:
            //   binding 0 : MaterialTable (SSBO)
            //   binding 1 : MaterialParams (SSBO)
            rhi::GraphicsPipeline::Builder b;
            b.setColorFormats({colorFormat});
            b.setDepthStencil({.depthTest = false, .depthWrite = false});
            b.setRasterizer({.cullMode = rhi::CullMode::eBack});
            b.setInputAssembly(ia);
            b.addShader(rhi::ShaderType::eVertex,
                        rhi::ShaderStageInfo {
                            .code           = std::string(kTestMaterialVert),
                            .entryPointName = "main",
                            .defines        = defines,
                        });
            b.addShader(rhi::ShaderType::eFragment,
                        rhi::ShaderStageInfo {
                            .code           = std::string(kTestMaterialFrag),
                            .entryPointName = "main",
                            .defines        = defines,
                        });

            PipelineState ps;
            ps.pipeline = b.build(rd);

            auto [it, _] = m_Pipelines.emplace(key, std::move(ps));
            return it->second.pipeline;
        }

        void TestMaterialPass::ensureMaterialTableUploaded(rhi::RenderDevice& rd, const resource::GpuResourcePool& pool)
        {
            // Hash CPU table content so we can avoid redundant uploads.
            size_t h = 0;
            for (const auto& m : pool.materials)
            {
                hashCombine(h, static_cast<uint32_t>(m.model), m.blockOffsetBytes);
            }
            if (h == m_LastMaterialTableHash && m_MaterialTableBuffer)
                return;

            std::vector<MaterialTableEntry> table;
            table.resize(pool.materials.size());
            for (size_t i = 0; i < pool.materials.size(); ++i)
            {
                table[i].model            = static_cast<uint32_t>(pool.materials[i].model);
                table[i].blockOffsetBytes = pool.materials[i].blockOffsetBytes;
            }

            const size_t bytes = table.size() * sizeof(MaterialTableEntry);
            if (!m_MaterialTableBuffer || m_MaterialTableBuffer->getSize() < bytes)
            {
                m_MaterialTableBuffer =
                    createRef<rhi::StorageBuffer>(rd.createStorageBuffer(std::max<size_t>(bytes, 16)));
            }

            if (bytes > 0)
                rd.upload(*m_MaterialTableBuffer, 0, bytes, table.data());

            m_LastMaterialTableHash = h;
        }

        void TestMaterialPass::addPasses(RenderContext& ctx)
        {
            if (!ctx.renderWorld.gpuResources)
                return;

            auto* pool = ctx.renderWorld.gpuResources;

            // Import the camera target into the graph.
            auto target = framegraph::importTexture(
                ctx.fg, "TestMaterial.Target", ctx.framebufferInfo->colorAttachments[0].target);

            struct PassData
            {
                FrameGraphResource color;
            };

            ctx.fg.addCallbackPass<PassData>(
                "TestMaterialPass",
                [&](FrameGraph::Builder& builder, PassData& data) {
                    PASS_SETUP_ZONE;
                    data.color = builder.write(target);
                    builder.setSideEffect();
                },
                [this, &ctx](const PassData&, FrameGraphPassResources&, void*) {
                    auto& cb = ctx.cb;
                    auto& rd = ctx.rd;

                    if (!ctx.framebufferInfo)
                        return;

                    ensureMaterialTableUploaded(rd, *ctx.renderWorld.gpuResources);
                    if (!m_MaterialTableBuffer || !ctx.renderWorld.gpuResources->materialParams.gpu)
                        return;

                    cb.beginRendering(*ctx.framebufferInfo);

                    // Descriptor set 0: material table + material params
                    auto descBuilder = cb.createDescriptorSetBuilder();

                    // Build one descriptor set and rebind if pipeline changes (layout changes by vertex defines).
                    vk::DescriptorSet       lastSet0    = nullptr;
                    vk::DescriptorSetLayout lastLayout0 = nullptr;

                    const auto& cams = ctx.camera;

                    for (const auto& inst : ctx.renderWorld.instances)
                    {
                        if (inst.meshIndex >= ctx.renderWorld.gpuResources->meshes.size())
                            continue;

                        auto& mesh = ctx.renderWorld.gpuResources->meshes[inst.meshIndex];
                        auto& pipe =
                            getOrCreatePipeline(rd,
                                                mesh.layout.attributes,
                                                ctx.framebufferInfo->colorAttachments[0].target->getPixelFormat());

                        cb.bindPipeline(pipe);

                        // (Re)build descriptor set if pipeline layout differs.
                        const auto layout0 = pipe.getDescriptorSetLayout(0);
                        if (layout0 != lastLayout0)
                        {
                            descBuilder.bind(
                                0, rhi::bindings::StorageBuffer {m_MaterialTableBuffer.get(), 0, std::nullopt});
                            descBuilder.bind(
                                1,
                                rhi::bindings::StorageBuffer {
                                    ctx.renderWorld.gpuResources->materialParams.gpu.get(), 0, std::nullopt});

                            lastSet0    = descBuilder.build(layout0);
                            lastLayout0 = layout0;
                        }

                        if (lastSet0)
                            cb.bindDescriptorSet(0, lastSet0);
                        rhi::GeometryInfo geo {};
                        geo.topology     = rhi::PrimitiveTopology::eTriangleList;
                        geo.vertexBuffer = &mesh.vertexBuffer;
                        geo.numVertices  = mesh.vertexCount;
                        geo.indexBuffer  = &mesh.indexBuffer;
                        geo.numIndices   = mesh.indexCount;

                        PushConstants pc;
                        pc.model         = inst.worldMatrix;
                        pc.viewProj      = cams.viewProjection;
                        pc.materialIndex = mesh.materialOffset;
                        pc.pad0          = 0;
                        pc.pad1          = 0;
                        pc.pad2          = 0;

                        cb.pushConstants(rhi::ShaderStages::eVertex | rhi::ShaderStages::eFragment, 0, &pc);
                        cb.draw(geo);
                    }

                    cb.endRendering();
                });
        }
    } // namespace rendering
} // namespace vultra
