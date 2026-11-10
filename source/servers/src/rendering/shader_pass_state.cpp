#include "shader_pass_state.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace vultra::detail
{
    namespace
    {
        std::expected<double, std::string>
        numeric(const ShaderAsset& asset, const MaterialInstance& instance, const ShaderStateValue& value)
        {
            if (!value.property)
            {
                return value.numberExpected();
            }
            auto property = instance.valueExpected(asset, value.value);
            if (!property)
            {
                return std::unexpected(property.error());
            }
            if (const auto* integer = std::get_if<int32_t>(&*property))
            {
                return *integer;
            }
            if (const auto* scalar = std::get_if<float>(&*property))
            {
                return *scalar;
            }
            if (const auto* boolean = std::get_if<bool>(&*property))
            {
                return *boolean ? 1 : 0;
            }
            return std::unexpected("State property is not numeric: " + value.value);
        }

        std::expected<uint32_t, std::string> enumValue(const ShaderAsset&                asset,
                                                       const MaterialInstance&           instance,
                                                       const ShaderStateValue&           value,
                                                       std::span<const std::string_view> names)
        {
            if (value.property)
            {
                auto number = numeric(asset, instance, value);
                if (!number)
                {
                    return std::unexpected(number.error());
                }
                if (!std::isfinite(*number) || *number < 0 || *number >= names.size() || std::trunc(*number) != *number)
                {
                    return std::unexpected("State enum is outside its domain: " + value.value);
                }
                return uint32_t(*number);
            }
            const auto found = std::ranges::find(names, value.value);
            if (found == names.end())
            {
                return std::unexpected("Unknown state enum: " + value.value);
            }
            return uint32_t(found - names.begin());
        }
    } // namespace

    std::expected<void, std::string> applyShaderStateExpected(const ShaderAsset&                   asset,
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
        constexpr std::array<std::string_view, 5>  operations {"Add", "Subtract", "ReverseSubtract", "Min", "Max"};
        for (const auto& [name, values] : state.commands)
        {
            // The immutable asset's state schema was validated when the material was created.
            const auto value = [&](size_t index, const auto& names)
            {
                return enumValue(asset, instance, values[index], names);
            };
            if (name == "Cull" || name == "FrontFace" || name == "ZWrite" || name == "ZTest" || name == "Stencil" ||
                name == "Stencil.Comp" || name == "Stencil.Pass" || name == "Stencil.Fail" || name == "Stencil.ZFail")
            {
                std::expected<uint32_t, std::string> resolved;
                if (name == "Cull")
                {
                    resolved = value(0, cull);
                }
                else if (name == "FrontFace")
                {
                    resolved = value(0, face);
                }
                else if (name == "ZWrite" || name == "Stencil")
                {
                    resolved = value(0, boolean);
                }
                else if (name == "ZTest" || name == "Stencil.Comp")
                {
                    resolved = value(0, compare);
                }
                else
                {
                    resolved = value(0, stencil);
                }
                if (!resolved)
                {
                    return std::unexpected(resolved.error());
                }
                if (name == "Cull")
                {
                    pipeline.rasterization.cullMode = VriCullMode(*resolved);
                }
                else if (name == "FrontFace")
                {
                    pipeline.rasterization.frontFace = VriFrontFace(*resolved);
                }
                else if (name == "ZWrite")
                {
                    pipeline.depthStencil.depthWrite = *resolved;
                }
                else if (name == "ZTest")
                {
                    pipeline.depthStencil.depthCompareOp = VriCompareOp(*resolved);
                }
                else if (name == "Stencil")
                {
                    pipeline.depthStencil.stencilTest = *resolved;
                }
                else if (name == "Stencil.Comp")
                {
                    pipeline.depthStencil.front.compareOp = VriCompareOp(*resolved);
                }
                else if (name == "Stencil.Pass")
                {
                    pipeline.depthStencil.front.passOp = VriStencilOp(*resolved);
                }
                else if (name == "Stencil.Fail")
                {
                    pipeline.depthStencil.front.failOp = VriStencilOp(*resolved);
                }
                else
                {
                    pipeline.depthStencil.front.depthFailOp = VriStencilOp(*resolved);
                }
            }
            else if (name == "DepthBias")
            {
                auto constant = numeric(asset, instance, values[0]);
                auto slope    = numeric(asset, instance, values[1]);
                auto clamp =
                    values.size() == 3 ? numeric(asset, instance, values[2]) : std::expected<double, std::string>(0);
                if (!constant)
                {
                    return std::unexpected(constant.error());
                }
                if (!slope)
                {
                    return std::unexpected(slope.error());
                }
                if (!clamp)
                {
                    return std::unexpected(clamp.error());
                }
                pipeline.rasterization.depthBias.constant = float(*constant);
                pipeline.rasterization.depthBias.slope    = float(*slope);
                pipeline.rasterization.depthBias.clamp    = float(*clamp);
            }
            else if (name == "BlendOp")
            {
                auto colorOp = value(0, operations);
                auto alphaOp = value(values.size() == 2 ? 1 : 0, operations);
                if (!colorOp)
                {
                    return std::unexpected(colorOp.error());
                }
                if (!alphaOp)
                {
                    return std::unexpected(alphaOp.error());
                }
                for (auto& color : colors)
                {
                    color.blend.colorOp = VriBlendOp(*colorOp);
                    color.blend.alphaOp = VriBlendOp(*alphaOp);
                }
            }
            else if (name == "Blend")
            {
                if (values.size() == 1)
                {
                    for (auto& color : colors)
                    {
                        color.blend.enable = false;
                    }
                    continue;
                }
                auto srcColor = value(0, blend);
                auto dstColor = value(1, blend);
                auto srcAlpha = value(values.size() == 4 ? 2 : 0, blend);
                auto dstAlpha = value(values.size() == 4 ? 3 : 1, blend);
                if (!srcColor)
                {
                    return std::unexpected(srcColor.error());
                }
                if (!dstColor)
                {
                    return std::unexpected(dstColor.error());
                }
                if (!srcAlpha)
                {
                    return std::unexpected(srcAlpha.error());
                }
                if (!dstAlpha)
                {
                    return std::unexpected(dstAlpha.error());
                }
                for (auto& color : colors)
                {
                    color.blend.enable   = true;
                    color.blend.srcColor = VriBlendFactor(*srcColor);
                    color.blend.dstColor = VriBlendFactor(*dstColor);
                    color.blend.srcAlpha = VriBlendFactor(*srcAlpha);
                    color.blend.dstAlpha = VriBlendFactor(*dstAlpha);
                }
            }
            else if (name == "ColorMask")
            {
                uint32_t mask = 0;
                if (values[0].property)
                {
                    auto number = numeric(asset, instance, values[0]);
                    if (!number)
                    {
                        return std::unexpected(number.error());
                    }
                    if (*number < 0 || *number > 15 || std::trunc(*number) != *number)
                    {
                        return std::unexpected("ColorMask is outside RGBA");
                    }
                    mask = uint32_t(*number);
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
                    auto number = numeric(asset, instance, values[1]);
                    if (!number)
                    {
                        return std::unexpected(number.error());
                    }
                    if (*number < 0 || *number >= colors.size() || std::trunc(*number) != *number)
                    {
                        return std::unexpected("ColorMask attachment is absent");
                    }
                    colors[uint32_t(*number)].colorWriteMask = mask;
                }
                else
                {
                    for (auto& color : colors)
                    {
                        color.colorWriteMask = mask;
                    }
                }
            }
            else
            {
                auto number = numeric(asset, instance, values[0]);
                if (!number)
                {
                    return std::unexpected(number.error());
                }
                if (*number < 0 || *number > 255 || std::trunc(*number) != *number)
                {
                    return std::unexpected("Stencil integer must be in [0, 255]");
                }
                if (name == "Stencil.Ref")
                {
                    pipeline.depthStencil.front.reference = uint32_t(*number);
                }
                else if (name == "Stencil.ReadMask")
                {
                    pipeline.depthStencil.front.compareMask = uint32_t(*number);
                }
                else if (name == "Stencil.WriteMask")
                {
                    pipeline.depthStencil.front.writeMask = uint32_t(*number);
                }
                else
                {
                    return std::unexpected("Unknown render state: " + name);
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
            pipeline.depthStencil.depthWrite = false;
        }
        if (context.depth == VriFormat_Unknown &&
            (state.commands.contains("ZWrite") || state.commands.contains("ZTest") ||
             pipeline.depthStencil.stencilTest))
        {
            return std::unexpected("Pass depth/stencil state requires a depth attachment");
        }
        if (pipeline.depthStencil.stencilTest && context.depth != VriFormat_D24_UNORM_S8_UINT &&
            context.depth != VriFormat_D32_SFLOAT_S8_UINT)
        {
            return std::unexpected("Stencil state requires a stencil attachment");
        }
        return {};
    }

    std::expected<std::string, std::string> shaderPipelineKeyExpected(const ShaderAsset&       asset,
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
                    auto resolved = numeric(asset, instance, value);
                    if (!resolved)
                    {
                        return std::unexpected(resolved.error());
                    }
                    command.push_back(*resolved);
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
        // Internal cache identity, not a serialized artifact. Retain exact semantic-name bytes without replacement.
        const auto bytes = nlohmann::json::to_cbor(key);
        return std::string(bytes.begin(), bytes.end());
    }
} // namespace vultra::detail
