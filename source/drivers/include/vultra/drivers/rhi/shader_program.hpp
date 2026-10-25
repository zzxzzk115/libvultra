#pragma once

#include <vri/vri.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    struct ShaderEntry
    {
        std::string        name;
        VriShaderStageBits stage;
    };

    struct CompiledShader
    {
        ShaderEntry             entry;
        std::vector<uint32_t>   words;
        uint32_t                colorOutputs = 0;
        std::array<uint32_t, 3> threadGroup {};
        std::string             wgsl;
    };

    enum class ShaderParameterKind
    {
        eNone,
        eScalar,
        eVector,
        eMatrix,
        eStruct,
        eArray,
        eConstantBuffer,
        eParameterBlock,
        eResource,
        eSampler
    };

    enum class ShaderScalarType
    {
        eNone,
        eFloat,
        eInteger,
        eUnsigned,
        eBoolean,
        eOther
    };

    enum class ShaderOffsetKind
    {
        eUniform,
        eDescriptor,
        eRegisterSpace,
        ePushConstant
    };

    struct ShaderParameterOffset
    {
        ShaderOffsetKind kind;
        uint64_t         offset = 0;
        uint64_t         space  = 0;
    };

    // Container and element nodes retain relative layouts, including implicit global containers.
    struct ShaderParameter
    {
        std::string         name;
        std::string         typeName;
        ShaderParameterKind kind         = ShaderParameterKind::eNone;
        ShaderScalarType    scalar       = ShaderScalarType::eNone;
        uint64_t            size         = 0;
        uint64_t            stride       = 0;
        uint64_t            count        = 0;
        uint64_t            matrixStride = 0;
        uint32_t            rows         = 0;
        uint32_t            columns      = 0;
        bool                columnMajor  = true;
        // Compiler reflection is normalized to VRI before it enters a cooked artifact.
        std::optional<VriDescriptorType>   descriptorType;
        VriTextureViewType                 viewType = VriTextureViewType_2D;
        std::vector<ShaderParameterOffset> offsets;
        std::vector<ShaderParameter>       fields;

        uint64_t               offset(ShaderOffsetKind kind) const;
        uint64_t               space(ShaderOffsetKind kind) const;
        const ShaderParameter* field(std::string_view name) const;
    };

    struct ShaderDefine
    {
        std::string name;
        std::string value;
    };

    struct ShaderCompileOptions
    {
        std::vector<ShaderEntry>           entries;
        std::vector<std::filesystem::path> includeDirectories;
        std::vector<std::filesystem::path> linkModules;
        std::vector<ShaderDefine>          defines;
        std::vector<ShaderDefine>          linkSources;            // Module name and ordinary Slang source text.
        std::string                        profile  = "spirv_1_5"; // SPIR-V profile or "wgsl".
        bool                               rayQuery = false;
        std::vector<std::string>           capabilities;
    };

    struct ShaderDependency
    {
        std::filesystem::path path;
        uint64_t              hash = 0;
    };

    struct ShaderResourceBinding
    {
        std::string        name;
        VriDescriptorType  type         = VriDescriptorType_Texture;
        uint32_t           set          = 0;
        uint32_t           binding      = 0;
        uint32_t           count        = 1;
        uint64_t           uniformSize  = 0;
        VriTextureViewType viewType     = VriTextureViewType_2D;
        bool               pushConstant = false;
    };

    // Owns one target's code, layout and entry names; descriptor views expire when this program changes.
    struct ShaderProgram
    {
        std::vector<CompiledShader>   shaders;
        std::string                   diagnostics;
        bool                          rayQuery = false;
        std::string                   profile  = "spirv_1_5";
        ShaderParameter               parameters;
        std::vector<ShaderDependency> dependencies;
        std::string                   compileKey;
        std::vector<std::string>      capabilities;

#ifdef VULTRA_WITH_SHADER_COMPILER
        // Empty entries discovers all annotated shader entries.
        static ShaderProgram compile(const std::filesystem::path&           file,
                                     std::span<const ShaderEntry>           entries  = {},
                                     std::span<const std::filesystem::path> includes = {},
                                     bool                                   rayQuery = false);
        static ShaderProgram compile(const std::filesystem::path& file, const ShaderCompileOptions& options);
        static ShaderProgram compileSource(const std::filesystem::path& sourcePath,
                                           std::string_view             source,
                                           const ShaderCompileOptions&  options = {});
        static bool          cook(const std::filesystem::path& source,
                                  const std::filesystem::path& output,
                                  const ShaderCompileOptions&  options = {});
        static std::string   compilerVersion();
#endif
        static ShaderProgram       load(const std::filesystem::path& file);
        static ShaderProgram       load(std::span<const std::byte> bytes, const std::filesystem::path& identity);
        void                       save(const std::filesystem::path& file) const;
        std::vector<VriShaderDesc> descriptors(std::span<const ShaderEntry> entries) const;
        std::vector<ShaderResourceBinding> resourceBindings() const;
    };
} // namespace vultra
