#include "vultra/function/rendering/text/glyph_atlas.hpp"

#include "vultra/core/base/common_context.hpp" // VULTRA_CORE_* logging
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/function/resource/gpu_resource_pool.hpp"
#include "vultra/function/resource/gpu_texture.hpp"
#include "vultra/function/services/gpu_resource_service.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstring>

namespace vultra::rendering
{
    struct GlyphAtlas::Face
    {
        std::vector<std::byte> bytes; // FT_New_Memory_Face does not copy; keep the bytes alive.
        FT_Face                ft {nullptr};
        uint32_t               loadedPixelSize {0};
    };

    std::size_t GlyphAtlas::GlyphKeyHash::operator()(const GlyphKey& k) const noexcept
    {
        std::size_t h = std::hash<uint64_t> {}(k.fontKey);
        h ^= std::hash<uint32_t> {}(k.pixelSize) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t> {}(k.codepoint) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }

    // Out-of-line (Face is complete here): keeps unique_ptr<Face> destruction valid.
    GlyphAtlas::GlyphAtlas() = default;

    GlyphAtlas::~GlyphAtlas()
    {
        for (auto& [key, face] : m_Faces)
            if (face && face->ft)
                FT_Done_Face(face->ft);
        m_Faces.clear();
        if (m_Library)
            FT_Done_FreeType(static_cast<FT_Library>(m_Library));
        m_Library = nullptr;
    }

    GlyphAtlas::Face* GlyphAtlas::acquireFace(uint64_t fontKey)
    {
        auto it = m_Faces.find(fontKey);
        return it != m_Faces.end() ? it->second.get() : nullptr;
    }

    bool GlyphAtlas::ensureFont(uint64_t fontKey, const std::function<std::vector<std::byte>()>& bytesProvider)
    {
        if (!m_Library)
        {
            FT_Library lib = nullptr;
            if (FT_Init_FreeType(&lib) != 0 || !lib)
            {
                VULTRA_CORE_ERROR("[GlyphAtlas] FT_Init_FreeType failed; in-game UI text will not render");
                return false;
            }
            m_Library = lib;
        }
        if (auto* existing = acquireFace(fontKey))
            return existing->ft != nullptr;

        auto face   = std::make_unique<Face>();
        face->bytes = bytesProvider ? bytesProvider() : std::vector<std::byte> {};
        if (face->bytes.empty())
        {
            // Cache the failure so we do not invoke the provider every frame.
            m_Faces.emplace(fontKey, std::move(face));
            return false;
        }

        FT_Face ftFace = nullptr;
        const auto err = FT_New_Memory_Face(static_cast<FT_Library>(m_Library),
                                            reinterpret_cast<const FT_Byte*>(face->bytes.data()),
                                            static_cast<FT_Long>(face->bytes.size()),
                                            0,
                                            &ftFace);
        if (err != 0 || !ftFace)
        {
            VULTRA_CORE_WARN("[GlyphAtlas] FT_New_Memory_Face failed (err={})", static_cast<int>(err));
            m_Faces.emplace(fontKey, std::move(face)); // cache failure (ft == nullptr)
            return false;
        }
        face->ft = ftFace;
        m_Faces.emplace(fontKey, std::move(face));
        return true;
    }

    bool GlyphAtlas::fontMetrics(uint64_t fontKey, uint32_t pixelSize, float& outAscentPx, float& outLineHeightPx)
    {
        auto* face = acquireFace(fontKey);
        if (!face || !face->ft || pixelSize == 0u)
            return false;
        if (face->loadedPixelSize != pixelSize)
        {
            FT_Set_Pixel_Sizes(face->ft, 0, pixelSize);
            face->loadedPixelSize = pixelSize;
        }
        outAscentPx     = static_cast<float>(face->ft->size->metrics.ascender) / 64.0f;
        outLineHeightPx = static_cast<float>(face->ft->size->metrics.height) / 64.0f;
        return true;
    }

    const GlyphAtlas::Glyph* GlyphAtlas::getGlyph(uint64_t fontKey, uint32_t pixelSize, uint32_t codepoint)
    {
        if (pixelSize == 0u)
            return nullptr;

        const GlyphKey key {fontKey, pixelSize, codepoint};
        if (auto it = m_Glyphs.find(key); it != m_Glyphs.end())
            return &it->second;

        if (m_Pixels.empty())
            m_Pixels.assign(static_cast<size_t>(m_Width) * m_Height, 0u);

        auto* face = acquireFace(fontKey);
        if (!face || !face->ft)
            return nullptr;

        if (face->loadedPixelSize != pixelSize)
        {
            FT_Set_Pixel_Sizes(face->ft, 0, pixelSize);
            face->loadedPixelSize = pixelSize;
        }

        if (FT_Load_Char(face->ft, codepoint, FT_LOAD_RENDER) != 0)
            return nullptr;

        const FT_GlyphSlot slot = face->ft->glyph;
        Glyph              g {};
        g.advancePx = static_cast<float>(slot->advance.x) / 64.0f;
        g.bearingPx = {static_cast<float>(slot->bitmap_left), static_cast<float>(slot->bitmap_top)};

        const uint32_t w = slot->bitmap.width;
        const uint32_t h = slot->bitmap.rows;
        g.sizePx         = {static_cast<float>(w), static_cast<float>(h)};

        if (w > 0u && h > 0u)
        {
            // Shelf-pack with 1px padding.
            if (m_PenX + w + 1u > m_Width)
            {
                m_PenX = 1u;
                m_PenY += m_RowHeight + 1u;
                m_RowHeight = 0u;
            }
            if (m_PenY + h + 1u > m_Height)
            {
                if (!m_Full)
                {
                    VULTRA_CORE_WARN("[GlyphAtlas] atlas full ({}x{}); further glyphs dropped", m_Width, m_Height);
                    m_Full = true;
                }
                // Cache as a metrics-only glyph (no bitmap) so layout still advances.
                g.hasBitmap = false;
                auto [ins, ok] = m_Glyphs.emplace(key, g);
                return &ins->second;
            }

            const uint32_t x0 = m_PenX;
            const uint32_t y0 = m_PenY;
            const int      pitch = slot->bitmap.pitch;
            const auto*    src = slot->bitmap.buffer;
            for (uint32_t row = 0u; row < h; ++row)
            {
                const auto* srcRow = src + static_cast<ptrdiff_t>(row) * pitch;
                uint8_t*    dstRow = m_Pixels.data() + static_cast<size_t>(y0 + row) * m_Width + x0;
                std::memcpy(dstRow, srcRow, w);
            }

            g.uvMin     = {static_cast<float>(x0) / m_Width, static_cast<float>(y0) / m_Height};
            g.uvMax     = {static_cast<float>(x0 + w) / m_Width, static_cast<float>(y0 + h) / m_Height};
            g.hasBitmap = true;

            m_PenX += w + 1u;
            m_RowHeight = std::max(m_RowHeight, h);
            m_Dirty     = true;
        }

        auto [ins, ok] = m_Glyphs.emplace(key, g);
        return &ins->second;
    }

    void GlyphAtlas::releaseGpu()
    {
        // Drop our reference to the atlas texture so it is destroyed while the render device is still
        // alive (called from RenderSystem::onShutdown). The CPU atlas/glyph cache are kept so the
        // atlas can be re-registered and re-uploaded if rendering resumes.
        m_Texture       = nullptr;
        m_BindlessIndex = 0u;
        m_Dirty         = true;
    }

    uint32_t GlyphAtlas::ensureRegistered(rhi::RenderDevice& rd, IGpuResourceService& gpu)
    {
        if (m_Pixels.empty())
            m_Pixels.assign(static_cast<size_t>(m_Width) * m_Height, 0u);

        // Create the GPU texture once.
        if (!m_Texture)
        {
            rhi::Texture texture = rhi::Texture::Builder {}
                                       .setExtent(rhi::Extent2D {m_Width, m_Height})
                                       .setPixelFormat(rhi::PixelFormat::eR8_UNorm)
                                       .setNumMipLevels(1u)
                                       .setUsageFlags(rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst)
                                       .setupOptimalSampler(true)
                                       .build(rd);
            if (!texture)
            {
                VULTRA_CORE_ERROR("[GlyphAtlas] failed to create R8 atlas texture");
                return 0u;
            }
            m_Texture     = createRef<rhi::Texture>(std::move(texture));
            m_BindlessIndex = 0u; // force (re)registration below
        }

        // The GPU resource pool is cleared/rebuilt on scene topology changes, which drops our
        // bindless slot. Re-register whenever the pool no longer holds our texture at m_BindlessIndex
        // so the UI overlay pass can always resolve the atlas (otherwise glyphs sample the white
        // fallback texture and render as solid boxes).
        auto&      pool       = gpu.pool();
        const auto handles    = pool.getBindlessTextureHandles();
        const bool stillBound = m_BindlessIndex != 0u && m_BindlessIndex < handles.size() &&
                                handles[m_BindlessIndex] == m_Texture.get();
        if (!stillBound)
        {
            resource::GpuTexture gt;
            gt.texture      = m_Texture; // share ownership with the pool
            m_BindlessIndex = gpu.createTexture(rd, std::move(gt));
            m_Dirty         = true; // re-upload glyph data into the (new) texture slot
        }

        // Upload any pending glyph data (initial transparent fill on first registration).
        flush(rd);
        return m_BindlessIndex;
    }

    void GlyphAtlas::flush(rhi::RenderDevice& rd)
    {
        if (!m_Dirty || !m_Texture)
            return;

        const uint64_t size    = static_cast<uint64_t>(m_Width) * m_Height; // R8: 1 byte/pixel
        auto           staging = rd.createStagingBuffer(size, m_Pixels.data());
        if (!staging)
        {
            VULTRA_CORE_WARN("[GlyphAtlas] failed to create staging buffer for atlas upload");
            return;
        }
        rhi::upload(rd, staging, {}, *m_Texture, false);
        m_Dirty = false;
    }
} // namespace vultra::rendering
