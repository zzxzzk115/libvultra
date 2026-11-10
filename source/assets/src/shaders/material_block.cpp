#include "rhi/shader_archive.hpp"

#include <vultra/assets/material_block.hpp>

#include <algorithm>
#include <bit>
#include <set>

namespace vultra
{
    namespace
    {
        using Json                          = nlohmann::json;
        constexpr size_t kMaximumBytes      = 16 * 1024 * 1024;
        constexpr size_t kMaximumFile       = 64 * 1024 * 1024;
        constexpr size_t kMaximumSlots      = 4096;
        constexpr size_t kMaximumReferences = 65536;

        bool validName(std::string_view name)
        {
            return !name.empty() && name.size() <= 4096 && name.find(char(0)) == std::string_view::npos &&
                   name.front() != '.' && name.back() != '.' && name.find("..") == std::string_view::npos;
        }

        bool inScope(std::string_view name, std::string_view scope)
        {
            return name == scope || (name.starts_with(scope) && name.size() > scope.size() &&
                                     (name[scope.size()] == '.' || name[scope.size()] == '['));
        }

        bool validLayoutText(const ShaderParameter& layout)
        {
            if (layout.name.size() > 4096 || layout.typeName.size() > 4096 ||
                layout.name.find(char(0)) != std::string::npos || layout.typeName.find(char(0)) != std::string::npos)
            {
                return false;
            }
            return std::ranges::all_of(layout.fields, validLayoutText);
        }

        bool textureType(VriDescriptorType type)
        {
            return type == VriDescriptorType_Texture || type == VriDescriptorType_StorageTexture;
        }

        bool resourceType(uint32_t type)
        {
            return type == VriDescriptorType_Sampler || type == VriDescriptorType_Texture ||
                   type == VriDescriptorType_StorageTexture || type == VriDescriptorType_StructuredBuffer ||
                   type == VriDescriptorType_StorageBuffer || type == VriDescriptorType_AccelerationStructure;
        }

        const Json* member(const Json& value, const char* name)
        {
            if (!value.is_object())
            {
                return nullptr;
            }
            const auto found = value.find(name);
            return found == value.end() ? nullptr : &*found;
        }

        std::string encodeBytes(std::span<const std::byte> bytes)
        {
            constexpr char digits[] = "0123456789abcdef";
            std::string    result(bytes.size() * 2, '0');
            for (size_t index = 0; index < bytes.size(); ++index)
            {
                const auto value      = std::to_integer<uint8_t>(bytes[index]);
                result[2 * index]     = digits[value >> 4];
                result[2 * index + 1] = digits[value & 15];
            }
            return result;
        }

        std::expected<std::vector<std::byte>, std::string> decodeBytes(const Json& value)
        {
            if (!value.is_string())
            {
                return std::unexpected("Material uniform bytes require a hexadecimal string");
            }
            const auto& text = value.get_ref<const std::string&>();
            if (text.size() % 2 || text.size() > 2 * kMaximumBytes)
            {
                return std::unexpected("Invalid material uniform byte count");
            }
            std::vector<std::byte> bytes(text.size() / 2);
            for (size_t index = 0; index < text.size(); ++index)
            {
                const char character = text[index];
                const auto digit     = std::string_view("0123456789abcdef").find(character);
                if (digit == std::string_view::npos)
                {
                    return std::unexpected("Invalid material uniform hexadecimal byte");
                }
                bytes[index / 2] |= std::byte(digit << (index % 2 == 0 ? 4 : 0));
            }
            return bytes;
        }

        const char* byteOrder()
        {
            return std::endian::native == std::endian::little ? "little" : "big";
        }
    } // namespace

    MaterialBlock::MaterialBlock(std::string scope) :
        m_Scope(std::move(scope))
    {
    }

    std::expected<MaterialBlock, std::string> MaterialBlock::create(const ShaderProgram& program,
                                                                    std::string_view     scope)
    {
        if (!validName(scope))
        {
            return std::unexpected("Material block requires a named reflected scope");
        }
        auto parameter = program.parameters.bindingExpected(scope);
        if (!parameter || ((*parameter)->kind != ShaderParameterKind::eConstantBuffer &&
                           (*parameter)->kind != ShaderParameterKind::eParameterBlock))
        {
            return std::unexpected("Material scope must select a ConstantBuffer or ParameterBlock: " +
                                   std::string(scope));
        }
        auto bindings = program.resourceBindingsExpected();
        if (!bindings)
        {
            return std::unexpected(bindings.error());
        }
        MaterialBlock              block {std::string(scope)};
        size_t                     bytes      = 0;
        size_t                     references = 0;
        std::set<std::string_view> names;
        for (const auto& binding : *bindings)
        {
            if (!inScope(binding.name, scope))
            {
                continue;
            }
            if (!validName(binding.name) || binding.pushConstant || !names.insert(binding.name).second ||
                names.size() > kMaximumSlots)
            {
                return std::unexpected("Invalid, duplicate or push-only material binding: " + binding.name);
            }
            const auto slots = block.m_Uniforms.size() + block.m_Resources.size();
            const auto count = binding.type == VriDescriptorType_ConstantBuffer ? binding.count : 1;
            if (count > kMaximumSlots - slots)
            {
                return std::unexpected("Material bindings exceed supported ownership: " + binding.name);
            }
            if (binding.type == VriDescriptorType_ConstantBuffer)
            {
                if (!binding.count || binding.count > kMaximumSlots - block.m_Uniforms.size() ||
                    binding.uniformSize > (kMaximumBytes - bytes) / binding.count)
                {
                    return std::unexpected("Material uniform count or aggregate size exceed supported ownership: " +
                                           binding.name);
                }
                auto layout = program.parameters.bindingExpected(binding.name);
                if (!layout)
                {
                    return std::unexpected(layout.error());
                }
                auto uniform = ShaderUniformBlock::create(**layout);
                if (!uniform)
                {
                    return std::unexpected(uniform.error());
                }
                if (!validLayoutText(uniform->layout()))
                {
                    return std::unexpected("Invalid material uniform layout text: " + binding.name);
                }
                bytes += uniform->data().size() * binding.count;
                for (uint32_t index = 0; index < binding.count; ++index)
                {
                    const auto name = binding.array ? binding.name + "[" + std::to_string(index) + "]" : binding.name;
                    if (!validName(name))
                    {
                        return std::unexpected("Invalid material uniform element name: " + name);
                    }
                    if (block.m_Resources.contains(name) || !block.m_Uniforms.emplace(name, *uniform).second)
                    {
                        return std::unexpected("Duplicate material uniform element: " + name);
                    }
                }
            }
            else
            {
                if (!resourceType(binding.type) || !binding.count || binding.count > kMaximumReferences - references)
                {
                    return std::unexpected("Unsupported material resource kind/count: " + binding.name);
                }
                references += binding.count;
                block.m_Resources.emplace(
                    binding.name,
                    MaterialResourceValue {binding.type, binding.viewType, std::vector<AssetId>(binding.count)});
            }
        }
        if (names.empty())
        {
            return std::unexpected("Material scope contains no supported bindings");
        }
        return block;
    }

    std::expected<bool, std::string> MaterialBlock::setExpected(std::span<const MaterialUniformWrite>  uniforms,
                                                                std::span<const MaterialResourceWrite> resources)
    {
        std::map<std::string, ShaderUniformBlock, std::less<>>   pendingUniforms;
        std::map<std::string, std::vector<AssetId>, std::less<>> pendingResources;
        for (const auto& write : uniforms)
        {
            const auto found = m_Uniforms.find(write.binding);
            if (found == m_Uniforms.end() ||
                std::ranges::count(uniforms, write.binding, &MaterialUniformWrite::binding) != 1)
            {
                return std::unexpected("Unknown or duplicate material uniform binding: " + std::string(write.binding));
            }
            auto candidate = found->second;
            auto changed   = candidate.setExpected(write.fields);
            if (!changed)
            {
                return std::unexpected(changed.error());
            }
            if (*changed)
            {
                pendingUniforms.emplace(found->first, std::move(candidate));
            }
        }
        for (const auto& write : resources)
        {
            const auto found = m_Resources.find(write.binding);
            if (found == m_Resources.end() || write.assets.size() != found->second.assets.size() ||
                std::ranges::count(resources, write.binding, &MaterialResourceWrite::binding) != 1)
            {
                return std::unexpected("Unknown, duplicate or incorrectly sized material resource: " +
                                       std::string(write.binding));
            }
            if (!std::ranges::equal(write.assets, found->second.assets))
            {
                pendingResources.emplace(found->first, std::vector<AssetId>(write.assets.begin(), write.assets.end()));
            }
        }
        if (pendingUniforms.empty() && pendingResources.empty())
        {
            return false;
        }
        if (m_Revision == UINT64_MAX)
        {
            return std::unexpected("Material block revision exhausted");
        }
        for (auto& [name, uniform] : pendingUniforms)
        {
            m_Uniforms.find(name)->second = std::move(uniform);
        }
        for (auto& [name, assets] : pendingResources)
        {
            m_Resources.find(name)->second.assets.swap(assets);
        }
        ++m_Revision;
        return true;
    }

    std::expected<MaterialBlock, std::string> MaterialBlock::reconcileExpected(const ShaderProgram& program,
                                                                               std::string&         diagnostics) const
    {
        auto candidate = create(program, m_Scope);
        if (!candidate)
        {
            return std::unexpected(candidate.error());
        }
        bool        changed = false;
        std::string messages;
        for (auto& [name, uniform] : candidate->m_Uniforms)
        {
            const auto previous = m_Uniforms.find(name);
            if (previous == m_Uniforms.end())
            {
                messages += "Added material uniform: " + name + "\n";
                changed = true;
                continue;
            }
            auto migrated = previous->second.reconcileExpected(uniform.layout(), messages);
            if (!migrated)
            {
                return std::unexpected(migrated.error());
            }
            changed |= uniform.layout() != previous->second.layout() ||
                       !std::ranges::equal(migrated->data(), previous->second.data());
            uniform = std::move(*migrated);
        }
        for (auto& [name, resource] : candidate->m_Resources)
        {
            const auto previous = m_Resources.find(name);
            if (previous == m_Resources.end() || previous->second.type != resource.type ||
                previous->second.dimension != resource.dimension)
            {
                messages += "Unassigned new or incompatible material resource: " + name + "\n";
                changed = true;
                continue;
            }
            std::copy_n(previous->second.assets.begin(),
                        std::min(resource.assets.size(), previous->second.assets.size()),
                        resource.assets.begin());
            if (resource.assets.size() != previous->second.assets.size())
            {
                messages += "Resized material resource array: " + name + "\n";
                changed = true;
            }
        }
        for (const auto& [name, uniform] : m_Uniforms)
        {
            if (!candidate->m_Uniforms.contains(name))
            {
                messages += "Removed material uniform: " + name + "\n";
                changed = true;
            }
        }
        for (const auto& [name, resource] : m_Resources)
        {
            if (!candidate->m_Resources.contains(name))
            {
                messages += "Removed material resource: " + name + "\n";
                changed = true;
            }
        }
        if (changed && m_Revision == UINT64_MAX)
        {
            return std::unexpected("Material block revision exhausted");
        }
        candidate->m_Revision = m_Revision + uint64_t(changed);
        diagnostics += messages;
        return candidate;
    }

    std::expected<std::vector<ShaderResourceViews>, std::string>
    MaterialBlock::resolveResourcesExpected(const Resolver& resolver) const
    {
        if (!m_Resources.empty() && !resolver)
        {
            return std::unexpected("Material resources require an explicit resolver");
        }
        for (const auto& [name, resource] : m_Resources)
        {
            if (std::ranges::any_of(resource.assets,
                                    [](AssetId id)
                                    {
                                        return !id.value.valid();
                                    }))
            {
                return std::unexpected("Unassigned material resource: " + name);
            }
        }
        std::vector<ShaderResourceViews> result;
        for (const auto& [name, resource] : m_Resources)
        {
            ShaderResourceViews binding {name, resource.type, {}};
            for (const auto id : resource.assets)
            {
                auto resolved = resolver(name, id);
                if (!resolved)
                {
                    return std::unexpected(resolved.error());
                }
                if (!resolved->view || resolved->type != resource.type ||
                    (textureType(resource.type) && resolved->dimension != resource.dimension))
                {
                    return std::unexpected("Material resource type/dimension mismatch: " + name);
                }
                binding.views.push_back(resolved->view);
            }
            result.push_back(std::move(binding));
        }
        return result;
    }

    std::expected<std::string, std::string> MaterialBlock::serializeExpected() const
    {
        Json document {{"version", 1},
                       {"kind", "material_block"},
                       {"scope", m_Scope},
                       {"byte_order", byteOrder()},
                       {"uniforms", Json::array()},
                       {"resources", Json::array()}};
        for (const auto& [name, uniform] : m_Uniforms)
        {
            document["uniforms"].push_back({{"binding", name},
                                            {"layout", detail::encodeShaderParameter(uniform.layout())},
                                            {"bytes", encodeBytes(uniform.data())}});
        }
        for (const auto& [name, resource] : m_Resources)
        {
            Json assets = Json::array();
            for (const auto id : resource.assets)
            {
                assets.push_back(id.value.valid() ? Json(id.value.toString()) : Json());
            }
            document["resources"].push_back({{"binding", name},
                                             {"type", resource.type},
                                             {"dimension", resource.dimension},
                                             {"assets", std::move(assets)}});
        }
        auto text = document.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
        if (text.size() > kMaximumFile || Json::parse(text, nullptr, false) != document)
        {
            return std::unexpected("Material block persistence requires valid UTF-8 and at most 64 MiB");
        }
        return text;
    }

    std::expected<MaterialBlock, std::string> MaterialBlock::parseExpected(std::string_view text)
    {
        if (text.size() > kMaximumFile)
        {
            return std::unexpected("Material block exceeds 64 MiB");
        }
        const auto  document  = Json::parse(text, nullptr, false);
        const auto* scope     = member(document, "scope");
        const auto* version   = member(document, "version");
        const auto* kind      = member(document, "kind");
        const auto* order     = member(document, "byte_order");
        const auto* uniforms  = member(document, "uniforms");
        const auto* resources = member(document, "resources");
        if (!scope || !scope->is_string() || !validName(scope->get_ref<const std::string&>()) || !version ||
            !version->is_number_integer() || *version != 1 || !kind || *kind != "material_block" || !order ||
            *order != byteOrder() || !uniforms || !uniforms->is_array() || !resources || !resources->is_array() ||
            uniforms->size() + resources->size() > kMaximumSlots || (uniforms->empty() && resources->empty()))
        {
            return std::unexpected("Invalid material block format, byte order or fields");
        }
        MaterialBlock         block(scope->get_ref<const std::string&>());
        std::set<std::string> names;
        size_t                totalBytes      = 0;
        size_t                totalReferences = 0;
        for (const auto& record : *uniforms)
        {
            const auto* name   = member(record, "binding");
            const auto* schema = member(record, "layout");
            const auto* data   = member(record, "bytes");
            if (!name || !name->is_string() || !validName(name->get_ref<const std::string&>()) ||
                !inScope(name->get_ref<const std::string&>(), block.m_Scope) ||
                !names.insert(name->get_ref<const std::string&>()).second || !schema || !data)
            {
                return std::unexpected("Invalid or duplicate material uniform record");
            }
            auto layout = detail::decodeShaderParameterExpected(*schema);
            auto bytes  = decodeBytes(*data);
            if (!layout || !bytes)
            {
                return std::unexpected(layout ? bytes.error() : layout.error());
            }
            if (!validLayoutText(*layout))
            {
                return std::unexpected("Invalid material uniform layout text");
            }
            if (bytes->size() > kMaximumBytes - totalBytes)
            {
                return std::unexpected("Material uniform data exceeds 16 MiB");
            }
            totalBytes += bytes->size();
            auto uniform = ShaderUniformBlock::create(*layout, *bytes);
            if (!uniform)
            {
                return std::unexpected(uniform.error());
            }
            block.m_Uniforms.emplace(name->get_ref<const std::string&>(), std::move(*uniform));
        }
        for (const auto& record : *resources)
        {
            const auto* name      = member(record, "binding");
            const auto* type      = member(record, "type");
            const auto* dimension = member(record, "dimension");
            const auto* assets    = member(record, "assets");
            if (!name || !name->is_string() || !validName(name->get_ref<const std::string&>()) ||
                !inScope(name->get_ref<const std::string&>(), block.m_Scope) ||
                !names.insert(name->get_ref<const std::string&>()).second || !type || !type->is_number_unsigned() ||
                type->get<uint64_t>() > UINT32_MAX || !resourceType(type->get<uint32_t>()) || !dimension ||
                !dimension->is_number_unsigned() || dimension->get<uint64_t>() > VriTextureViewType_CubeArray ||
                !assets || !assets->is_array() || assets->empty() ||
                assets->size() > kMaximumReferences - totalReferences)
            {
                return std::unexpected("Invalid or duplicate material resource record");
            }
            MaterialResourceValue resource {VriDescriptorType(type->get<uint32_t>()),
                                            VriTextureViewType(dimension->get<uint32_t>()),
                                            {}};
            for (const auto& value : *assets)
            {
                AssetId id;
                if (!value.is_null())
                {
                    const auto parsed =
                        value.is_string() ? StableId::parse(value.get_ref<const std::string&>()) : std::nullopt;
                    if (!parsed || !parsed->valid())
                    {
                        return std::unexpected("Invalid material resource AssetId");
                    }
                    id.value = *parsed;
                }
                resource.assets.push_back(id);
            }
            totalReferences += resource.assets.size();
            block.m_Resources.emplace(name->get_ref<const std::string&>(), std::move(resource));
        }
        return block;
    }

    const std::string& MaterialBlock::scope() const
    {
        return m_Scope;
    }

    const std::map<std::string, ShaderUniformBlock, std::less<>>& MaterialBlock::uniforms() const
    {
        return m_Uniforms;
    }

    const std::map<std::string, MaterialResourceValue, std::less<>>& MaterialBlock::resources() const
    {
        return m_Resources;
    }

    uint64_t MaterialBlock::revision() const
    {
        return m_Revision;
    }
} // namespace vultra
