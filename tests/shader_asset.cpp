#include "support/material_expected_probe.hpp"
#include "support/material_persistence_expected_probe.hpp"
#include "support/shader_asset_expected_probe.hpp"

#include <vultra/assets/shader_asset.hpp>
#include <vultra/platform/os/file.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Callback>
    void reject(Callback&& callback, std::string_view expected)
    {
        try
        {
            callback();
        }
        catch (const std::exception& error)
        {
            if (std::string_view(error.what()).find(expected) == std::string_view::npos)
            {
                throw std::runtime_error("Expected " + std::string(expected) + "; received: " + error.what());
            }
            return;
        }
        throw std::runtime_error("Invalid shader input was accepted");
    }

    constexpr std::string_view kSource = R"(Shader "Tests/Material"
{
    Properties
    {
        roughness ("Roughness", Range(0.03, 1)) = 0.4
        baseColor ("Base Color", Color) = (0.8, 0.2, 0.1, 1)
        enabled ("Enabled", Boolean) = true
        [Enum(Off, 0, On, 1)] cull ("Cull", Integer) = 1
    }
    Variant "Default" {}
    Variant "Detail"
    {
        Constants { bool useDetail = true int sampleCount = 4 }
        Defines { DETAIL = 1 }
    }
    SubShader
    {
        Cull [cull]
        Pass
        {
            Name "Compute"
            Compute computeMain
            SLANGPROGRAM
            /*
ENDSLANG
            */
            // ENDSLANG is also legal in comments and identifiers.
            static const int ENDSLANG_identifier = 1;
            [numthreads(1, 1, 1)]
            void computeMain(uint3 index : SV_DispatchThreadID) {}
            ENDSLANG
        }
    }
})";

    std::expected<void, std::string> authoringExpected(const std::filesystem::path& root)
    {
        using namespace vultra;
        const auto source = root / "expected.vshader";
        const auto cooked = root / "expected.vshaderc";
        auto       parsed = ShaderAsset::parseExpected(source, kSource);
        if (!parsed)
        {
            return std::unexpected(parsed.error());
        }
        auto material  = parsed->generatedMaterialSourceExpected();
        auto generated = parsed->generatedPassSourceExpected(0, 0);
        auto projected = parsed->projectSourcesExpected();
        if (!material || !generated || !projected || projected->size() != 1 ||
            projected->front().mappings.size() < parsed->properties.size() ||
            parsed->generatedPassSourceExpected(1, 0) || parsed->generatedPassSourceExpected(0, 1))
        {
            return std::unexpected("Expected shader generation/projection or index rejection failed");
        }
        ShaderSourceProjection missing;
        missing.text  = "unrelated";
        size_t cursor = 3;
        if (missing.mapBlockExpected(parsed->subshaders.front().passes.front().source, cursor) || cursor != 3 ||
            !missing.mappings.empty())
        {
            return std::unexpected("Rejected source mapping mutated accepted mappings/cursor");
        }
        for (const auto& [from, to] :
             std::vector<std::pair<std::string, std::string>> {{"Range(0.03, 1)", "Range(1, 0)"},
                                                               {"= 0.4", "= 1e999"},
                                                               {"Integer) = 1", "Integer) = 2147483648"},
                                                               {"Cull [cull]", "Cull [cull] Cull Off"},
                                                               {"bool useDetail = true", "bool useDetail = invalid"},
                                                               {"int sampleCount = 4", "int sampleCount = 1.5"},
                                                               {"Tests/Material", "Tests/\\q"}})
        {
            std::string invalid(kSource);
            invalid.replace(invalid.find(from), from.size(), to);
            auto result = ShaderAsset::parseExpected(source, invalid);
            if (result || result.error().find(source.filename().string()) == std::string::npos)
            {
                return std::unexpected("Expected authoring semantic rejection lost source context: " + from);
            }
        }
        if (ShaderAsset::parseExpected(source, "Shader { broken") ||
            ShaderAsset::parseExpected("invalid.slang", kSource) ||
            ShaderAsset::parseExpected(source, std::string(kSource) + char(0xff)))
        {
            return std::unexpected("Expected authoring syntax/extension/UTF-8 rejection failed");
        }
        ShaderCompileOptions invalidOptions;
        invalidOptions.entries = {{"computeMain", VriShaderStage_Compute}};
        const std::array unknown {std::string("Unknown")};
        const std::array duplicate {std::string("Default"), std::string("Default")};
        if (ShaderAsset::compileExpected(root / "absent.vshader") ||
            ShaderAsset::compileSourceExpected(source, kSource, invalidOptions) ||
            ShaderAsset::compileSourceExpected(source, kSource, {}, unknown) ||
            ShaderAsset::compileSourceExpected(source, kSource, {}, duplicate))
        {
            return std::unexpected("Expected authoring accepted missing source, external entries or invalid variants");
        }
        std::string invalid(kSource);
        invalid.replace(invalid.find("void computeMain"), 16, "void missingMain");
        auto rejected = ShaderAsset::compileSourceExpected(source, invalid);
        if (rejected || rejected.error().find("Pass Compute, variant Default") == std::string::npos)
        {
            return std::unexpected("Expected Slang failure lost Pass/variant context");
        }
        if (auto written = writeFileAtomicallyExpected(source, std::as_bytes(std::span(kSource))); !written)
        {
            return written;
        }
        auto first  = ShaderAsset::cookExpected(source, cooked);
        auto hit    = ShaderAsset::cookExpected(source, cooked);
        auto assets = AssetSource::create(root);
        if (!first || !*first || !hit || *hit || !assets)
        {
            return std::unexpected("Expected game shader cook/cache-hit failed");
        }
        auto before = (*assets)->readExpected(cooked.filename());
        if (!before)
        {
            return std::unexpected(before.error());
        }
        if (auto written = writeFileAtomicallyExpected(source, std::as_bytes(std::span(invalid))); !written)
        {
            return written;
        }
        auto failedCook = ShaderAsset::cookExpected(source, cooked);
        auto retained   = (*assets)->readExpected(cooked.filename());
        if (failedCook || !retained || *retained != *before || !ShaderAsset::loadExpected(cooked))
        {
            return std::unexpected("Rejected source cook replaced the accepted artifact");
        }
        if (auto written = writeFileAtomicallyExpected(source, std::as_bytes(std::span(kSource))); !written)
        {
            return written;
        }
        const auto corrupt = std::span(*before).first(15);
        if (auto written = writeFileAtomicallyExpected(cooked, corrupt); !written)
        {
            return written;
        }
        auto rebuilt   = ShaderAsset::cookExpected(source, cooked);
        auto recovered = (*assets)->readExpected(cooked.filename());
        if (!rebuilt || !*rebuilt || !recovered || *recovered != *before)
        {
            return std::unexpected("Expected game shader cook failed to recover an unreadable cache");
        }
        std::puts("Expected game shader authoring: semantic/syntax/UTF-8/Slang errors, source projection, "
                  "cache hits, failed-cook retention and recovery passed");
        return {};
    }

    void variantBindings(const std::filesystem::path& root)
    {
        using namespace vultra;
        const auto asset  = ShaderAsset::compileSource(root / "variant_bindings.vshader", R"(
Shader "Tests/VariantBindings"
{
    Variant "Base" { Defines { EXTRA = 0 } }
    Variant "Extra" { Defines { EXTRA = 1 } }
    SubShader
    {
        Pass
        {
            Name "Compute"
            Compute computeMain
            SLANGPROGRAM
            [[vk::binding(0, 3)]] RWStructuredBuffer<float> output;
            #if EXTRA
            [[vk::binding(1, 3)]] StructuredBuffer<float> extra;
            #endif
            [numthreads(1, 1, 1)]
            void computeMain(uint3 index : SV_DispatchThreadID)
            {
                output[index.x] = 1;
                #if EXTRA
                output[index.x] += extra[index.x];
                #endif
            }
            ENDSLANG
        }
    }
})");
        const auto cooked = root / "variant_bindings.vshaderc";
        asset.save(cooked);
        const auto restored = ShaderAsset::load(cooked);
        // A variant can change descriptor layout, not just code; the archive must preserve that distinction.
        for (const auto* candidate : {&asset, &restored})
        {
            const auto& pass   = candidate->subshaders.front().passes.front();
            const auto  base   = pass.program("Base").resourceBindings();
            const auto  extra  = pass.program("Extra").resourceBindings();
            const auto  output = std::ranges::find(base, "output", &ShaderResourceBinding::name);
            const auto  added  = std::ranges::find(extra, "extra", &ShaderResourceBinding::name);
            require(output != base.end() && output->set == 3 && output->binding == 0 &&
                        output->type == VriDescriptorType_StorageBuffer,
                    "Base variant lost its storage buffer binding");
            require(std::ranges::find(base, "extra", &ShaderResourceBinding::name) == base.end(),
                    "Base variant acquired a descriptor from another variant");
            require(extra.size() == base.size() + 1 && added != extra.end() && added->set == 3 && added->binding == 1 &&
                        added->type == VriDescriptorType_StructuredBuffer,
                    "Variant-specific descriptor was lost during compilation or cooking");
            require(pass.program("Base").compileKey != pass.program("Extra").compileKey,
                    "Variant defines did not distinguish compiled program identities");
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    auto persisted = test::materialPersistenceExpectedProbe();
    require(bool(persisted), persisted ? "" : persisted.error().c_str());
    auto asset = ShaderAsset::parse("build/.tmp/material.vshader", kSource);
    require(asset.properties.size() == 4 && asset.variants.size() == 2, "Shader sections were lost");
    require(asset.variant("Detail").constants.find("export static const int sampleCount = 4;") != std::string::npos,
            "Typed Slang link constant was lost");
    require(asset.subshaders[0].passes[0].source.text.find("ENDSLANG_identifier") != std::string::npos,
            "Slang island was prematurely terminated");
    require(asset.subshaders[0].passes[0].source.location.line == 24, "Slang source line map changed");

    const auto island = ShaderAsset::parse("islands.vshader", R"shader(Shader "Tests/Islands"
{
    SubShader { Pass { Name "Islands" Compute main SLANGPROGRAM
#define END_TEXT \
ENDSLANG
// The preprocessor continuation is native Slang, not a block terminator.
static const char* value = "ENDSLANG";
#define RAW_VALUE R"vultra(embedded "quote"
ENDSLANG
)vultra"
static const char* rawValue = R"marker(embedded "quote" and a mismatched )other"
ENDSLANG
)marker";
/* ENDSLANG */
[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {}
ENDSLANG
    } }
})shader");
    require(island.subshaders[0].passes[0].source.text.find("void main") != std::string::npos,
            "String/comment/preprocessor text ended the Slang island");

    require(island.subshaders[0].passes[0].source.text.find("rawValue") != std::string::npos,
            "Raw Slang strings or multiline raw macro values ended the island");

    MaterialInstance instance;
    instance.shader = {StableId::generate()};
    instance.set(asset, "roughness", 2.0f);
    require(std::get<float>(instance.value(asset, "roughness")) == 2.0f, "Range silently clamped a value");
    const auto revision = instance.revision();
    instance.set(asset, "roughness", 2.0f);
    require(instance.revision() == revision, "Unchanged parameter caused work");
    instance.setVariant(asset, "Detail");
    const auto restored = MaterialInstance::parse(instance.serialize());
    require(restored.shader == instance.shader && restored.variant == "Detail" &&
                restored.overrides() == instance.overrides(),
            "Material persistence lost typed values");
    reject(
        [&]
        {
            instance.set(asset, "roughness", int32_t(1));
        },
        "type");
    reject(
        [&]
        {
            instance.set(asset, "roughness", std::numeric_limits<float>::infinity());
        },
        "finite");
    reject(
        [&]
        {
            instance.set(asset, "missing", 1.0f);
        },
        "missing");
    reject(
        [&]
        {
            instance.setVariant(asset, "Missing");
        },
        "Missing");

    auto broken = std::string(kSource);
    broken.replace(broken.find("Range(0.03, 1)"), 14, "Range(1, 0)");
    reject(
        [&]
        {
            ShaderAsset::parse("invalid.vshader", broken);
        },
        "minimum");
    broken = kSource;
    broken.replace(broken.find("roughness ("), 9, "baseColor");
    reject(
        [&]
        {
            ShaderAsset::parse("invalid.vshader", broken);
        },
        "duplicate property");
    broken = kSource;
    broken.replace(broken.find("Cull [cull]"), 11, "Cull [roughness]");
    reject(
        [&]
        {
            ShaderAsset::parse("invalid.vshader", broken);
        },
        "wrong type");
    broken = kSource;
    broken.replace(broken.find("Compute computeMain"), 19, "Vertex computeMain Compute computeMain");
    reject(
        [&]
        {
            ShaderAsset::parse("invalid.vshader", broken);
        },
        "stage combination");
    broken = kSource;
    broken.replace(broken.find("[Enum("), 6, "[Unknown(");
    reject(
        [&]
        {
            ShaderAsset::parse("invalid.vshader", broken);
        },
        "Unknown property attribute");
    reject(
        [&]
        {
            ShaderAsset::parse("invalid.slang", kSource);
        },
        ".vshader");

    auto changedAsset    = asset;
    auto changedInstance = instance;
    changedInstance.set(asset, "enabled", true);
    std::erase_if(changedAsset.properties,
                  [](const ShaderProperty& property)
                  {
                      return property.name == "roughness";
                  });
    for (auto& property : changedAsset.properties)
    {
        if (property.name == "enabled")
        {
            property.type         = ShaderPropertyType::eFloat;
            property.defaultValue = 0.25f;
        }
    }
    auto added = asset.property("roughness");
    added.name = "newGain";
    changedAsset.properties.push_back(added);
    std::string reconciliation;
    changedInstance.reconcile(asset, changedAsset, reconciliation);
    require(!changedInstance.overrides().contains("roughness") &&
                std::get<float>(changedInstance.value(changedAsset, "enabled")) == 0.25f &&
                std::get<float>(changedInstance.value(changedAsset, "newGain")) == 0.4f &&
                reconciliation.find("roughness") != std::string::npos &&
                reconciliation.find("enabled") != std::string::npos &&
                reconciliation.find("newGain") != std::string::npos,
            "Property migration failed to remove, reset or describe changed fields");
    auto alternatives = asset;
    alternatives.subshaders.push_back(alternatives.subshaders.front());
    std::string selection;
    const auto  selectedSubshader = alternatives.selectSubshader(
        "Vultra",
        0,
        {},
        selection,
        [&](const ShaderSubshader& candidate)
        {
            return &candidate == &alternatives.subshaders.front() ? "attachment contract differs" : "";
        });
    require(selectedSubshader == 1 && selection.find("attachment contract differs") != std::string::npos,
            "SubShader contract rejection did not select the next declaration");

    const auto root = std::filesystem::path("build/.tmp/shader-authoring") / StableId::generate().toString();
    std::filesystem::create_directories(root);
    variantBindings(root);
    auto expectedAuthoring = authoringExpected(root);
    require(bool(expectedAuthoring), expectedAuthoring ? "" : expectedAuthoring.error().c_str());
    const auto source = root / "material.vshader";
    const auto cooked = root / "material.vshaderc";
    writeFileAtomically(source, std::as_bytes(std::span(kSource.data(), kSource.size())));
    const auto  compiled = ShaderAsset::compile(source);
    auto        expectedSchema = test::materialSchemaExpectedProbe(compiled, "roughness", "baseColor");
    require(bool(expectedSchema), expectedSchema ? "" : expectedSchema.error().c_str());
    const auto& program  = compiled.subshaders[0].passes[0].program("Detail");
    const auto  bytes    = instance.uniformData(compiled, program);
    const auto* material = program.parameters.field("material");
    require(material != nullptr, "Material parameter block was not reflected");
    const auto* parameters = material->field("$element");
    require(parameters != nullptr, "Material element layout was not reflected");
    const auto* roughness = parameters->field("roughness");
    require(roughness != nullptr, "Material float layout was not reflected");
    float uploaded = 0;
    std::memcpy(&uploaded, bytes.data() + roughness->offset(ShaderOffsetKind::eUniform), sizeof(uploaded));
    require(uploaded == 2.0f, "Material uniform was written at the wrong offset");
    compiled.save(cooked);
    const auto loaded = ShaderAsset::load(cooked);
    auto       expectedArchive = test::shaderAssetArchiveExpectedProbe(compiled, root / "expected-archive");
    require(bool(expectedArchive), expectedArchive ? "" : expectedArchive.error().c_str());
    require(loaded.properties.size() == compiled.properties.size() &&
                loaded.subshaders[0].passes[0].program("Detail").shaders[0].words == program.shaders[0].words &&
                loaded.subshaders[0].passes[0].source.text.empty(),
            "Cooked game shader lost metadata/code or retained its Slang source");
    reject(
        [&]
        {
            ShaderProgram::load(cooked);
        },
        "raw Slang");
    require(!ShaderAsset::cook(source, cooked), "Unchanged shader missed its cooking cache");
    const std::array selected {std::string("Detail")};
    const auto       stripped = ShaderAsset::compile(source, {}, selected);
    require(stripped.variants.size() == 1, "Explicit shader variant stripping failed");
    reject(
        [&]
        {
            stripped.subshaders[0].passes[0].program("Default");
        },
        "Default");

    std::puts(
        "Shader authoring passed: native islands, typed variants, reflected material layout, persistence and cooking");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
