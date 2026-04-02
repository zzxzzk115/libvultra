#include "vultra/core/rhi/shader_compiler.hpp"

#include <vshadersystem/compiler.hpp>

#include <cassert>
#include <sstream>

namespace vultra
{
    namespace rhi
    {
        std::filesystem::path ShaderCompiler::s_ShaderRootPath = std::filesystem::current_path() / "shaders";

        namespace
        {
            [[nodiscard]] vshadersystem::ShaderStage toStage(const ShaderType t)
            {
                using SS = vshadersystem::ShaderStage;
                switch (t)
                {
                    case ShaderType::eVertex:
                        return SS::eVert;
                    case ShaderType::eGeometry:
                        return SS::eUnknown; // not supported by vshadersystem currently
                    case ShaderType::eFragment:
                        return SS::eFrag;
                    case ShaderType::eCompute:
                        return SS::eComp;
                    case ShaderType::eRayGen:
                        return SS::eRgen;
                    case ShaderType::eMiss:
                        return SS::eRmiss;
                    case ShaderType::eClosestHit:
                        return SS::eRchit;
                    case ShaderType::eAnyHit:
                        return SS::eRahit;
                    case ShaderType::eIntersect:
                        return SS::eRint;
                    case ShaderType::eMesh:
                        return SS::eMesh;
                    case ShaderType::eTask:
                        return SS::eTask;
                }
                return SS::eUnknown;
            }
        } // namespace

        ShaderCompiler::Result
        ShaderCompiler::compile(const ShaderType                                                   shaderType,
                                const std::string_view                                             code,
                                const std::string_view                                             entryPointName,
                                const std::unordered_map<std::string, std::optional<std::string>>& defines) const
        {
            // vshadersystem currently assumes entry point "main".
            if (entryPointName != "main")
            {
                std::ostringstream oss;
                oss << "Unsupported shader entry point: " << entryPointName
                    << " (vshadersystem backend expects 'main')";
                return std::unexpected {oss.str()};
            }

            const auto stage = toStage(shaderType);
            if (stage == vshadersystem::ShaderStage::eUnknown)
            {
                return std::unexpected {"Unsupported shader stage for vshadersystem"};
            }

            vshadersystem::SourceInput input;
            input.virtualPath = "inline_shader";
            input.sourceText  = std::string(code);

            vshadersystem::CompileOptions opt;
            opt.stage = stage;

            opt.includeDirs.push_back(s_ShaderRootPath.generic_string());

            for (const auto& [name, value] : defines)
            {
                vshadersystem::Define d;
                d.name  = name;
                d.value = value.has_value() ? value.value() : "1";
                opt.defines.push_back(std::move(d));
            }

            auto r = vshadersystem::compile_glsl_to_spirv(input, opt);
            if (!r.isOk())
            {
                return std::unexpected {r.error().message};
            }

            return std::move(r.value().spirv);
        }
    } // namespace rhi
} // namespace vultra
