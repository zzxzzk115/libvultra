#pragma once

#include <vultra/core/rhi/structs/extent2d.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/services/imgui_service.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace vultra_app::ui
{
    // A render target backing an editor viewport/preview: the offscreen texture, its size, the ImGui
    // texture id it is registered under (null when the target is never displayed, e.g. a picking target),
    // and the frame at which a retired copy may be freed.
    struct RenderTargetSlot
    {
        std::optional<vultra::rhi::Texture> texture;
        vultra::rhi::Extent2D               extent {};
        // >1 for layered/stereo targets (render-graph overlay); 1 for the common single-layer case.
        uint32_t                            layerCount {1};
        vultra::IImGuiService::TextureID    textureId {};
        // Frame the target was created on; used by viewports that cache a static frame (scene/game/
        // render-graph). Left at 0 by windows that don't need it (e.g. the inspector model preview).
        uint64_t                            frameCreated {0};
        uint64_t                            releaseFrame {0};
    };

    // Deferred-release list for render targets that were resized/replaced. ImGui keeps referencing a
    // texture for a few frames after we stop drawing it, so a target cannot be freed the same frame it
    // is swapped out. Editor windows park the old slot here and reclaim it once the delay has elapsed.
    //
    // This converges what used to be a hand-written retire helper + reclaim loop duplicated across every
    // viewport/preview window (scene view, game view, render-graph, material-graph, inspector preview).
    class RetiredRenderTargets
    {
    public:
        // Park `slot` for deferred release (no-op if it holds nothing); clears `slot` afterwards.
        void retire(RenderTargetSlot& slot, uint64_t currentFrame, uint64_t delayFrames)
        {
            if (!slot.texture && !slot.textureId)
                return;
            slot.releaseFrame = currentFrame + delayFrames;
            m_Slots.push_back(std::move(slot));
            slot = {};
        }

        // Free every parked target whose release frame has passed, freeing its ImGui texture id (when
        // present) via `imguiService`. Pass nullptr for targets that never register an ImGui id.
        void reclaim(vultra::IImGuiService* imguiService, uint64_t currentFrame)
        {
            std::size_t out = 0;
            for (auto& slot : m_Slots)
            {
                if (currentFrame >= slot.releaseFrame)
                {
                    if (imguiService && slot.textureId)
                        imguiService->removeTexture(slot.textureId);
                    slot.texture.reset();
                }
                else
                {
                    m_Slots[out++] = std::move(slot);
                }
            }
            m_Slots.resize(out);
        }

        // Free all parked targets immediately, regardless of release frame (e.g. on window destroy).
        void releaseAll(vultra::IImGuiService* imguiService)
        {
            for (auto& slot : m_Slots)
            {
                if (imguiService && slot.textureId)
                    imguiService->removeTexture(slot.textureId);
                slot.texture.reset();
            }
            m_Slots.clear();
        }

        [[nodiscard]] bool empty() const { return m_Slots.empty(); }

    private:
        std::vector<RenderTargetSlot> m_Slots;
    };
} // namespace vultra_app::ui
