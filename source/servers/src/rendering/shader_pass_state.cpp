#include "shader_pass_state.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vultra::detail
{
    namespace
    {
        double numeric(const ShaderAsset& asset, const MaterialInstance& instance, const ShaderStateValue& value)
        {
            if (!value.property)
            {
                return std::stod(value.value);
            }
            const auto property = instance.value(asset, value.value);
            if (const auto* integer = std::get_if<int32_t>(&property))
            {
                return *integer;
            }
            if (const auto* scalar = std::get_if<float>(&property))
            {
                return *scalar;
            }
            if (const auto* boolean = std::get_if<bool>(&property))
            {
                return *boolean ? 1 : 0;
            }
            throw std::invalid_argument("State property is not numeric: " + value.value);
        }

        uint32_t enumValue(const ShaderAsset&                asset,
                           const MaterialInstance&           instance,
                           const ShaderStateValue&           value,
                           std::span<const std::string_view> names)
        {
            if (value.property)
            {
                const auto number = numeric(asset, instance, value);
                if (number < 0 || number >= names.size() || std::trunc(number) != number)
                {
                    throw std::invalid_argument("State enum is outside its domain: " + value.value);
                }
                return uint32_t(number);
            }
            const auto found = std::ranges::find(names, value.value);
            if (found == names.end())
            {
                throw std::invalid_argument("Unknown state enum: " + value.value);
            }
            return uint32_t(found - names.begin());
        }
    } // namespace

    void applyShaderState(const ShaderAsset&                   asset,
                          const MaterialInstance&              instance,
                          const ShaderState&                   state,
                          const ShaderPassContext&             context,
                          VriGraphicsPipelineDesc&             pipeline,
                          std::vector<VriColorAttachmentDesc>& colors)
    {
        constexpr std::array<std::string_view, 8>
            compare {"Never", "Less", "Equal", "LEqual", "Greater", "NotEqual", "GEqual", "Always"};
        constexpr std::array<std::string_view, 8>
            stencil {"Keep", "Zero", "Replace", "IncrSat", "DecrSat", "Invert", "IncrWrap", "DecrWrap"};
        constexpr std::array<std::string_view, 15> blend {"Zero",
                                                          "One",
                                                          "SrcColor",
                                                          "OneMinusSrcColor",
                                                          "DstColor",
                                                          "OneMinusDstColor",
                                                          "SrcAlpha",
                                                          "OneMinusSrcAlpha",
                                                          "DstAlpha",
                                                          "OneMinusDstAlpha",
                                                          "ConstantColor",
                                                          "OneMinusConstantColor",
                                                          "ConstantAlpha",
                                                          "OneMinusConstantAlpha",
                                                          "SrcAlphaSaturate"};
        constexpr std::array<std::string_view, 3>  cull {"Off", "Front", "Back"};
        constexpr std::array<std::string_view, 2>  face {"CCW", "CW"};
        constexpr std::array<std::string_view, 2>  boolean {"Off", "On"};
        for (const auto& [name, values] : state.commands)
        {
            auto value = [&](size_t index, auto& names)
            {
                return enumValue(asset, instance, values.at(index), names);
            };
            if (name == "Cull")
            {
                pipeline.rasterization.cullMode = VriCullMode(value(0, cull));
            }
            else if (name == "FrontFace")
            {
                pipeline.rasterization.frontFace = VriFrontFace(value(0, face));
            }
            else if (name == "ZWrite")
            {
                pipeline.depthStencil.depthWrite = value(0, boolean);
            }
            else if (name == "ZTest")
            {
                pipeline.depthStencil.depthCompareOp = VriCompareOp(value(0, compare));
            }
            else if (name == "DepthBias")
            {
                pipeline.rasterization.depthBias.constant = float(numeric(asset, instance, values[0]));
                pipeline.rasterization.depthBias.slope    = float(numeric(asset, instance, values[1]));
                pipeline.rasterization.depthBias.clamp =
                    values.size() == 3 ? float(numeric(asset, instance, values[2])) : 0;
            }
            else if (name == "BlendOp")
            {
                constexpr std::array<std::string_view, 5> operations {"Add",
                                                                      "Subtract",
                                                                      "ReverseSubtract",
                                                                      "Min",
                                                                      "Max"};
                for (auto& color : colors)
                {
                    color.blend.colorOp = VriBlendOp(value(0, operations));
                    color.blend.alphaOp = VriBlendOp(value(values.size() == 2 ? 1 : 0, operations));
                }
            }
            else if (name == "Blend")
            {
                for (auto& color : colors)
                {
                    color.blend.enable = values.size() != 1;
                    if (color.blend.enable)
                    {
                        color.blend.srcColor = VriBlendFactor(value(0, blend));
                        color.blend.dstColor = VriBlendFactor(value(1, blend));
                        color.blend.srcAlpha = VriBlendFactor(value(values.size() == 4 ? 2 : 0, blend));
                        color.blend.dstAlpha = VriBlendFactor(value(values.size() == 4 ? 3 : 1, blend));
                    }
                }
            }
            else if (name == "ColorMask")
            {
                uint32_t mask = 0;
                if (values[0].property)
                {
                    const auto number = numeric(asset, instance, values[0]);
                    if (number < 0 || number > 15)
                    {
                        throw std::invalid_argument("ColorMask is outside RGBA");
                    }
                    mask = uint32_t(number);
                }
                else if (values[0].value != "0")
                {
                    for (const auto channel : values[0].value)
                    {
                        mask |= 1u << std::string_view("RGBA").find(channel);
                    }
                }
                if (values.size() == 2)
                {
                    const auto index = uint32_t(numeric(asset, instance, values[1]));
                    if (index >= colors.size())
                    {
                        throw std::invalid_argument("ColorMask attachment is absent");
                    }
                    colors[index].colorWriteMask = mask;
                }
                else
                {
                    for (auto& color : colors)
                    {
                        color.colorWriteMask = mask;
                    }
                }
            }
            else if (name == "Stencil")
            {
                pipeline.depthStencil.stencilTest = value(0, boolean);
            }
            else if (name.starts_with("Stencil."))
            {
                auto& front = pipeline.depthStencil.front;
                if (name == "Stencil.Comp")
                {
                    front.compareOp = VriCompareOp(value(0, compare));
                }
                else if (name == "Stencil.Pass")
                {
                    front.passOp = VriStencilOp(value(0, stencil));
                }
                else if (name == "Stencil.Fail")
                {
                    front.failOp = VriStencilOp(value(0, stencil));
                }
                else if (name == "Stencil.ZFail")
                {
                    front.depthFailOp = VriStencilOp(value(0, stencil));
                }
                else
                {
                    const auto number = numeric(asset, instance, values[0]);
                    if (number < 0 || number > 255 || std::trunc(number) != number)
                    {
                        throw std::invalid_argument("Stencil integer must be in [0, 255]");
                    }
                    if (name == "Stencil.Ref")
                    {
                        front.reference = uint32_t(number);
                    }
                    else if (name == "Stencil.ReadMask")
                    {
                        front.compareMask = uint32_t(number);
                    }
                    else if (name == "Stencil.WriteMask")
                    {
                        front.writeMask = uint32_t(number);
                    }
                }
            }
        }
        pipeline.depthStencil.back = pipeline.depthStencil.front;
        if (context.mirrored)
        {
            pipeline.rasterization.frontFace = pipeline.rasterization.frontFace == VriFrontFace_Clockwise ?
                                                   VriFrontFace_CounterClockwise :
                                                   VriFrontFace_Clockwise;
        }
        if (context.depthReadOnly)
        {
            pipeline.depthStencil.depthWrite = VRI_FALSE;
        }
        if (context.depth == VriFormat_Unknown &&
            (state.commands.contains("ZWrite") || state.commands.contains("ZTest") ||
             pipeline.depthStencil.stencilTest))
        {
            throw std::invalid_argument("Pass depth/stencil state requires a depth attachment");
        }
        if (pipeline.depthStencil.stencilTest && context.depth != VriFormat_D24_UNORM_S8_UINT &&
            context.depth != VriFormat_D32_SFLOAT_S8_UINT)
        {
            throw std::invalid_argument("Stencil state requires a stencil attachment");
        }
    }

    std::string shaderPipelineKey(const ShaderAsset&       asset,
                                  const MaterialInstance&  instance,
                                  const ShaderPass&        pass,
                                  const ShaderPassContext& context)
    {
        nlohmann::json key {{"variant", instance.variant},
                            {"pass", pass.name},
                            {"colors", context.colors},
                            {"depth", context.depth},
                            {"samples", context.samples},
                            {"view_mask", context.viewMask},
                            {"topology", context.topology},
                            {"mirrored", context.mirrored},
                            {"read_only_depth", context.depthReadOnly},
                            {"state", nlohmann::json::object()},
                            {"attributes", nlohmann::json::array()},
                            {"streams", nlohmann::json::array()}};
        for (const auto& [name, values] : pass.state.commands)
        {
            auto& command = key["state"][name] = nlohmann::json::array();
            for (const auto& value : values)
            {
                if (value.property)
                {
                    command.push_back(numeric(asset, instance, value));
                }
                else
                {
                    command.push_back(value.value);
                }
            }
        }
        for (const auto& attribute : context.attributes)
        {
            key["attributes"].push_back({attribute.format,
                                         attribute.offset,
                                         attribute.streamIndex,
                                         attribute.semanticName ? attribute.semanticName : "",
                                         attribute.semanticIndex});
        }
        for (const auto& stream : context.streams)
        {
            key["streams"].push_back({stream.stride, stream.bindingSlot, stream.stepRate});
        }
        return key.dump();
    }
} // namespace vultra::detail
