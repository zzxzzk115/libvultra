#pragma once
#include <vultra/drivers/rhi/device.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace vultra
{
    class ShaderPipeline
    {
    public:
        // Builder uses ordinary VRI descriptors; layout/target formats stay in the experiment.
        using Builder = std::function<VriPipeline*(std::span<const VriShaderDesc>)>;
        // Source files compile and watch; .vshaderc files load without a watcher. If a .slang source is
        // absent, load its .vshaderc sibling. Invalid cooked files never fall back to compilation.
        // watchDirectory defaults to the entry's directory. Use a shared root for sibling lib/resources folders.
        // Search the entry directory first, then includeDirectories in order. Paths resolve at construction.
        ShaderPipeline(Device&                            device,
                       std::filesystem::path              file,
                       std::vector<ShaderEntry>           entries,
                       Builder                            builder,
                       std::filesystem::path              watchDirectory     = {},
                       std::vector<std::filesystem::path> includeDirectories = {});
        ShaderPipeline(Device&               device,
                       std::filesystem::path file,
                       ShaderCompileOptions  options,
                       Builder               builder,
                       std::filesystem::path watchDirectory = {});
        ~ShaderPipeline();
        ShaderPipeline(const ShaderPipeline&)            = delete;
        ShaderPipeline& operator=(const ShaderPipeline&) = delete;
        bool            poll();   // main thread, between completed frames; debounced file notifications
        bool            reload(); // failed compile/build preserves the previous pipeline

        VriPipeline* handle() const
        {
            return m_Pipeline;
        }

        const std::string& diagnostics() const
        {
            return m_Diagnostics;
        }

        uint64_t generation() const
        {
            return m_Generation;
        }

    private:
        struct Watch;
        Device&                m_Device;
        std::filesystem::path  m_File;
        ShaderCompileOptions   m_CompileOptions;
        Builder                m_Builder;
        VriPipeline*           m_Pipeline = nullptr;
        std::string            m_Diagnostics;
        uint64_t               m_Generation = 0;
        std::unique_ptr<Watch> m_Watch;
    };
} // namespace vultra
