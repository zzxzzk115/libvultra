#include "support/ray_geometry_uploads.hpp"
#include "support/scene_position_write.hpp"

#include <vultra/assets/scene_render_data.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/ray_scene.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>

namespace
{
    using namespace vultra;

    struct RestoreCore
    {
        Device&          device;
        VriCoreInterface previous;

        ~RestoreCore()
        {
            device.core = previous;
        }
    };

    struct Bindings
    {
        Device&                         device;
        VriPipelineLayout*              layout     = nullptr;
        VriDescriptorPool*              pool       = nullptr;
        VriDescriptorSet*               set        = nullptr;
        VriDescriptor*                  outputView = nullptr;
        std::unique_ptr<ShaderPipeline> pipeline;

        ~Bindings()
        {
            device.waitIdleForDestruction();
            pipeline.reset();
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            if (outputView)
            {
                device.core.DestroyDescriptor(outputView);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }
    };

    std::expected<void, std::string>
    bind(Bindings& binding, RayScene& rays, Buffer& output, const ShaderProgram& program)
    {
        auto&            core = binding.device.core;
        const std::array ranges {
            VriDescriptorRangeDesc {0, 1, VriDescriptorType_AccelerationStructure, VriShaderStage_Compute},
            VriDescriptorRangeDesc {1, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute}};
        const VriDescriptorSetDesc  set {0, ranges.data(), uint32_t(ranges.size())};
        const VriPipelineLayoutDesc layout {&set, 1, nullptr, 0, VriShaderStage_Compute};
        if (core.CreatePipelineLayout(binding.device.handle, &layout, &binding.layout) != VriResult_Success)
        {
            return std::unexpected("Create scene-ray query layout failed");
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum         = 1;
        pool.accelerationStructureMaxNum = 1;
        pool.storageBufferMaxNum         = 1;
        if (core.CreateDescriptorPool(binding.device.handle, &pool, &binding.pool) != VriResult_Success ||
            core.AllocateDescriptorSets(binding.pool, binding.layout, 0, &binding.set, 1) != VriResult_Success)
        {
            return std::unexpected("Create scene-ray query descriptors failed");
        }
        const VriBufferViewDesc view {output.handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 32};
        if (core.CreateBufferView(binding.device.handle, &view, &binding.outputView) != VriResult_Success)
        {
            return std::unexpected("Create scene-ray output view failed");
        }
        const std::array<const VriDescriptor*, 2> views {rays.descriptor(), binding.outputView};
        const std::array                          updates {VriDescriptorRangeUpdateDesc {&views[0], 1},
                                  VriDescriptorRangeUpdateDesc {&views[1], 1}};
        core.UpdateDescriptorRanges(binding.set, 0, uint32_t(updates.size()), updates.data());
        auto pipeline = ShaderPipeline::create(
            binding.device,
            program,
            {{"traceMain", VriShaderStage_Compute}},
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                VriPipeline*                 result = nullptr;
                const VriComputePipelineDesc description {binding.layout, shaders[0], binding.device.pipelineCache};
                const auto status = core.CreateComputePipeline(binding.device.handle, &description, &result);
                if (status != VriResult_Success)
                {
                    if (result)
                    {
                        core.DestroyPipeline(result);
                    }
                    return std::unexpected("Create scene-ray query pipeline failed");
                }
                return result;
            });
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        binding.pipeline = std::move(*pipeline);
        return {};
    }

    using AccelerationFootprint = std::map<const void*, std::pair<uint64_t, uint32_t>>;

    std::expected<AccelerationFootprint, std::string> accelerationFootprint(Device& device)
    {
        uint32_t count = 0;
        if (device.core.EnumerateObjects(device.handle, &count, nullptr) != VriResult_Success)
        {
            return std::unexpected("Count acceleration footprint objects failed");
        }
        std::vector<VriObjectInfo> objects(count);
        if (device.core.EnumerateObjects(device.handle, &count, objects.data()) != VriResult_Success)
        {
            return std::unexpected("Read acceleration footprint objects failed");
        }
        AccelerationFootprint result;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& object = objects[i];
            if (object.type == VriObjectType_AccelerationStructure)
            {
                result.emplace(object.handle, std::pair {object.memoryBytes, object.height});
            }
        }
        return result;
    }

    std::expected<void, std::string> probe(Device& device, const ShaderProgram& program)
    {
        SceneData input;
        input.vertices = {{{-1, -1, -2}, {0, 0, 1}, {0, 0}},
                          {{1, -1, -2}, {0, 0, 1}, {1, 0}},
                          {{0, 1, -2}, {0, 0, 1}, {.5f, 1}}};
        input.indices  = {0, 1, 2};
        input.primitives.push_back({0, 3, 0});
        input.materials.emplace_back().doubleSided = false;
        input.radius                               = 2;
        PreparedTextures prepared;
        TextureData      white;
        white.format = TextureFormat::eRgba8Unorm;
        white.levels.push_back({{1, 1}, {std::byte {255}, std::byte {255}, std::byte {255}, std::byte {255}}});
        prepared.images.push_back(std::move(white));
        prepared.materials.push_back({0, 0, 0, 0, 0, 0, 0});
        auto scene = GpuScene::create(device, input, prepared);
        if (!scene)
        {
            return std::unexpected(scene.error());
        }
        {
            const auto      vertexState         = (*scene)->vertices->captureState();
            const auto      indexState          = (*scene)->indices->captureState();
            static unsigned rejectedSubmissions = 0;
            rejectedSubmissions                 = 0;
            RestoreCore restore {device, device.core};
            device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
            {
                ++rejectedSubmissions;
                return VriResult_Failure;
            };
            auto rejected = (*scene)->raySceneExpected();
            if (rejected || rejectedSubmissions != 1 || (*scene)->vertices->captureState() != vertexState ||
                (*scene)->indices->captureState() != indexState)
            {
                return std::unexpected("Rejected BLAS submission did not restore its unsubmitted geometry states");
            }
        }
        {
            RestoreCore restore {device, device.core};
            device.core.CreateBuffer = [](VriDevice*, const VriBufferDesc*, VriBuffer** output)
            {
                *output = nullptr;
                return VriResult_OutOfMemory;
            };
            if ((*scene)->raySceneExpected())
            {
                return std::unexpected("Failed AS instance-buffer allocation installed a scene cache");
            }
        }
        test::RayGeometryUploads uploads(device);
        auto rays = (*scene)->raySceneExpected();
        if (!rays)
        {
            return std::unexpected("Retry scene-owned ray creation: " + rays.error());
        }
        const auto descriptor = (*rays)->descriptor();
        RaySceneOptions options;
        options.forceOpaque    = true;
        options.forceNonOpaque = true;
        if (RayScene::create(device, **scene, options))
        {
            return std::unexpected("Ray scene accepted conflicting instance opacity overrides");
        }
        options.forceOpaque         = false;
        options.disableFaceCulling  = true;
        options.includeAlphaBlended = true;
        {
            RestoreCore restore {device, device.core};
            device.core.CreateBuffer = [](VriDevice*, const VriBufferDesc*, VriBuffer** output)
            {
                *output = nullptr;
                return VriResult_OutOfMemory;
            };
            if (RayScene::create(device, **scene, options))
            {
                return std::unexpected("Failed policy allocation replaced shared geometry");
            }
        }
        auto policy = RayScene::create(device, **scene, options);
        if (!policy || uploads.packets.size() != 2 || uploads.packets[0].addresses.size() != 1 ||
            uploads.packets[0].addresses[0] == 0 || uploads.packets[0].addresses != uploads.packets[1].addresses ||
            uploads.packets[0].ids != uploads.packets[1].ids || uploads.packets[0].flags[0] != (4u << 24) ||
            uploads.packets[1].flags[0] != (9u << 24))
        {
            return std::unexpected("Ray policies did not share BLAS addresses with independent instance flags");
        }
        auto output = Buffer::create(device, {32, 16, VriBufferUsage_StorageBuffer, VriMemoryLocation_HostReadback});
        if (!output)
        {
            return std::unexpected(output.error());
        }
        Bindings binding {device};
        if (auto bound = bind(binding, **rays, **output, program); !bound)
        {
            return bound;
        }
        Bindings policyBinding {device};
        if (auto bound = bind(policyBinding, **policy, **output, program); !bound)
        {
            return bound;
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto check = [&](bool front, bool back, bool usePolicy = false) -> std::expected<void, std::string>
        {
            if (usePolicy)
            {
                if (auto updated = (*policy)->update(); !updated)
                {
                    return updated;
                }
            }
            auto& selected = usePolicy ? policyBinding : binding;
            auto current = (*scene)->raySceneExpected();
            if (!current || *current != *rays || (*current)->descriptor() != descriptor)
            {
                return std::unexpected("Scene-owned AS/descriptor identity changed between consumers or edits");
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*output)->transition(*commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            device.core.CmdSetPipelineLayout(*commands, selected.layout);
            device.core.CmdSetPipeline(*commands, selected.pipeline->handle());
            device.core.CmdSetDescriptorSet(*commands, 0, selected.set);
            const VriDispatchDesc groups {1, 1, 1};
            device.core.CmdDispatch(*commands, &groups);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            const auto* mapped = device.core.MapBuffer((*output)->handle, 0, 32);
            if (!mapped)
            {
                return std::unexpected("Map scene-ray query output failed");
            }
            std::array<uint32_t, 8> values {};
            std::memcpy(values.data(), mapped, 32);
            device.core.UnmapBuffer((*output)->handle);
            for (size_t i = 0; i < 2; ++i)
            {
                const bool hit = i == 0 ? front : back;
                if (values[i * 4] != uint32_t(hit) || values[i * 4 + 1] != 0 || values[i * 4 + 3] != 0 ||
                    (hit ? std::bit_cast<float>(values[i * 4 + 2]) != 2.f : values[i * 4 + 2] != 0))
                {
                    std::fprintf(stderr,
                                 "SCENE_RAY_MISMATCH ray=%zu expected_hit=%u actual=%u,%u,%u,%u\n",
                                 i,
                                 uint32_t(hit),
                                 values[i * 4],
                                 values[i * 4 + 1],
                                 values[i * 4 + 2],
                                 values[i * 4 + 3]);
                    return std::unexpected("Scene-owned AS query data differs for ray " + std::to_string(i));
                }
            }
            return {};
        };
        if (auto checked = check(true, false); !checked)
        {
            return checked;
        }
        if (auto checked = check(true, true, true); !checked)
        {
            return checked;
        }
        (*scene)->materials[0].doubleSided = true;
        if (auto checked = check(true, true); !checked)
        {
            return checked;
        }
        (*scene)->materials[0].alphaBlend = true;
        if (auto checked = check(false, false); !checked)
        {
            return checked;
        }
        if (auto checked = check(true, true, true); !checked)
        {
            return checked;
        }
        (*scene)->materials[0].alphaBlend = false;
        if (auto moved =
                (*scene)->setPrimitiveTransformsExpected(0, 1, glm::translate(glm::mat4(1), glm::vec3(5, 0, 0)));
            !moved)
        {
            return moved;
        }
        if (auto checked = check(false, false); !checked)
        {
            return checked;
        }
        if (auto moved = (*scene)->setPrimitiveTransformsExpected(0, 1, glm::mat4(1)); !moved)
        {
            return moved;
        }
        if (auto checked = check(true, true); !checked)
        {
            return checked;
        }
        ++(*scene)->primitives[0].firstIndex;
        const bool rejectedTopology = !(*scene)->raySceneExpected() && !(*policy)->update();
        --(*scene)->primitives[0].firstIndex;
        if (!rejectedTopology)
        {
            return std::unexpected("Scene-owned AS accepted stale topology");
        }
        auto replacement = Buffer::create(device, (*scene)->indices->desc);
        if (!replacement)
        {
            return std::unexpected(replacement.error());
        }
        (*scene)->indices.swap(*replacement);
        const bool rejectedBuffer = !(*scene)->raySceneExpected() && !(*policy)->update();
        (*scene)->indices.swap(*replacement);
        if (!rejectedBuffer)
        {
            return std::unexpected("Scene-owned AS accepted replacement geometry buffers");
        }
        if (auto checked = check(true, true); !checked)
        {
            return checked;
        }
        const auto geometryRevision = (*scene)->geometryRevision();
        const auto beforeFootprint  = accelerationFootprint(device);
        if (!beforeFootprint || beforeFootprint->size() != 3)
        {
            return std::unexpected("Position fixture did not track one shared BLAS and two policy TLAS");
        }
        const auto worldBefore = **(*scene)->primitiveBoundsExpected(0);
        std::array invalidBounds {PrimitiveBounds {{0, 0, -2}, {-1, 1, 0}}};
        if ((*scene)->notifyVertexPositionsChangedExpected({}) ||
            (*scene)->notifyVertexPositionsChangedExpected(invalidBounds))
        {
            return std::unexpected("Position notification accepted incomplete or negative bounds");
        }
        invalidBounds[0].extent = {1, 1, std::numeric_limits<float>::infinity()};
        if ((*scene)->notifyVertexPositionsChangedExpected(invalidBounds) ||
            (*scene)->geometryRevision() != geometryRevision ||
            (**(*scene)->primitiveBoundsExpected(0)).center != worldBefore.center ||
            (**(*scene)->primitiveBoundsExpected(0)).extent != worldBefore.extent)
        {
            return std::unexpected("Rejected position bounds changed accepted bounds or revision");
        }
        auto displaced = input.vertices;
        for (auto& vertex : displaced)
        {
            vertex.position.x += 5;
        }
        const std::array displacedBounds {PrimitiveBounds {{5, 0, -2}, {1, 1, 0}}};
        if (auto changed = test::writeScenePositionsExpected(device, **scene, displaced, displacedBounds); !changed)
        {
            return changed;
        }
        if ((*scene)->geometryRevision() != geometryRevision + 1 || (*scene)->center != glm::vec3(5, 0, -2) ||
            (**(*scene)->primitiveBoundsExpected(0)).center != displacedBounds[0].center)
        {
            return std::unexpected("Position notification did not publish conservative scene/primitive bounds");
        }
        {
            RestoreCore     restore {device, device.core};
            static auto     end          = device.core.EndCommandBuffer;
            static unsigned submissions  = 0;
            submissions                  = 0;
            const auto before            = (*scene)->vertices->captureState();
            device.core.EndCommandBuffer = [](VriCommandBuffer* commands)
            {
                const auto status = end(commands);
                return status == VriResult_Success ? VriResult_OutOfMemory : status;
            };
            device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
            {
                ++submissions;
                return VriResult_Failure;
            };
            if ((*scene)->raySceneExpected() || submissions || (*scene)->vertices->captureState() != before)
            {
                return std::unexpected("Failed AS recording was submitted or changed accepted input tracking");
            }
        }
        {
            RestoreCore restore {device, device.core};
            const auto  before      = (*scene)->vertices->captureState();
            device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
            {
                return VriResult_Failure;
            };
            if ((*scene)->raySceneExpected() || (*scene)->vertices->captureState() != before)
            {
                return std::unexpected("Rejected geometry rebuild did not retain its pending revision/input state");
            }
        }
        if (auto checked = check(false, false); !checked)
        {
            return checked;
        }
        if (auto checked = check(false, false, true); !checked)
        {
            return checked;
        }
        const auto uploadsBeforeStable = uploads.packets.size();
        if (auto checked = check(false, false, true); !checked)
        {
            return checked;
        }
        if (uploads.packets.size() != uploadsBeforeStable)
        {
            return std::unexpected("Unchanged position revision uploaded policy instances again");
        }
        const std::array originalBounds {PrimitiveBounds {{0, 0, -2}, {1, 1, 0}}};
        if (auto changed = test::writeScenePositionsExpected(device, **scene, input.vertices, originalBounds); !changed)
        {
            return changed;
        }
        {
            RestoreCore     restore {device, device.core};
            static auto     submit  = device.core.QueueSubmit;
            static unsigned calls   = 0;
            calls                   = 0;
            device.core.QueueSubmit = [](VriQueue* queue, const VriQueueSubmitDesc* desc)
            {
                return ++calls == 2 ? VriResult_Failure : submit(queue, desc);
            };
            if ((*policy)->update() || calls != 2)
            {
                return std::unexpected("Position update did not reject its TLAS submission after completing BLAS");
            }
        }
        if (auto checked = check(true, true, true); !checked)
        {
            return checked;
        }
        if (uploads.packets.back().addresses != uploads.packets.front().addresses)
        {
            return std::unexpected("Position rebuild replaced the shared BLAS device addresses");
        }
        const auto afterFootprint = accelerationFootprint(device);
        if (!afterFootprint || *afterFootprint != *beforeFootprint)
        {
            return std::unexpected("Position rebuild retained scratch or changed native AS ownership/accounting");
        }
        std::puts("Scene-owned AS: shared BLAS addresses, independent flags/queries, submission rollback, allocation "
                  "failure/retry, "
                  "stable identity, 24 exact ray queries, position rebuild/bounds/retry, edits and stale-input "
                  "recovery, recording failure before submission, scratch accounting stable passed");
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fputs("Usage: test-scene-rays scene-rays.vshaderc\n", stderr);
        return 2;
    }
    std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() <= 0 || stream.tellg() > 64 * 1024 * 1024)
    {
        return 1;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(stream.tellg()));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
    {
        return 1;
    }
    auto program = ShaderProgram::loadExpected(bytes, argv[1]);
    auto device  = Device::create(true, nullptr, VriFeature_RayQuery);
    if (!program || !device)
    {
        std::fprintf(stderr, "%s\n", (!program ? program.error() : device.error()).c_str());
        return 1;
    }
    if (auto result = probe(**device, *program); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
