#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/vertex_attributes.hpp"

#include <unordered_map>

namespace vultra
{
    namespace gfx
    {
        enum class AttributeLocation : int32_t
        {
            ePosition = 0,
            eColor,
            eNormal,
            eTexCoord0,
            eTexCoord1,
            eTangent,
            eBitangent,
            eJoints,
            eWeights,
        };

        class VertexFormat final
        {
        public:
            VertexFormat()                        = delete;
            VertexFormat(const VertexFormat&)     = delete;
            VertexFormat(VertexFormat&&) noexcept = default;
            ~VertexFormat()                       = default;

            VertexFormat& operator=(const VertexFormat&)     = delete;
            VertexFormat& operator=(VertexFormat&&) noexcept = delete;

            using Hash         = std::size_t;
            using VertexStride = uint32_t;

            [[nodiscard]] Hash getHash() const;

            [[nodiscard]] const rhi::VertexAttributes& getAttributes() const;
            [[nodiscard]] bool                         contains(const AttributeLocation) const;
            [[nodiscard]] bool                         contains(std::initializer_list<AttributeLocation>) const;

            [[nodiscard]] VertexStride getStride() const;

            friend bool operator==(const VertexFormat&, const VertexFormat&);

            class Builder
            {
            public:
                Builder()                   = default;
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = delete;
                ~Builder()                  = default;

                Builder& operator=(const Builder&)     = delete;
                Builder& operator=(Builder&&) noexcept = delete;

                Builder& setAttribute(const AttributeLocation, const rhi::VertexAttribute&);

                [[nodiscard]] Ref<VertexFormat> build();

            private:
                rhi::VertexAttributes m_Attributes;

                using Cache = std::unordered_map<Hash, std::weak_ptr<VertexFormat>>;
                inline static Cache s_Cache;
            };

        private:
            VertexFormat(const Hash, rhi::VertexAttributes&&, const VertexStride);

        private:
            const Hash                  m_Hash {0u};
            const rhi::VertexAttributes m_Attributes;
            const VertexStride          m_Stride {0};
        };

        [[nodiscard]] std::string_view toString(const AttributeLocation);

        [[nodiscard]] std::vector<std::string> buildDefines(const VertexFormat&);
    } // namespace gfx
} // namespace vultra