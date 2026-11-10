#pragma once

#include <vultra/assets/scene_render_data.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>
#include <vultra/servers/rendering/scene.hpp>

#include <cstdio>
#include <limits>
#include <set>

namespace vultra::test
{
    namespace scene_owner_probe
    {
        enum class Failure
        {
            eNone,
            eBuffer,
            eTexture,
            eMap,
            eEnd
        };

        struct NativeFault
        {
            Device&                    device;
            VriCoreInterface           original;
            Failure                    failure      = Failure::eNone;
            uint32_t                   bufferCalls  = 0;
            uint32_t                   textureCalls = 0;
            uint32_t                   viewCalls    = 0;
            std::set<VriBuffer*>       buffers;
            std::set<VriTexture*>      textures;
            inline static NativeFault* s_Active = nullptr;

            explicit NativeFault(Device& value) :
                device(value),
                original(value.core)
            {
                s_Active                     = this;
                device.core.CreateBuffer     = createBuffer;
                device.core.DestroyBuffer    = destroyBuffer;
                device.core.CreateTexture    = createTexture;
                device.core.DestroyTexture   = destroyTexture;
                device.core.CreateBufferView = createView;
                device.core.MapBuffer        = map;
                device.core.EndCommandBuffer = end;
            }

            ~NativeFault()
            {
                device.core = original;
                s_Active    = nullptr;
            }

            static VriResult VRI_CALL createBuffer(VriDevice* device, const VriBufferDesc* desc, VriBuffer** output)
            {
                auto& self = *s_Active;
                if (++self.bufferCalls == 2 && self.failure == Failure::eBuffer)
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto status = self.original.CreateBuffer(device, desc, output);
                if (status == VriResult_Success && *output)
                {
                    self.buffers.insert(*output);
                }
                return status;
            }

            static void VRI_CALL destroyBuffer(VriBuffer* buffer)
            {
                s_Active->buffers.erase(buffer);
                s_Active->original.DestroyBuffer(buffer);
            }

            static VriResult VRI_CALL createTexture(VriDevice* device, const VriTextureDesc* desc, VriTexture** output)
            {
                auto& self = *s_Active;
                ++self.textureCalls;
                if (self.failure == Failure::eTexture)
                {
                    *output = nullptr;
                    return VriResult_OutOfMemory;
                }
                const auto status = self.original.CreateTexture(device, desc, output);
                if (status == VriResult_Success && *output)
                {
                    self.textures.insert(*output);
                }
                return status;
            }

            static void VRI_CALL destroyTexture(VriTexture* texture)
            {
                s_Active->textures.erase(texture);
                s_Active->original.DestroyTexture(texture);
            }

            static VriResult VRI_CALL createView(VriDevice*               device,
                                                 const VriBufferViewDesc* desc,
                                                 VriDescriptor**          output)
            {
                ++s_Active->viewCalls;
                return s_Active->original.CreateBufferView(device, desc, output);
            }

            static void* VRI_CALL map(VriBuffer* buffer, uint64_t offset, uint64_t size)
            {
                return s_Active->failure == Failure::eMap ? nullptr :
                                                            s_Active->original.MapBuffer(buffer, offset, size);
            }

            static VriResult VRI_CALL end(VriCommandBuffer* commands)
            {
                return s_Active->failure == Failure::eEnd ? VriResult_Failure :
                                                            s_Active->original.EndCommandBuffer(commands);
            }
        };
    } // namespace scene_owner_probe

    inline std::expected<void, std::string> sceneOwnerExpectedProbe(Device& device)
    {
        using namespace scene_owner_probe;
        SceneData input;
        input.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}},
                          {{1, 0, 0}, {0, 0, 1}, {1, 0}},
                          {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
        input.indices  = {0, 1, 2};
        input.primitives.push_back({0, 3, 0});
        input.materials.emplace_back();
        input.radius = 1;
        PreparedTextures prepared;
        TextureData      white;
        white.format = TextureFormat::eRgba8Unorm;
        white.levels.push_back({{1, 1}, {std::byte {255}, std::byte {255}, std::byte {255}, std::byte {255}}});
        prepared.images.push_back(std::move(white));
        prepared.materials.push_back({0, 0, 0, 0, 0, 0, 0});
        NativeFault faults(device);
        {
            RenderingServer first(device);
            RenderingServer second(device);
            auto            live  = first.uploadSceneExpected(input, prepared);
            auto            other = second.uploadSceneExpected(input, prepared);
            if (!live || !other)
            {
                return std::unexpected("Expected rendering server failed to upload an indexed scene");
            }
            auto borrowed = live->getExpected();
            if (!borrowed)
            {
                return std::unexpected(borrowed.error());
            }
            const auto        rid = live->rid();
            const GpuSceneRid wrongType {(rid.value & ((uint64_t(1) << 56) - 1)) | (uint64_t(2) << 56)};
            if (rid == other->rid() || !first.alive(rid) || second.sceneExpected(rid) ||
                first.sceneExpected(wrongType) || first.release(wrongType) || GpuSceneHandle {}.getExpected())
            {
                return std::unexpected("Expected rendering server accepted a foreign, typed or empty scene handle");
            }
            const auto buffers  = faults.buffers;
            const auto textures = faults.textures;
            const auto vertices = (*borrowed)->vertices->handle;
            const auto indices  = (*borrowed)->indices->handle;
            const auto texture  = (*borrowed)->textures.front()->handle;
            const auto retained = [&]
            {
                auto scene = live->getExpected();
                return scene && *scene == *borrowed && first.alive(rid) && (*scene)->vertices->handle == vertices &&
                       (*scene)->indices->handle == indices && faults.buffers == buffers && faults.textures == textures;
            };
            auto invalid       = input;
            invalid.indices[0] = 3;
            if (first.uploadSceneExpected(invalid, prepared) || !retained())
            {
                return std::unexpected("Invalid scene metadata replaced the accepted rendering-server scene");
            }
            for (auto failure : {Failure::eBuffer, Failure::eTexture, Failure::eMap, Failure::eEnd})
            {
                faults.failure     = failure;
                faults.bufferCalls = 0;
                auto failed        = first.uploadSceneExpected(input, prepared);
                faults.failure     = Failure::eNone;
                if (failed || !retained())
                {
                    return std::unexpected(
                        "Rejected scene upload retained partial native buffers/textures or changed its RID");
                }
                auto recovered = first.uploadSceneExpected(input, prepared);
                if (!recovered || recovered->rid() == rid || !recovered->getExpected())
                {
                    return std::unexpected("Expected scene upload did not recover after native failure");
                }
                *recovered = GpuSceneHandle {};
                first.collectCompletedFrame();
                if (!retained())
                {
                    return std::unexpected("Retiring a recovered candidate changed the previous live scene");
                }
            }
            MeshletData meshlets;
            meshlets.primitives.push_back({0, 1});
            meshlets.meshlets.push_back({{0, 0, 3, 1}, {0, 0, 0, 1}});
            meshlets.vertices  = {0, 1, 2};
            meshlets.triangles = {0x020100};
            auto mismatched    = meshlets;
            mismatched.primitives.clear();
            if (GpuScene::create(device, input, prepared, &mismatched) || !retained())
            {
                return std::unexpected("Expected meshlet scene accepted a mismatched primitive table");
            }
            if (!(device.features & VriFeature_MeshShader))
            {
                const auto calls = faults.bufferCalls;
                if (GpuScene::create(device, input, prepared, &meshlets) ||
                    GpuMeshlets::create(device, *(*borrowed)->vertices, meshlets) || calls != faults.bufferCalls)
                {
                    return std::unexpected("Unsupported meshlet request allocated GPU payloads or silently succeeded");
                }
            }
            {
                struct RestoreFeatures
                {
                    Device&  device;
                    uint64_t previous;

                    ~RestoreFeatures()
                    {
                        device.features = previous;
                    }
                } restore {device, device.features};

                // Synthetic capability exercises CPU validation only; every case must reject before native work.
                device.features |= VriFeature_MeshShader;
                const auto calls = faults.bufferCalls;
                const auto views = faults.viewCalls;
                for (uint32_t kind = 0; kind < 5; ++kind)
                {
                    auto broken = meshlets;
                    if (kind == 0)
                    {
                        broken.primitives[0].count = 2;
                    }
                    if (kind == 1)
                    {
                        broken.vertices[0] = 3;
                    }
                    if (kind == 2)
                    {
                        broken.meshlets[0].geometry.z = 65;
                    }
                    if (kind == 3)
                    {
                        broken.meshlets[0].bounds.x = std::numeric_limits<float>::quiet_NaN();
                    }
                    if (kind == 4)
                    {
                        broken.triangles[0] = 0xff0000;
                    }
                    if (GpuMeshlets::create(device, *(*borrowed)->vertices, broken) || faults.bufferCalls != calls ||
                        faults.viewCalls != views || !retained())
                    {
                        return std::unexpected(
                            "Meshlet CPU validation accepted invalid geometry or issued native work");
                    }
                }
            }
            auto moved = std::move(*live);
            if (live->getExpected() || !moved.getExpected() || moved.rid() != rid)
            {
                return std::unexpected("Expected scene handle move lost stable identity");
            }
            if (!first.release(rid) || first.alive(rid) || first.sceneExpected(rid) || moved.getExpected() ||
                first.release(rid))
            {
                return std::unexpected("Expected released scene RID remained accessible");
            }
            if (!faults.buffers.contains(vertices) || !faults.textures.contains(texture))
            {
                return std::unexpected("Retired scene resources were destroyed before completed-frame collection");
            }
            first.collectCompletedFrame();
        }
        if (!faults.buffers.empty() || !faults.textures.empty())
        {
            return std::unexpected("Expected rendering server destruction retained native scene owners");
        }
        std::puts("Expected scene ownership: typed/stale/moved RIDs, four native upload failures, owner retention, "
                  "retirement and recovery passed; five meshlet CPU preflight controls issue no native work");
        return {};
    }
} // namespace vultra::test
