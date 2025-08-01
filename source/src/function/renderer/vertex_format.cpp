#include "vultra/function/renderer/vertex_format.hpp"
#include "vultra/core/base/hash.hpp"

#include <magic_enum/magic_enum.hpp>

namespace std
{
    template<>
    struct hash<vultra::rhi::VertexAttribute>
    {
        auto operator()(const vultra::rhi::VertexAttribute& attribute) const noexcept
        {
            size_t h {0};
            hashCombine(h, attribute.type, attribute.offset);
            return h;
        }
    };
} // namespace std

namespace vultra
{
    namespace gfx
    {

        VertexFormat::Hash VertexFormat::getHash() const { return m_Hash; }

        const rhi::VertexAttributes& VertexFormat::getAttributes() const { return m_Attributes; }
        bool                         VertexFormat::contains(const AttributeLocation location) const
        {
            return m_Attributes.contains(static_cast<rhi::LocationIndex>(location));
        }
        bool VertexFormat::contains(std::initializer_list<AttributeLocation> locations) const
        {
            return std::ranges::all_of(locations, [this](const auto required) {
                return std::ranges::any_of(m_Attributes, [required](const auto& p) {
                    return p.first == static_cast<rhi::LocationIndex>(required);
                });
            });
        }

        VertexFormat::VertexStride VertexFormat::getStride() const { return m_Stride; }

        VertexFormat::VertexFormat(const Hash hash, rhi::VertexAttributes&& attributes, const VertexStride stride) :
            m_Hash {hash}, m_Attributes {std::move(attributes)}, m_Stride {stride}
        {}

        //
        // Builder:
        //

        using Builder = VertexFormat::Builder;

        Builder& Builder::setAttribute(const AttributeLocation location, const rhi::VertexAttribute& attribute)
        {
            m_Attributes.insert_or_assign(static_cast<rhi::LocationIndex>(location), attribute);
            return *this;
        }

        std::shared_ptr<VertexFormat> Builder::build()
        {
            Hash hash {0};

            VertexStride stride {0};
            for (const auto& [location, attribute] : m_Attributes)
            {
                hashCombine(hash, location, attribute);
                stride += getSize(attribute.type);
            }

            if (const auto it = s_Cache.find(hash); it != s_Cache.cend())
            {
                if (auto vertexFormat = it->second.lock(); vertexFormat)
                    return vertexFormat;
            }

            auto vertexFormat = createRef<VertexFormat>(VertexFormat {hash, std::move(m_Attributes), stride});
            s_Cache.insert_or_assign(hash, vertexFormat);

            return vertexFormat;
        }

        //
        // Utility:
        //

        bool operator==(const VertexFormat& lhs, const VertexFormat& rhs) { return lhs.getHash() == rhs.getHash(); }

        std::string_view toString(AttributeLocation location) { return magic_enum::enum_name(location); }

        std::vector<std::string> buildDefines(const VertexFormat& vertexFormat)
        {
            constexpr auto           kMaxNumVertexDefines = 6;
            std::vector<std::string> defines;
            defines.reserve(kMaxNumVertexDefines);

            using enum AttributeLocation;

            if (vertexFormat.contains(eColor))
                defines.emplace_back("HAS_COLOR");
            if (vertexFormat.contains(eNormal))
                defines.emplace_back("HAS_NORMAL");
            if (vertexFormat.contains(eTexCoord0))
            {
                defines.emplace_back("HAS_TEXCOORD0");
                if (vertexFormat.contains({eTangent, eBitangent}))
                {
                    defines.emplace_back("HAS_TANGENTS");
                }
            }
            if (vertexFormat.contains(eTexCoord1))
                defines.emplace_back("HAS_TEXCOORD1");
            if (vertexFormat.contains({eJoints, eWeights}))
                defines.emplace_back("IS_SKINNED");

            return defines;
        }
    } // namespace gfx
} // namespace vultra