#pragma once

#include "vultra/function/framegraph/framegraph_buffer.hpp"
#include "vultra/function/framegraph/framegraph_texture.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
    }

    namespace framegraph
    {
        class TransientResources
        {
        public:
            explicit TransientResources(rhi::RenderDevice&);
            TransientResources(const TransientResources&)     = delete;
            TransientResources(TransientResources&&) noexcept = delete;
            ~TransientResources()                             = default;

            TransientResources& operator=(const TransientResources&)     = delete;
            TransientResources& operator=(TransientResources&&) noexcept = delete;

            // In bytes
            struct MemoryStats
            {
                vk::DeviceSize textures;
                vk::DeviceSize buffers;
            };
            [[nodiscard]] MemoryStats getStats() const;

            void update();

            [[nodiscard]] rhi::Texture* acquireTexture(const FrameGraphTexture::Desc&);
            void                        releaseTexture(const FrameGraphTexture::Desc&, rhi::Texture*);

            [[nodiscard]] rhi::Buffer* acquireBuffer(const FrameGraphBuffer::Desc&);
            void                       releaseBuffer(const FrameGraphBuffer::Desc&, rhi::Buffer*);

        private:
            rhi::RenderDevice& m_RenderDevice;

            template<class T>
            struct Pool
            {
                using HashType = std::size_t;

                struct Entry
                {
                    Entry(T* resource, std::size_t life) : resource(resource), life(life) {}

                    T*          resource;
                    std::size_t life {0}; // In frames.
                };
                using Entries     = std::vector<Entry>;
                using EntryGroups = std::unordered_map<HashType, Entries>;

                std::vector<std::unique_ptr<T>> resources;
                EntryGroups                     entryGroups;
            };
            Pool<rhi::Texture> m_Textures;
            Pool<rhi::Buffer>  m_Buffers;
        };
    } // namespace framegraph
} // namespace vultra