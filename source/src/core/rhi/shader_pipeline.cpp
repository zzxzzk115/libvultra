#include <vultra/core/base/logger.hpp>
#include <vultra/core/rhi/shader_pipeline.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005 4068) // upstream FileWatch macros and #pragma mark
#endif
#include <FileWatch.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <slang-com-ptr.h>
#include <slang.h>

#include <atomic>
#include <chrono>

namespace vultra
{
    struct ShaderPipeline::Watch
    {
        std::atomic<uint64_t>                 revision {0};
        uint64_t                              observed = 0;
        bool                                  pending  = false;
        std::chrono::steady_clock::time_point changed {};
        // Destroy first: FileWatch joins its thread before callback state is destroyed.
        std::unique_ptr<filewatch::FileWatch<std::string>> watcher;
    };

    ShaderPipeline::ShaderPipeline(Device&                            device,
                                   std::filesystem::path              file,
                                   std::vector<ShaderEntry>           entries,
                                   Builder                            builder,
                                   std::filesystem::path              watchDirectory,
                                   std::vector<std::filesystem::path> includeDirectories) :
        m_Device(device),
        m_File(std::filesystem::absolute(file)),
        m_Entries(std::move(entries)),
        m_Builder(std::move(builder)),
        m_Watch(std::make_unique<Watch>())
    {
        if (m_Entries.empty())
        {
            throw std::invalid_argument("Shader entry points cannot be empty");
        }
        m_SearchPaths.push_back(m_File.parent_path().string());
        for (const auto& directory : includeDirectories)
        {
            m_SearchPaths.push_back(std::filesystem::absolute(directory).lexically_normal().string());
        }
        if (!reload())
        {
            throw std::runtime_error(m_Diagnostics);
        }
        // Watch the shader directory tree, including includes and imported modules.
        try
        {
            m_Watch->watcher = std::make_unique<filewatch::FileWatch<std::string>>(
                (watchDirectory.empty() ? m_File.parent_path() : std::filesystem::absolute(watchDirectory)).string(),
                [watch = m_Watch.get()](const std::string&, filewatch::Event)
                {
                    watch->revision.fetch_add(1, std::memory_order_relaxed);
                });
        }
        catch (...)
        {
            m_Device.core.DestroyPipeline(m_Pipeline);
            throw;
        }
    }

    ShaderPipeline::~ShaderPipeline()
    {
        m_Watch.reset();
        m_Device.waitIdle();
        if (m_Pipeline)
        {
            m_Device.core.DestroyPipeline(m_Pipeline);
        }
    }

    bool ShaderPipeline::poll()
    {
        const auto revision = m_Watch->revision.load(std::memory_order_relaxed);
        const auto now      = std::chrono::steady_clock::now();
        if (revision != m_Watch->observed)
        {
            m_Watch->observed = revision;
            m_Watch->changed  = now;
            m_Watch->pending  = true;
        }
        if (!m_Watch->pending || now - m_Watch->changed < std::chrono::milliseconds(150))
        {
            return false;
        }
        m_Watch->pending = false;
        return reload();
    }

    bool ShaderPipeline::reload()
    {
        using Slang::ComPtr;
        m_Diagnostics.clear();
        auto diagnose = [&](SlangResult result, const ComPtr<slang::IBlob>& blob, const char* operation)
        {
            if (blob)
            {
                m_Diagnostics += static_cast<const char*>(blob->getBufferPointer());
            }
            if (SLANG_FAILED(result))
            {
                throw std::runtime_error(operation);
            }
        };
        try
        {
            ComPtr<slang::IGlobalSession> global;
            diagnose(slang::createGlobalSession(global.writeRef()), {}, "Create Slang global session");
            slang::TargetDesc target {};
            target.format  = SLANG_SPIRV;
            target.profile = global->findProfile("spirv_1_5");
            std::vector<slang::CompilerOptionEntry> options(2);
            options[0].name = slang::CompilerOptionName::VulkanUseEntryPointName;
            options[1].name = slang::CompilerOptionName::EmitSpirvDirectly;
            for (auto& option : options)
            {
                option.value.kind      = slang::CompilerOptionValueKind::Int;
                option.value.intValue0 = 1;
            }
            if (m_Device.core.GetDeviceDesc(m_Device.handle)->hasRayQuery)
            {
                slang::CompilerOptionEntry capability {};
                capability.name            = slang::CompilerOptionName::Capability;
                capability.value.kind      = slang::CompilerOptionValueKind::Int;
                capability.value.intValue0 = global->findCapability("spvRayQueryKHR");
                options.push_back(capability);
            }
            std::vector<const char*> searchPaths;
            for (const auto& directory : m_SearchPaths)
            {
                searchPaths.push_back(directory.c_str());
            }
            slang::SessionDesc sd {};
            sd.targets                  = &target;
            sd.targetCount              = 1;
            sd.searchPaths              = searchPaths.data();
            sd.searchPathCount          = SlangInt(searchPaths.size());
            sd.defaultMatrixLayoutMode  = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
            sd.compilerOptionEntries    = options.data();
            sd.compilerOptionEntryCount = uint32_t(options.size());
            // A new session discards Slang's cached modules on every reload.
            ComPtr<slang::ISession> session;
            diagnose(global->createSession(sd, session.writeRef()), {}, "Create Slang session");
            ComPtr<slang::IBlob> diagnostics;
            auto*                module = session->loadModule(m_File.stem().string().c_str(), diagnostics.writeRef());
            diagnose(module ? SLANG_OK : SLANG_FAIL, diagnostics, "Load shader module");
            std::vector<ComPtr<slang::IEntryPoint>> entries(m_Entries.size());
            std::vector<slang::IComponentType*>     components {module};
            for (size_t i = 0; i < entries.size(); ++i)
            {
                diagnose(module->findEntryPointByName(m_Entries[i].name.c_str(), entries[i].writeRef()),
                         {},
                         "Find shader entry point");
                components.push_back(entries[i]);
            }
            ComPtr<slang::IComponentType> composed;
            ComPtr<slang::IComponentType> linked;
            diagnostics.setNull();
            auto result = session->createCompositeComponentType(components.data(),
                                                                SlangInt(components.size()),
                                                                composed.writeRef(),
                                                                diagnostics.writeRef());
            diagnose(result, diagnostics, "Compose shader");
            diagnostics.setNull();
            result = composed->link(linked.writeRef(), diagnostics.writeRef());
            diagnose(result, diagnostics, "Link shader");
            std::vector<ComPtr<slang::IBlob>> code(entries.size());
            std::vector<VriShaderDesc>        shaders(entries.size());
            for (size_t i = 0; i < entries.size(); ++i)
            {
                diagnostics.setNull();
                result = linked->getEntryPointCode(SlangInt(i), 0, code[i].writeRef(), diagnostics.writeRef());
                diagnose(result, diagnostics, "Emit SPIR-V");
                shaders[i].stage          = m_Entries[i].stage;
                shaders[i].entryPointName = m_Entries[i].name.c_str();
                shaders[i].bytecode       = code[i]->getBufferPointer();
                shaders[i].bytecodeSize   = code[i]->getBufferSize();
            }
            VriPipeline* replacement = m_Builder(shaders);
            if (!replacement)
            {
                throw std::runtime_error("Pipeline builder returned null");
            }
            m_Device.waitIdle();
            if (m_Pipeline)
            {
                m_Device.core.DestroyPipeline(m_Pipeline);
            }
            m_Pipeline = replacement;
            ++m_Generation;
            return true;
        }
        catch (const std::exception& error)
        {
            m_Diagnostics += error.what();
            Logger::core().error("[Shader] {}", m_Diagnostics);
            return false;
        }
    }
} // namespace vultra
