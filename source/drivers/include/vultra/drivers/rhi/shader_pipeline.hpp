#pragma once
#include <vultra/drivers/rhi/device.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vultra
{
    class AssetSource;
    class ShaderCompiler;

    struct ShaderPipelineIdentity
    {
        std::filesystem::path         file;
        std::string                   compileKey;
        uint64_t                      bytecodeHash = 0;
        std::vector<ShaderDependency> dependencies;
    };

    class ShaderPipeline
    {
    public:
        // Builder uses ordinary VRI descriptors; layout/target formats stay in the experiment.
        using Builder         = std::function<VriPipeline*(std::span<const VriShaderDesc>)>;
        using ExpectedBuilder = std::function<std::expected<VriPipeline*, std::string>(std::span<const VriShaderDesc>)>;
        // Candidate program is borrowed during the call, before any old pipeline is retired or replaced.
        // Validate external layouts here; publish a changed layout with a separate, fully prepared owner.
        using ReflectedBuilder =
            std::function<std::expected<VriPipeline*, std::string>(const ShaderProgram&,
                                                                   std::span<const VriShaderDesc>)>;
        static std::expected<std::unique_ptr<ShaderPipeline>, std::string>
        create(Device& device, const ShaderProgram& program, std::vector<ShaderEntry> entries, ExpectedBuilder builder);
        static std::expected<std::unique_ptr<ShaderPipeline>, std::string>
        create(Device&                            device,
               std::filesystem::path              file,
               std::vector<ShaderEntry>           entries,
               ExpectedBuilder                    builder,
               std::filesystem::path              watchDirectory     = {},
               std::vector<std::filesystem::path> includeDirectories = {},
               const AssetSource*                 source             = nullptr);
        static std::expected<std::unique_ptr<ShaderPipeline>, std::string>
        create(Device&               device,
               std::filesystem::path file,
               ShaderCompileOptions  options,
               ExpectedBuilder       builder,
               std::filesystem::path watchDirectory = {},
               std::filesystem::path cacheDirectory = ".vultra/shaders",
               const AssetSource*    source         = nullptr);
        static std::expected<std::unique_ptr<ShaderPipeline>, std::string>
        create(Device&               device,
               std::filesystem::path file,
               ShaderCompileOptions  options,
               ReflectedBuilder      builder,
               std::filesystem::path watchDirectory = {},
               std::filesystem::path cacheDirectory = ".vultra/shaders",
               const AssetSource*    source         = nullptr);
        // Call between completed frames. A failed replacement retains the old handle, identity and generation.
        std::expected<bool, std::string> pollExpected();
        std::expected<void, std::string> reloadExpected();
        // Last accepted uses of the old handle on every queue. Success transfers those completion owners.
        // Retain layout/descriptors independently; this defers the native pipeline, not arbitrary caller state.
        std::expected<void, std::string> reloadExpected(std::vector<SubmissionCompletion>& lastUses);
        // Source files compile and watch; .vshaderc files load without a watcher. If a .slang source is
        // absent, load its .vshaderc sibling. Invalid cooked files never fall back to compilation.
        // watchDirectory defaults to the entry's directory. Use a shared root for sibling lib/resources folders.
        // Search the entry directory first, then includeDirectories in order. Paths resolve at construction.
        // An explicit source always loads cooked entry bytes and must outlive this pipeline and reload calls.
        ShaderPipeline(Device&                            device,
                       std::filesystem::path              file,
                       std::vector<ShaderEntry>           entries,
                       Builder                            builder,
                       std::filesystem::path              watchDirectory     = {},
                       std::vector<std::filesystem::path> includeDirectories = {},
                       const AssetSource*                 source             = nullptr);
        // Source cache paths resolve at construction; an empty path disables the disk program cache.
        // Cooked programs do not create a compiler or cache.
        ShaderPipeline(Device&               device,
                       std::filesystem::path file,
                       ShaderCompileOptions  options,
                       Builder               builder,
                       std::filesystem::path watchDirectory = {},
                       std::filesystem::path cacheDirectory = ".vultra/shaders",
                       const AssetSource*    source         = nullptr);
        // Builds synchronously from owned bytecode, for example an entry read from an embedded VPK.
        // The program can be destroyed after construction; this pipeline has no file watcher.
        ShaderPipeline(Device& device, const ShaderProgram& program, std::vector<ShaderEntry> entries, Builder builder);
        ~ShaderPipeline();
        ShaderPipeline(const ShaderPipeline&)            = delete;
        ShaderPipeline& operator=(const ShaderPipeline&) = delete;
        bool            poll();   // main thread, between completed frames; debounced file notifications
        bool            reload(); // failed compile/build preserves the previous pipeline

        VriPipeline* handle() const
        {
            return m_Pipeline.get();
        }

        const std::string& diagnostics() const
        {
            return m_Diagnostics;
        }

        uint64_t generation() const
        {
            return m_Generation;
        }

        const ShaderPipelineIdentity& identity() const
        {
            return m_Identity;
        }

    private:
        struct DestroyPipeline
        {
            Device* device;
            void    operator()(VriPipeline* pipeline) const noexcept;
        };

        struct Uninitialized
        {};

        ShaderPipeline(Device& device, Uninitialized);
        std::expected<void, std::string> initializeExpected(std::filesystem::path file,
                                                            ShaderCompileOptions  options,
                                                            std::filesystem::path watchDirectory,
                                                            std::filesystem::path cacheDirectory,
                                                            const AssetSource*    source);
        std::expected<void, std::string> reloadWithCompletionsExpected(std::vector<SubmissionCompletion>* lastUses);
        std::expected<void, std::string> replaceExpected(const ShaderProgram&               program,
                                                         std::vector<SubmissionCompletion>* lastUses = nullptr);
        std::expected<void, std::string> rejectReload(std::string diagnostic);
#ifdef VULTRA_WITH_SHADER_COMPILER
        struct Watch;
#endif
        Device&                                       m_Device;
        const AssetSource*                            m_AssetSource = nullptr;
        std::filesystem::path                         m_File;
        ShaderCompileOptions                          m_CompileOptions;
        ReflectedBuilder                              m_Builder;
        std::unique_ptr<VriPipeline, DestroyPipeline> m_Pipeline;
        std::string                                   m_Diagnostics;
        uint64_t                                      m_Generation = 0;
        ShaderPipelineIdentity                        m_Identity;
#ifdef VULTRA_WITH_SHADER_COMPILER
        std::unique_ptr<ShaderCompiler> m_Compiler;
        std::unique_ptr<Watch>          m_Watch;
#endif
    };
} // namespace vultra
