#include <vultra/assets/shader_asset.hpp>
#include <vultra/platform/os/file.hpp>

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
} // namespace

int main()
try
{
    using namespace vultra;
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
    const auto source = root / "material.vshader";
    const auto cooked = root / "material.vshaderc";
    writeFileAtomically(source, std::as_bytes(std::span(kSource.data(), kSource.size())));
    const auto  compiled = ShaderAsset::compile(source);
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
