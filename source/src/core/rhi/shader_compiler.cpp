#include "vultra/core/rhi/shader_compiler.hpp"

#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

// https://github.com/KhronosGroup/GLSL/blob/master/extensions/khr/GL_KHR_vulkan_glsl.txt

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            constexpr TBuiltInResource kDefaultResources {
                .maxLights                                 = 32,
                .maxClipPlanes                             = 6,
                .maxTextureUnits                           = 32,
                .maxTextureCoords                          = 32,
                .maxVertexAttribs                          = 64,
                .maxVertexUniformComponents                = 4096,
                .maxVaryingFloats                          = 64,
                .maxVertexTextureImageUnits                = 32,
                .maxCombinedTextureImageUnits              = 80,
                .maxTextureImageUnits                      = 32,
                .maxFragmentUniformComponents              = 4096,
                .maxDrawBuffers                            = 32,
                .maxVertexUniformVectors                   = 128,
                .maxVaryingVectors                         = 8,
                .maxFragmentUniformVectors                 = 16,
                .maxVertexOutputVectors                    = 16,
                .maxFragmentInputVectors                   = 15,
                .minProgramTexelOffset                     = -8,
                .maxProgramTexelOffset                     = 7,
                .maxClipDistances                          = 8,
                .maxComputeWorkGroupCountX                 = 65535,
                .maxComputeWorkGroupCountY                 = 65535,
                .maxComputeWorkGroupCountZ                 = 65535,
                .maxComputeWorkGroupSizeX                  = 1024,
                .maxComputeWorkGroupSizeY                  = 1024,
                .maxComputeWorkGroupSizeZ                  = 64,
                .maxComputeUniformComponents               = 1024,
                .maxComputeTextureImageUnits               = 16,
                .maxComputeImageUniforms                   = 8,
                .maxComputeAtomicCounters                  = 8,
                .maxComputeAtomicCounterBuffers            = 1,
                .maxVaryingComponents                      = 60,
                .maxVertexOutputComponents                 = 64,
                .maxGeometryInputComponents                = 64,
                .maxGeometryOutputComponents               = 128,
                .maxFragmentInputComponents                = 128,
                .maxImageUnits                             = 8,
                .maxCombinedImageUnitsAndFragmentOutputs   = 8,
                .maxCombinedShaderOutputResources          = 8,
                .maxImageSamples                           = 0,
                .maxVertexImageUniforms                    = 0,
                .maxTessControlImageUniforms               = 0,
                .maxTessEvaluationImageUniforms            = 0,
                .maxGeometryImageUniforms                  = 0,
                .maxFragmentImageUniforms                  = 8,
                .maxCombinedImageUniforms                  = 8,
                .maxGeometryTextureImageUnits              = 16,
                .maxGeometryOutputVertices                 = 256,
                .maxGeometryTotalOutputComponents          = 1024,
                .maxGeometryUniformComponents              = 1024,
                .maxGeometryVaryingComponents              = 64,
                .maxTessControlInputComponents             = 128,
                .maxTessControlOutputComponents            = 128,
                .maxTessControlTextureImageUnits           = 16,
                .maxTessControlUniformComponents           = 1024,
                .maxTessControlTotalOutputComponents       = 4096,
                .maxTessEvaluationInputComponents          = 128,
                .maxTessEvaluationOutputComponents         = 128,
                .maxTessEvaluationTextureImageUnits        = 16,
                .maxTessEvaluationUniformComponents        = 1024,
                .maxTessPatchComponents                    = 120,
                .maxPatchVertices                          = 32,
                .maxTessGenLevel                           = 64,
                .maxViewports                              = 16,
                .maxVertexAtomicCounters                   = 0,
                .maxTessControlAtomicCounters              = 0,
                .maxTessEvaluationAtomicCounters           = 0,
                .maxGeometryAtomicCounters                 = 0,
                .maxFragmentAtomicCounters                 = 8,
                .maxCombinedAtomicCounters                 = 8,
                .maxAtomicCounterBindings                  = 1,
                .maxVertexAtomicCounterBuffers             = 0,
                .maxTessControlAtomicCounterBuffers        = 0,
                .maxTessEvaluationAtomicCounterBuffers     = 0,
                .maxGeometryAtomicCounterBuffers           = 0,
                .maxFragmentAtomicCounterBuffers           = 1,
                .maxCombinedAtomicCounterBuffers           = 1,
                .maxAtomicCounterBufferSize                = 16384,
                .maxTransformFeedbackBuffers               = 4,
                .maxTransformFeedbackInterleavedComponents = 64,
                .maxCullDistances                          = 8,
                .maxCombinedClipAndCullDistances           = 8,
                .maxSamples                                = 4,
                .maxMeshOutputVerticesNV                   = 256,
                .maxMeshOutputPrimitivesNV                 = 512,
                .maxMeshWorkGroupSizeX_NV                  = 32,
                .maxMeshWorkGroupSizeY_NV                  = 1,
                .maxMeshWorkGroupSizeZ_NV                  = 1,
                .maxTaskWorkGroupSizeX_NV                  = 32,
                .maxTaskWorkGroupSizeY_NV                  = 1,
                .maxTaskWorkGroupSizeZ_NV                  = 1,
                .maxMeshViewCountNV                        = 4,
                .maxMeshOutputVerticesEXT                  = 256,
                .maxMeshOutputPrimitivesEXT                = 512,
                .maxMeshWorkGroupSizeX_EXT                 = 32,
                .maxMeshWorkGroupSizeY_EXT                 = 1,
                .maxMeshWorkGroupSizeZ_EXT                 = 1,
                .maxTaskWorkGroupSizeX_EXT                 = 32,
                .maxTaskWorkGroupSizeY_EXT                 = 1,
                .maxTaskWorkGroupSizeZ_EXT                 = 1,
                .maxMeshViewCountEXT                       = 4,
                .limits =
                    {
                        .nonInductiveForLoops                 = true,
                        .whileLoops                           = true,
                        .doWhileLoops                         = true,
                        .generalUniformIndexing               = true,
                        .generalAttributeMatrixVectorIndexing = true,
                        .generalVaryingIndexing               = true,
                        .generalSamplerIndexing               = true,
                        .generalVariableIndexing              = true,
                        .generalConstantMatrixVectorIndexing  = true,
                    },
            };

            [[nodiscard]] auto toLanguage(const ShaderType type)
            {
#define CASE(Value) \
    case ShaderType::e##Value: \
        return EShLang##Value

                switch (type)
                {
                    CASE(Vertex);
                    CASE(Geometry);
                    CASE(Fragment);

                    CASE(Compute);

                    CASE(RayGen);
                    CASE(Miss);
                    CASE(ClosestHit);
                    CASE(AnyHit);
                    CASE(Intersect);

                    CASE(Mesh);
                    CASE(Task);
                }
#undef CASE

                assert(false);
                return EShLangCount;
            }

            class MyIncluder : public glslang::TShader::Includer
            {
            public:
                explicit MyIncluder(const std::filesystem::path& shaderRootPath) : m_ShaderRootPath(shaderRootPath) {}

                IncludeResult* includeSystem(const char* /*headerName*/,
                                             const char* /*includerName*/,
                                             size_t /*inclusionDepth*/) override
                {
                    return nullptr;
                }

                IncludeResult*
                includeLocal(const char* headerName, const char* includerName, size_t /*inclusionDepth*/) override
                {
                    return readFile(headerName, includerName);
                }

                void releaseInclude(IncludeResult* result) override { delete result; }

            private:
                IncludeResult* readFile(const char* headerName, const char* includerName)
                {
                    const std::filesystem::path includerPath = includerName;
                    std::filesystem::path       headerPath   = m_ShaderRootPath;
                    if (includerPath.has_extension())
                    {
                        headerPath /= includerPath.parent_path();
                    }
                    headerPath /= headerName;

                    std::ifstream file(headerPath);
                    if (!file.is_open())
                    {
                        return nullptr;
                    }
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    std::string content = buffer.str();
                    m_HeaderDatas.push_back(content); // keep the string alive
                    return new IncludeResult(headerName, m_HeaderDatas.back().c_str(), content.size(), nullptr);
                }

                std::filesystem::path m_ShaderRootPath;

                std::vector<std::string> m_HeaderDatas;
            };
        } // namespace

        std::filesystem::path ShaderCompiler::s_ShaderRootPath = std::filesystem::current_path() / "shaders";

        ShaderCompiler::ShaderCompiler() { glslang::InitializeProcess(); }

        ShaderCompiler::~ShaderCompiler() { glslang::FinalizeProcess(); }

        ShaderCompiler::Result
        ShaderCompiler::compile(const ShaderType                                                   shaderType,
                                const std::string_view                                             code,
                                const std::string_view                                             entryPointName,
                                const std::unordered_map<std::string, std::optional<std::string>>& defines)
        {
            glslang::TShader shader {toLanguage(shaderType)};

            // NOTE: Implicit defines:
            // VULKAN (via setEnvInput)
            // GL_(VERTEX/FRAGMENT/...)_SHADER
            // https://github.com/KhronosGroup/glslang/blob/4386679bcdb5c90833b5e46ea76d58d4fc2493f1/glslang/MachineIndependent/Versions.cpp#L388
            // https://github.com/KhronosGroup/glslang/issues/949
            std::ostringstream preambleAll;
            preambleAll << "#extension GL_GOOGLE_include_directive : require" << std::endl;
            preambleAll << "#extension GL_GOOGLE_cpp_style_line_directive : require" << std::endl;

            preambleAll << "#define DEPTH_ZERO_TO_ONE 1" << std::endl;

            for (const auto& define : defines)
            {
                preambleAll << "#define " << define.first;
                if (define.second.has_value())
                {
                    preambleAll << " " << define.second.value();
                }
                preambleAll << std::endl;
            }
            std::string preambleStr = preambleAll.str();
            shader.setPreamble(preambleStr.c_str());
            const auto strings = std::array {code.data()};
            shader.setStrings(strings.data(), static_cast<int32_t>(strings.size()));
            shader.setOverrideVersion(460);
            shader.setEntryPoint(entryPointName.data());

            shader.setEnvInput(glslang::EShSourceGlsl, shader.getStage(), glslang::EShClientVulkan, 100);
            shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
            shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

            constexpr auto kMessages =
                EShMessages {EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules | EShMsgCascadingErrors
#if _DEBUG
                             | EShMsgKeepUncalled
#if 0
                  | EShMsgDebugInfo
#endif
#endif
                             | EShMsgEnhanced};

            MyIncluder includer {s_ShaderRootPath};

            if (const auto result = shader.parse(&kDefaultResources, 110, false, kMessages, includer); !result)
            {
                const auto* const infoLog = shader.getInfoLog();
                return std::unexpected {infoLog};
            }

            glslang::TProgram program;
            program.addShader(&shader);
            if (!program.link(kMessages))
            {
                const auto* const infoLog = program.getInfoLog();
                return std::unexpected {infoLog};
            }

            const auto* intermediate = program.getIntermediate(shader.getStage());
            assert(intermediate);

            SPIRV spv;
            glslang::GlslangToSpv(*intermediate, spv);

            return spv;
        }
    } // namespace rhi
} // namespace vultra