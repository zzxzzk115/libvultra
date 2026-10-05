#pragma once

#include <vultra/assets/project_manifest.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <variant>

namespace vultra
{
    enum class ShaderPropertyType
    {
        eFloat,
        eRange,
        eInteger,
        eBoolean,
        eVector,
        eColor,
        eTexture2D,
        eTexture2DArray,
        eTexture3D,
        eTextureCube,
        eTextureCubeArray
    };

    struct ShaderTextureValue
    {
        AssetId              asset {};
        std::string          builtin;
        std::array<float, 4> scaleOffset {1, 1, 0, 0};
        friend bool          operator==(const ShaderTextureValue&, const ShaderTextureValue&) = default;
    };

    using ShaderPropertyValue = std::variant<float, int32_t, bool, std::array<float, 4>, ShaderTextureValue>;

    struct ShaderSourceLocation
    {
        std::filesystem::path file;
        uint32_t              line   = 1;
        uint32_t              column = 1;
        std::string           describe() const;
    };

    struct ShaderProperty
    {
        std::string                                  name;
        std::string                                  label;
        ShaderPropertyType                           type          = ShaderPropertyType::eFloat;
        ShaderPropertyValue                          defaultValue  = 0.0f;
        bool                                         hdr           = false;
        bool                                         normal        = false;
        bool                                         srgb          = false;
        bool                                         hidden        = false;
        bool                                         noScaleOffset = false;
        double                                       minimum       = 0;
        double                                       maximum       = 1;
        std::vector<std::pair<std::string, int32_t>> enumeration;
        ShaderSourceLocation                         location;
        bool                                         isTexture() const;
        void                                         validate(const ShaderPropertyValue& value) const;
    };

    struct ShaderStateValue
    {
        std::string value;
        bool        property = false;
    };

    // Authoring commands are resolved to ordinary VRI state by the rendering module.
    struct ShaderState
    {
        std::map<std::string, std::vector<ShaderStateValue>, std::less<>> commands;
        ShaderSourceLocation                                              location;
    };

    struct ShaderSourceBlock
    {
        std::string          text;
        ShaderSourceLocation location;
    };

    struct ShaderSourceMapping
    {
        uint32_t             generatedLine;
        uint32_t             lineCount;
        ShaderSourceLocation original;
        bool                 editable        = true;
        uint32_t             generatedColumn = 1;
    };

    struct ShaderSourceProjection
    {
        std::string                      name;
        std::string                      text;
        std::vector<ShaderSourceMapping> mappings;
        void                             mapBlock(const ShaderSourceBlock& block, size_t& cursor);
    };

    struct ShaderVariant
    {
        std::string                        name;
        std::string                        constants;
        std::vector<std::filesystem::path> modules;
        std::vector<ShaderDefine>          defines;
        ShaderSourceLocation               location;
    };

    struct ShaderPass
    {
        std::string                                       name;
        std::string                                       lightMode = "Custom";
        std::vector<ShaderEntry>                          entries;
        ShaderState                                       state;
        uint64_t                                          requiredFeatures = 0;
        ShaderSourceBlock                                 source;
        std::map<std::string, ShaderProgram, std::less<>> programs;
        bool                                              generated = false;
        ShaderSourceLocation                              location;
        const ShaderProgram&                              program(std::string_view variant) const;
    };

    struct ShaderSurface
    {
        std::string          model;
        std::string          entry;
        ShaderState          state;
        ShaderSourceBlock    source;
        ShaderSourceLocation location;
    };

    struct ShaderSubshader
    {
        std::map<std::string, std::string, std::less<>> tags;
        ShaderState                                     state;
        uint64_t                                        requiredFeatures = 0;
        std::vector<ShaderSourceBlock>                  common;
        std::optional<ShaderSurface>                    surface;
        std::vector<ShaderPass>                         passes;
        ShaderSourceLocation                            location;
    };

    // CPU asset only. GPU objects and command recording belong to the rendering module.
    struct ShaderAsset
    {
        using SubshaderCompatibility = std::function<std::string(const ShaderSubshader&)>;
        std::string                  name;
        std::filesystem::path        sourcePath;
        std::vector<ShaderProperty>  properties;
        std::vector<ShaderVariant>   variants;
        std::vector<ShaderSubshader> subshaders;
        std::string                  compileKey;
        std::string                  diagnostics;

        static ShaderAsset                  parse(const std::filesystem::path& file, std::string_view text);
        static ShaderAsset                  compile(const std::filesystem::path& file,
                                                    const ShaderCompileOptions&  options  = {},
                                                    std::span<const std::string> variants = {});
        static ShaderAsset                  compileSource(const std::filesystem::path& file,
                                                          std::string_view             source,
                                                          const ShaderCompileOptions&  options  = {},
                                                          std::span<const std::string> variants = {});
        static bool                         cook(const std::filesystem::path& source,
                                                 const std::filesystem::path& output,
                                                 const ShaderCompileOptions&  options  = {},
                                                 std::span<const std::string> variants = {});
        static ShaderAsset                  load(const std::filesystem::path& file);
        void                                save(const std::filesystem::path& file) const;
        void                                validate() const;
        const ShaderProperty&               property(std::string_view name) const;
        const ShaderVariant&                variant(std::string_view name) const;
        uint32_t                            selectSubshader(std::string_view                  pipeline,
                                                            uint64_t                          features,
                                                            std::span<const std::string_view> lightModes,
                                                            std::string&                      diagnostics,
                                                            const SubshaderCompatibility&     compatible = {}) const;
        std::vector<ShaderSourceProjection> projectSources() const;
        std::string                         generatedMaterialSource() const;
        std::string                         generatedPassSource(uint32_t subshader, uint32_t pass) const;
    };

    // Serializable CPU overrides. Setters use the selected asset's property definitions.
    class MaterialInstance
    {
    public:
        AssetId                                                        shader {};
        std::string                                                    variant = "Default";
        const std::map<std::string, ShaderPropertyValue, std::less<>>& overrides() const;
        ShaderPropertyValue value(const ShaderAsset& asset, std::string_view name) const;
        void                set(const ShaderAsset& asset, std::string_view name, ShaderPropertyValue value);
        void                reset(std::string_view name);
        void                setVariant(const ShaderAsset& asset, std::string value);
        void                validate(const ShaderAsset& asset) const;
        void                assign(const ShaderAsset& asset, const MaterialInstance& source);
        void     reconcile(const ShaderAsset& previous, const ShaderAsset& candidate, std::string& diagnostics);
        uint64_t revision() const;
        std::vector<std::byte>  uniformData(const ShaderAsset& asset, const ShaderProgram& program) const;
        std::string             serialize() const;
        static MaterialInstance parse(std::string_view text);

    private:
        std::map<std::string, ShaderPropertyValue, std::less<>> m_Overrides;
        uint64_t                                                m_Revision = 0;
    };
} // namespace vultra
