#pragma once

#include "vultra/core/base/base.hpp" // Ref, createRef

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;
        class Texture;
    } // namespace rhi

    class IGpuResourceService;

    namespace rendering
    {
        // CPU FreeType rasterizer + a single fixed-size R8 coverage atlas, registered once as a
        // bindless texture so the UI overlay pass can sample glyph quads. This is the pragmatic
        // "temporary" in-game text path (not an SDF/MSDF engine); color emoji are out of scope.
        //
        // Lifetime: owned by RenderSystem, persistent across frames. Faces and rasterized glyphs
        // are cached, so steady-state frames do no FreeType work and no GPU uploads.
        class GlyphAtlas
        {
        public:
            struct Glyph
            {
                glm::vec2 uvMin {0.0f};     // atlas sub-rect (normalized)
                glm::vec2 uvMax {0.0f};
                glm::vec2 sizePx {0.0f};    // rasterized bitmap size (px)
                glm::vec2 bearingPx {0.0f}; // FreeType bearing: left, top (px)
                float     advancePx {0.0f}; // pen advance (px)
                bool      hasBitmap {false};
            };

            // Lazy: FreeType, the CPU atlas, and the GPU texture are all created on first use, so
            // text-less scenes allocate nothing. The ctor/dtor are defined out-of-line (in the .cpp,
            // where Face is complete) because m_Faces holds unique_ptr<Face> with Face incomplete here.
            GlyphAtlas();
            ~GlyphAtlas();
            GlyphAtlas(const GlyphAtlas&)            = delete;
            GlyphAtlas& operator=(const GlyphAtlas&) = delete;

            // Create the GPU atlas texture and register it bindless (idempotent). Returns the
            // bindless index (0 == unavailable; 0 is the pool's reserved fallback slot).
            uint32_t ensureRegistered(rhi::RenderDevice& rd, IGpuResourceService& gpu);

            // Ensure a FreeType face exists for fontKey. bytesProvider is invoked at most once
            // (only when the face is first created); its returned bytes are copied and owned.
            // Returns true if the face is usable.
            bool ensureFont(uint64_t fontKey, const std::function<std::vector<std::byte>()>& bytesProvider);

            // Vertical layout metrics at pixelSize. Returns false if the face is unavailable.
            bool fontMetrics(uint64_t fontKey, uint32_t pixelSize, float& outAscentPx, float& outLineHeightPx);

            // Glyph metrics + atlas UVs, rasterizing and caching on demand. Returns nullptr if the
            // face is unavailable or the atlas is full. Requires ensureFont() to have succeeded.
            const Glyph* getGlyph(uint64_t fontKey, uint32_t pixelSize, uint32_t codepoint);

            // Upload newly-rasterized glyphs to the GPU. Call once per frame after emitting glyphs.
            void flush(rhi::RenderDevice& rd);

            // Release the GPU atlas texture while the render device is still alive (call from
            // RenderSystem::onShutdown). CPU state is retained so rendering can resume.
            void releaseGpu();

            [[nodiscard]] uint32_t bindlessIndex() const { return m_BindlessIndex; }

            // Raw atlas texture, for passes that bind it directly instead of via the bindless pool.
            [[nodiscard]] const rhi::Texture* texture() const { return m_Texture.get(); }

        private:
            struct Face;
            Face* acquireFace(uint64_t fontKey);

            // FreeType (FT_Library, opaque here to keep FreeType out of the header).
            void*                                               m_Library {nullptr};
            std::unordered_map<uint64_t, std::unique_ptr<Face>> m_Faces;

            // CPU R8 atlas + shelf packer.
            uint32_t             m_Width {2048};
            uint32_t             m_Height {2048};
            std::vector<uint8_t> m_Pixels;
            uint32_t             m_PenX {1};
            uint32_t             m_PenY {1};
            uint32_t             m_RowHeight {0};
            bool                 m_Dirty {false};
            bool                 m_Full {false};

            struct GlyphKey
            {
                uint64_t fontKey {0};
                uint32_t pixelSize {0};
                uint32_t codepoint {0};
                bool     operator==(const GlyphKey&) const = default;
            };
            struct GlyphKeyHash
            {
                std::size_t operator()(const GlyphKey& k) const noexcept;
            };
            std::unordered_map<GlyphKey, Glyph, GlyphKeyHash> m_Glyphs;

            // GPU atlas texture (shared with the GPU resource pool via the Ref).
            Ref<rhi::Texture> m_Texture {nullptr};
            uint32_t          m_BindlessIndex {0};
        };
    } // namespace rendering
} // namespace vultra
