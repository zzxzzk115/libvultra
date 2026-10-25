#pragma once

#include <vultra/assets/material_instance.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>

#include <array>
#include <functional>
#include <map>
#include <optional>

namespace vultra
{
    class AssetSource;

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
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        void mapBlock(const ShaderSourceBlock& block, size_t& cursor);
#endif
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

#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        static ShaderAsset parse(const std::filesystem::path& file, std::string_view text);
        static ShaderAsset compile(const std::filesystem::path& file,
                                   const ShaderCompileOptions&  options  = {},
                                   std::span<const std::string> variants = {});
        static ShaderAsset compileSource(const std::filesystem::path& file,
                                         std::string_view             source,
                                         const ShaderCompileOptions&  options  = {},
                                         std::span<const std::string> variants = {});
        static bool        cook(const std::filesystem::path& source,
                                const std::filesystem::path& output,
                                const ShaderCompileOptions&  options  = {},
                                std::span<const std::string> variants = {});
#endif
        static ShaderAsset    load(const std::filesystem::path& file, const AssetSource* source = nullptr);
        void                  save(const std::filesystem::path& file) const;
        void                  validate() const;
        const ShaderProperty& property(std::string_view name) const;
        const ShaderVariant&  variant(std::string_view name) const;
        uint32_t              selectSubshader(std::string_view                  pipeline,
                                              uint64_t                          features,
                                              std::span<const std::string_view> lightModes,
                                              std::string&                      diagnostics,
                                              const SubshaderCompatibility&     compatible = {}) const;
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        std::vector<ShaderSourceProjection> projectSources() const;
        std::string                         generatedMaterialSource() const;
        std::string                         generatedPassSource(uint32_t subshader, uint32_t pass) const;
#endif
    };

} // namespace vultra
