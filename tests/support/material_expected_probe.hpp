#pragma once

#include <vultra/servers/rendering/shader_material.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>

namespace vultra::test
{
    inline std::expected<void, std::string>
    materialSchemaExpectedProbe(const ShaderAsset& asset, std::string_view scalarName, std::string_view vectorName)
    {
        MaterialInstance instance;
        if (auto valid = asset.validateExpected(); !valid)
        {
            return valid;
        }
        if (asset.propertyExpected("missing") || asset.variantExpected("missing") ||
            asset.subshaders.front().passes.front().programExpected("missing"))
        {
            return std::unexpected("Expected material lookup accepted a missing field/variant/program");
        }
        if (auto set = instance.setExpected(asset, scalarName, .75f); !set)
        {
            return set;
        }
        const auto revision  = instance.revision();
        const auto overrides = instance.overrides();
        if (instance.setExpected(asset, scalarName, int32_t(1)) ||
            instance.setExpected(asset, scalarName, std::numeric_limits<float>::quiet_NaN()) ||
            instance.setExpected(asset, "missing", .5f) || instance.setVariantExpected(asset, "missing") ||
            instance.overrides() != overrides || instance.revision() != revision)
        {
            return std::unexpected("Rejected Expected material edit changed its value/revision");
        }
        if (auto unchanged = instance.setExpected(asset, scalarName, .75f);
            !unchanged || instance.revision() != revision)
        {
            return std::unexpected("Unchanged Expected material edit advanced its revision");
        }
        const auto& program = asset.subshaders.front().passes.front().programs.begin()->second;
        if (auto data = instance.uniformDataExpected(asset, program); !data)
        {
            return std::unexpected(data.error());
        }
        auto  wrong = program;
        auto* field =
            const_cast<ShaderParameter*>(wrong.parameters.field("material")->field("$element")->field(scalarName));
        field->scalar = ShaderScalarType::eInteger;
        if (instance.uniformDataExpected(asset, wrong))
        {
            return std::unexpected("Same-size wrong uniform scalar type was accepted");
        }
        wrong             = program;
        auto* uniforms    = const_cast<ShaderParameter*>(wrong.parameters.field("material")->field("$element"));
        field             = const_cast<ShaderParameter*>(uniforms->field(scalarName));
        const auto offset = uniforms->field(vectorName)->offset(ShaderOffsetKind::eUniform);
        std::erase_if(field->offsets,
                      [](const auto& value)
                      {
                          return value.kind == ShaderOffsetKind::eUniform;
                      });
        field->offsets.push_back({ShaderOffsetKind::eUniform, offset, 0});
        if (instance.uniformDataExpected(asset, wrong))
        {
            return std::unexpected("Overlapping uniform fields were accepted");
        }
        auto changed = asset;
        std::erase_if(changed.properties,
                      [&](const auto& property)
                      {
                          return property.name == scalarName;
                      });
        changed.variants = {{"Other", {}, {}, {}, {}, {}}};
        for (auto& subshader : changed.subshaders)
        {
            for (auto& pass : subshader.passes)
            {
                auto compiled = pass.programs.begin()->second;
                pass.programs.clear();
                pass.programs.emplace("Other", std::move(compiled));
            }
        }
        std::string messages   = "retained";
        auto        reconciled = instance.reconcileExpected(asset, changed, messages);
        if (reconciled || instance.overrides() != overrides || instance.revision() != revision ||
            instance.variant != "Default" || messages != "retained")
        {
            return std::unexpected("Rejected reconciliation changed overrides, variant, revision or diagnostics");
        }
        changed.variants.front().name = "Default";
        for (auto& subshader : changed.subshaders)
        {
            for (auto& pass : subshader.passes)
            {
                auto compiled = pass.programs.begin()->second;
                pass.programs.clear();
                pass.programs.emplace("Default", std::move(compiled));
            }
        }
        if (auto recovered = instance.reconcileExpected(asset, changed, messages); !recovered)
        {
            return recovered;
        }
        if (instance.revision() != revision + 1 || !instance.overrides().empty() ||
            messages.find("Removed") == std::string::npos)
        {
            return std::unexpected("Expected material reconciliation did not recover once");
        }
        for (const auto& [text, expected] : std::array<std::pair<const char*, double>, 4> {
                 {{"  +.5", .5}, {"-2e+1", -20}, {"0x1p+2", 4}, {"-0", -0.0}}})
        {
            auto number = ShaderStateValue {text, false}.numberExpected();
            if (!number || *number != expected)
            {
                return std::unexpected("Expected render-state literal changed numeric syntax");
            }
        }
        for (const auto* text : {"", "NaN", "Inf", "1e9999", "--2", "1 trailing"})
        {
            if (ShaderStateValue {text, false}.numberExpected())
            {
                return std::unexpected("Invalid state literal was accepted");
            }
        }
        std::string diagnostics;
        auto        selection = asset.selectSubshaderExpected("Vultra",
                                                       UINT64_MAX,
                                                              {},
                                                       diagnostics,
                                                       [](const auto&) -> std::expected<std::string, std::string>
                                                       {
                                                           return std::unexpected("injected compatibility error");
                                                       });
        if (selection || selection.error() != "injected compatibility error")
        {
            return std::unexpected("Expected SubShader compatibility error was lost");
        }
        ShaderProgram malformed;
        malformed.parameters.kind    = ShaderParameterKind::eStruct;
        malformed.parameters.offsets = {{ShaderOffsetKind::eDescriptor, UINT64_MAX, 0}};
        ShaderParameter resource;
        resource.name               = "resource";
        resource.kind               = ShaderParameterKind::eSampler;
        resource.offsets            = {{ShaderOffsetKind::eDescriptor, 1, 0}};
        malformed.parameters.fields = {resource};
        if (malformed.resourceBindingsExpected())
        {
            return std::unexpected("Overflowed reflected binding wrapped into a valid slot");
        }
        malformed.parameters.offsets.clear();
        resource.offsets.clear();
        ShaderParameter array;
        array.name                  = "array";
        array.kind                  = ShaderParameterKind::eArray;
        array.count                 = 3;
        array.fields                = {resource};
        resource.name               = "adjacent";
        resource.offsets            = {{ShaderOffsetKind::eDescriptor, 1, 0}};
        malformed.parameters.fields = {array, resource};
        auto bindings               = malformed.resourceBindingsExpected();
        if (!bindings || bindings->size() != 2 || (*bindings)[0].binding != 0 || (*bindings)[0].count != 3 ||
            (*bindings)[1].binding != 1)
        {
            return std::unexpected("Bounded descriptor array lost its distinct adjacent binding");
        }
        std::cout << "Expected material schema: typed edits/uniforms, overlap/overflow, array slots and transactional "
                     "reconciliation passed\n";
        return {};
    }

    namespace material_probe
    {
        enum class Failure
        {
            eNone,
            eView,
            eLayout,
            ePool,
            eSets,
            ePipeline,
            eMap
        };

        struct NativeFault
        {
            Device&                      device;
            VriCoreInterface             original;
            Failure                      failure = Failure::eNone;
            std::set<VriPipelineLayout*> layouts;
            std::set<VriDescriptorPool*> pools;
            std::set<VriPipeline*>       pipelines;
            std::set<VriDescriptor*>     views;
            inline static NativeFault*   s_Active = nullptr;

            explicit NativeFault(Device& value) :
                device(value),
                original(value.core)
            {
                s_Active                           = this;
                device.core.CreateBufferView       = createView;
                device.core.DestroyDescriptor      = destroyView;
                device.core.CreatePipelineLayout   = createLayout;
                device.core.DestroyPipelineLayout  = destroyLayout;
                device.core.CreateDescriptorPool   = createPool;
                device.core.DestroyDescriptorPool  = destroyPool;
                device.core.AllocateDescriptorSets = allocateSets;
                device.core.CreateComputePipeline  = createPipeline;
                device.core.DestroyPipeline        = destroyPipeline;
                device.core.MapBuffer              = map;
            }

            ~NativeFault()
            {
                device.core = original;
                s_Active    = nullptr;
            }

            NativeFault(const NativeFault&)            = delete;
            NativeFault& operator=(const NativeFault&) = delete;

            bool empty() const
            {
                return layouts.empty() && pools.empty() && pipelines.empty() && views.empty();
            }

            static VriResult VRI_CALL createView(VriDevice*               device,
                                                 const VriBufferViewDesc* desc,
                                                 VriDescriptor**          output)
            {
                auto& self = *s_Active;
                if (self.failure == Failure::eView)
                {
                    return VriResult_Failure;
                }
                const auto status = self.original.CreateBufferView(device, desc, output);
                if (status == VriResult_Success)
                {
                    self.views.insert(*output);
                }
                return status;
            }

            static void VRI_CALL destroyView(VriDescriptor* value)
            {
                s_Active->views.erase(value);
                s_Active->original.DestroyDescriptor(value);
            }

            static VriResult VRI_CALL createLayout(VriDevice*                   device,
                                                   const VriPipelineLayoutDesc* desc,
                                                   VriPipelineLayout**          output)
            {
                auto& self = *s_Active;
                if (self.failure == Failure::eLayout)
                {
                    return VriResult_Failure;
                }
                const auto status = self.original.CreatePipelineLayout(device, desc, output);
                if (status == VriResult_Success)
                {
                    self.layouts.insert(*output);
                }
                return status;
            }

            static void VRI_CALL destroyLayout(VriPipelineLayout* value)
            {
                s_Active->layouts.erase(value);
                s_Active->original.DestroyPipelineLayout(value);
            }

            static VriResult VRI_CALL createPool(VriDevice*                   device,
                                                 const VriDescriptorPoolDesc* desc,
                                                 VriDescriptorPool**          output)
            {
                auto& self = *s_Active;
                if (self.failure == Failure::ePool)
                {
                    return VriResult_Failure;
                }
                const auto status = self.original.CreateDescriptorPool(device, desc, output);
                if (status == VriResult_Success)
                {
                    self.pools.insert(*output);
                }
                return status;
            }

            static void VRI_CALL destroyPool(VriDescriptorPool* value)
            {
                s_Active->pools.erase(value);
                s_Active->original.DestroyDescriptorPool(value);
            }

            static VriResult VRI_CALL allocateSets(VriDescriptorPool*       pool,
                                                   const VriPipelineLayout* layout,
                                                   uint32_t                 index,
                                                   VriDescriptorSet**       sets,
                                                   uint32_t                 count)
            {
                if (s_Active->failure == Failure::eSets)
                {
                    return VriResult_Failure;
                }
                return s_Active->original.AllocateDescriptorSets(pool, layout, index, sets, count);
            }

            static VriResult VRI_CALL createPipeline(VriDevice*                    device,
                                                     const VriComputePipelineDesc* desc,
                                                     VriPipeline**                 output)
            {
                auto& self = *s_Active;
                if (self.failure == Failure::ePipeline)
                {
                    return VriResult_Failure;
                }
                const auto status = self.original.CreateComputePipeline(device, desc, output);
                if (status == VriResult_Success)
                {
                    self.pipelines.insert(*output);
                }
                return status;
            }

            static void VRI_CALL destroyPipeline(VriPipeline* value)
            {
                s_Active->pipelines.erase(value);
                s_Active->original.DestroyPipeline(value);
            }

            static void* VRI_CALL map(VriBuffer* buffer, uint64_t offset, uint64_t size)
            {
                return s_Active->failure == Failure::eMap ? nullptr :
                                                            s_Active->original.MapBuffer(buffer, offset, size);
            }
        };

        struct View
        {
            Device&        device;
            VriDescriptor* handle = nullptr;

            explicit View(Device& device) :
                device(device)
            {
            }

            ~View()
            {
                if (handle)
                {
                    device.core.DestroyDescriptor(handle);
                }
            }
        };
    } // namespace material_probe

    // Consumes the source-free Tests/ComputeMaterial fixture used by shader_material.cpp.
    inline std::expected<void, std::string> shaderMaterialExpectedProbe(Device& device, const ShaderAsset& source)
    {
        using namespace material_probe;
        if (auto schema = materialSchemaExpectedProbe(source, "gain", "color"); !schema)
        {
            return schema;
        }
        ShaderAsset asset = source;
        asset.variants.push_back({"Alternate", {}, {}, {}, {}, {}});
        for (auto& subshader : asset.subshaders)
        {
            for (auto& pass : subshader.passes)
            {
                pass.programs.emplace("Alternate", pass.programs.at("Default"));
            }
        }
        const VriBufferDesc outputDesc {16,
                                        16,
                                        VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc |
                                            VriBufferUsage_TransferDst,
                                        VriMemoryLocation_Device};
        auto                output = Buffer::create(device, outputDesc);
        auto                other  = Buffer::create(device, outputDesc);
        auto readback = Buffer::create(device, {16, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto zero     = Buffer::create(device, {16, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        if (!output)
        {
            return std::unexpected(output.error());
        }
        if (!other)
        {
            return std::unexpected(other.error());
        }
        if (!readback)
        {
            return std::unexpected(readback.error());
        }
        if (!zero)
        {
            return std::unexpected(zero.error());
        }
        auto* mapped = device.core.MapBuffer((*zero)->handle, 0, 16);
        if (!mapped)
        {
            return std::unexpected("Map material probe zero buffer");
        }
        std::memset(mapped, 0, 16);
        device.core.UnmapBuffer((*zero)->handle);
        View              outputView(device);
        View              otherView(device);
        VriBufferViewDesc view {(*output)->handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 16};
        if (const auto status = device.core.CreateBufferView(device.handle, &view, &outputView.handle);
            status != VriResult_Success)
        {
            return std::unexpected("Create material probe output view");
        }
        view.buffer = (*other)->handle;
        if (const auto status = device.core.CreateBufferView(device.handle, &view, &otherView.handle);
            status != VriResult_Success)
        {
            return std::unexpected("Create material probe alternate view");
        }
        NativeFault      faults(device);
        MaterialInstance instance;
        int              resolverMode = 0;
        auto             material     = ShaderMaterial::create(
            device,
            asset,
            instance,
            [&resolverMode](const auto&, const auto&) -> std::expected<ShaderTextureBinding, std::string>
            {
                if (resolverMode == 0)
                {
                    return std::unexpected("injected texture resolver rejection");
                }
                return ShaderTextureBinding {};
            });
        if (!material)
        {
            return std::unexpected(material.error());
        }
        const std::array resources {
            ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {outputView.handle}}};
        for (const auto failure :
             {Failure::eView, Failure::eLayout, Failure::ePool, Failure::eSets, Failure::ePipeline})
        {
            faults.failure = failure;
            auto rejected  = (*material)->prepareExpected("Experiment", {}, resources);
            if (rejected || (*material)->pipelineCount() || !(*material)->preparedPasses().empty() || !faults.empty())
            {
                return std::unexpected(
                    "Rejected material preparation retained candidate pipelines/layouts/pools/views");
            }
        }
        faults.failure = Failure::eNone;
        if ((*material)->prepareExpected("Experiment", {}) || (*material)->pipelineCount() || !faults.empty())
        {
            return std::unexpected("Missing material binding published a candidate cache entry");
        }
        auto pipeline = (*material)->prepareExpected("Experiment", {}, resources);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        const auto firstPipeline = *pipeline;
        auto       frame         = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        // Reuse the accepted state directly. Clearing A first detects a hidden descriptor switch to B.
        const auto renderAccepted = [&](ShaderMaterial* selectedMaterial = nullptr,
                                        VriPipeline* selectedPipeline =
                                            nullptr) -> std::expected<std::array<float, 4>, std::string>
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            (*output)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            const VriBufferCopyDesc copy {0, 0, 16};
            device.core.CmdCopyBuffer(*commands, (*output)->handle, (*zero)->handle, &copy);
            (*output)->transition(*commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            if (!selectedMaterial)
            {
                selectedMaterial = material->get();
            }
            auto bound = selectedPipeline ? selectedMaterial->bindExpected(*commands, selectedPipeline) :
                                            selectedMaterial->bindExpected(*commands, "Experiment");
            if (!bound)
            {
                auto discarded = (*frame)->discardExpected();
                if (!discarded)
                {
                    return std::unexpected(discarded.error());
                }
                return std::unexpected(bound.error());
            }
            const VriDispatchDesc dispatch {1, 1, 1};
            device.core.CmdDispatch(*commands, &dispatch);
            (*output)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            (*readback)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            device.core.CmdCopyBuffer(*commands, (*readback)->handle, (*output)->handle, &copy);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return std::unexpected(submitted.error());
            }
            auto* data = device.core.MapBuffer((*readback)->handle, 0, 16);
            if (!data)
            {
                return std::unexpected("Map Expected material readback");
            }
            std::array<float, 4> pixel {};
            std::memcpy(pixel.data(), data, 16);
            device.core.UnmapBuffer((*readback)->handle);
            return pixel;
        };
        auto baseline = renderAccepted();
        if (!baseline || std::abs((*baseline)[0] - .214041f) >= .00001f || (*baseline)[1] != 2 ||
            (*baseline)[2] != .5f || (*baseline)[3] != .5f)
        {
            return std::unexpected("Expected reflected material initial pixels differ");
        }
        if (auto changed = instance.setExpected(asset, "gain", 3.f); !changed)
        {
            return changed;
        }
        faults.failure   = Failure::eMap;
        auto rejectedMap = (*material)->prepareExpected("Experiment", {}, resources);
        faults.failure   = Failure::eNone;
        auto retained    = renderAccepted();
        if (rejectedMap || !retained || *retained != *baseline || (*material)->pipelineCount() != 1)
        {
            return std::unexpected("Failed uniform mapping changed accepted material pixels/cache");
        }
        auto recovered = (*material)->prepareExpected("Experiment", {}, resources);
        auto current   = renderAccepted();
        if (!recovered || *recovered != firstPipeline || !current || (*current)[1] != 3)
        {
            return std::unexpected("Expected material parameter retry failed");
        }
        if (auto changed = instance.setExpected(asset, "gain", 5.f); !changed)
        {
            return changed;
        }
        auto invalid     = resources;
        invalid[0].type  = VriDescriptorType_Texture;
        invalid[0].views = {otherView.handle};
        if ((*material)->prepareExpected("Experiment", {}, invalid))
        {
            return std::unexpected("Incompatible material resource was accepted");
        }
        retained = renderAccepted();
        if (!retained || *retained != *current)
        {
            return std::unexpected("Rejected binding changed accepted uniforms/descriptors");
        }
        if (auto selected = instance.setVariantExpected(asset, "Alternate"); !selected)
        {
            return selected;
        }
        faults.failure        = Failure::ePipeline;
        auto rejectedPipeline = (*material)->prepareExpected("Experiment", {}, resources);
        faults.failure        = Failure::eNone;
        retained              = renderAccepted();
        if (rejectedPipeline || !retained || *retained != *current || (*material)->pipelineCount() != 1 ||
            faults.layouts.size() != 1 || faults.pools.size() != 1 || faults.pipelines.size() != 1 ||
            faults.views.size() != 1)
        {
            return std::unexpected("Rejected variant changed live material or retained native candidate owners");
        }
        recovered = (*material)->prepareExpected("Experiment", {}, resources);
        current   = renderAccepted();
        if (!recovered || !current || (*current)[1] != 5 || (*material)->pipelineCount() != 2)
        {
            return std::unexpected("Expected material variant did not recover");
        }
        ShaderTextureValue external;
        external.asset = {StableId::generate()};
        if (auto changed = instance.setExpected(asset, "normalMap", external); !changed)
        {
            return changed;
        }
        auto alternate     = resources;
        alternate[0].views = {otherView.handle};
        for (resolverMode = 0; resolverMode < 2; ++resolverMode)
        {
            if ((*material)->prepareExpected("Experiment", {}, alternate))
            {
                return std::unexpected("Rejected/invalid texture resolver was accepted");
            }
            retained = renderAccepted();
            if (!retained || *retained != *current)
            {
                return std::unexpected("Texture resolver error changed live material bindings");
            }
        }
        if (auto reset = instance.resetExpected("normalMap"); !reset)
        {
            return reset;
        }
        recovered = (*material)->prepareExpected("Experiment", {}, resources);
        retained  = renderAccepted();
        if (!recovered || !retained || *retained != *current || (*material)->bindExpected(nullptr, "Experiment") ||
            (*material)->pushConstantSizeExpected("missing") || (*material)->passForModeExpected("missing"))
        {
            return std::unexpected("Expected material failed recovery or accepted invalid bind/query");
        }
        material->reset();
        if (!faults.empty())
        {
            return std::unexpected("Expected material destruction leaked tracked native owners");
        }
        // Both quoted names are legal. Their (Pass, variant) pairs must not share mutable GPU parameters.
        ShaderAsset collision = source;
        collision.variants    = {{"V", {}, {}, {}, {}, {}}, {"Q:V", {}, {}, {}, {}, {}}};
        auto       firstPass  = collision.subshaders.front().passes.front();
        const auto compiled   = firstPass.programs.at("Default");
        firstPass.name        = "P:Q";
        firstPass.programs.clear();
        firstPass.programs.emplace("V", compiled);
        firstPass.programs.emplace("Q:V", compiled);
        auto secondPass                     = firstPass;
        secondPass.name                     = "P";
        collision.subshaders.front().passes = {std::move(firstPass), std::move(secondPass)};
        MaterialInstance collidingInstance;
        collidingInstance.variant = "V";
        auto colliding            = ShaderMaterial::create(device, collision, collidingInstance);
        if (!colliding)
        {
            return std::unexpected(colliding.error());
        }
        auto firstHandle = (*colliding)->prepareExpected("P:Q", {}, resources);
        if (!firstHandle)
        {
            return std::unexpected(firstHandle.error());
        }
        if (auto changed = collidingInstance.setExpected(collision, "gain", 7.f); !changed)
        {
            return changed;
        }
        if (auto changed = collidingInstance.setVariantExpected(collision, "Q:V"); !changed)
        {
            return changed;
        }
        auto secondHandle = (*colliding)->prepareExpected("P", {}, resources);
        if (!secondHandle)
        {
            return std::unexpected(secondHandle.error());
        }
        auto firstPixels  = renderAccepted(colliding->get(), *firstHandle);
        auto secondPixels = renderAccepted(colliding->get(), *secondHandle);
        if (!firstPixels)
        {
            return std::unexpected(firstPixels.error());
        }
        if (!secondPixels)
        {
            return std::unexpected(secondPixels.error());
        }
        if ((*firstPixels)[1] != 2 || (*secondPixels)[1] != 7)
        {
            return std::unexpected("Distinct legal Pass/variant names aliased accepted GPU parameters: " +
                                   std::to_string((*firstPixels)[1]) + "/" + std::to_string((*secondPixels)[1]));
        }
        colliding->reset();
        if (!faults.empty())
        {
            return std::unexpected("Colliding-name fixture leaked native owners");
        }
        std::cout
            << "Expected reflected material GPU: 5 native construction failures, missing/type/texture/map "
               "rejection, old pixels/bindings/cache retention, variant recovery and distinct-name GPU state passed\n";
        return {};
    }
} // namespace vultra::test
