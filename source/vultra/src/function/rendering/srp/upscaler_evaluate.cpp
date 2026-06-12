#include "vultra/function/rendering/srp/upscaler_evaluate.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/framegraph/framegraph_context.hpp"
#include "vultra/function/rendering/srp/render_view.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace vultra
{
    namespace
    {
        [[nodiscard]] UpscalerConstants makeUpscalerConstants(const RenderCamera* camera,
                                                              const rhi::Extent2D viewExtent,
                                                              const bool          hasMotion)
        {
            UpscalerConstants constants {};
            if (camera == nullptr)
                return constants;

            constants.view                   = camera->view;
            constants.projection             = camera->projection;
            constants.viewProjection         = camera->viewProjection;
            constants.previousView           = camera->previousView;
            constants.previousProjection     = camera->previousProjection;
            constants.previousViewProjection = camera->previousViewProjection;
            constants.clipToPreviousClip     = camera->previousViewProjection * camera->inverseViewProjection;
            constants.previousClipToClip     = camera->viewProjection * glm::inverse(camera->previousViewProjection);
            constants.jitterOffsetPx         = camera->jitterOffsetPx;
            constants.cameraPosition         = glm::vec3(camera->inverseView[3]);
            constants.cameraUp               = glm::normalize(glm::vec3(camera->inverseView[1]));
            constants.cameraRight            = glm::normalize(glm::vec3(camera->inverseView[0]));
            constants.cameraForward          = glm::normalize(-glm::vec3(camera->inverseView[2]));
            constants.nearPlane              = camera->zNear;
            constants.farPlane               = camera->zFar;
            constants.fovYRadians            = camera->fovY;
            constants.aspectRatio            = static_cast<float>(std::max(viewExtent.width, 1u)) /
                                    static_cast<float>(std::max(viewExtent.height, 1u));
            constants.reset                = !camera->hasPreviousViewProjection;
            constants.cameraMotionIncluded = hasMotion;
            return constants;
        }

        void appendResourceTag(std::vector<UpscalerResourceTag>& tags,
                               const UpscalerResourceRole        role,
                               const rhi::Texture*               texture,
                               const rhi::RenderBackendApi       backendApi,
                               const std::optional<uint32_t>     layer)
        {
            if (texture == nullptr)
                return;
            // Slice only genuinely layered textures; a mono resource feeding a stereo
            // evaluate is passed whole for both eyes.
            const bool slice = layer.has_value() && texture->getNumLayers() > *layer;
            tags.push_back({
                .role     = role,
                .resource = slice ? makeNativeTextureResource(*texture, backendApi, *layer) :
                                    makeNativeTextureResource(*texture, backendApi),
            });
        }
    } // namespace

    bool evaluateUpscalerForView(FrameGraphExecContext&          rc,
                                 IRenderUpscalerService&         upscaler,
                                 const UpscalerSettings&         settings,
                                 const UpscalerEvaluateTextures& textures)
    {
        if (textures.color == nullptr || textures.output == nullptr)
            return false;

        const auto&    view       = rc.view();
        const auto     backendApi = rc.rd.getBackendApi();
        const uint32_t sharedLayers =
            std::min(std::max(textures.color->getNumLayers(), 1u), std::max(textures.output->getNumLayers(), 1u));
        const bool perEye = view.usesSingleGraphStereo() && sharedLayers >= 2u;
        // Providers (Streamline/NGX) receive no array-layer information, so two evaluations
        // writing different layers of one image discard each other's result. Stereo therefore
        // requires dedicated per-eye staging outputs; without them, fall back to the blit path.
        const bool haveEyeOutputs = textures.eyeOutputs[0] != nullptr && textures.eyeOutputs[1] != nullptr;
        if (perEye && !haveEyeOutputs)
        {
            static bool s_WarnedMissingStagings = false;
            if (!s_WarnedMissingStagings)
            {
                s_WarnedMissingStagings = true;
                VULTRA_CORE_WARN("[UpscalerEvaluate] Stereo view without per-eye staging outputs; skipping "
                                 "upscaler evaluation.");
            }
            return false;
        }
        const uint32_t evaluateCount = perEye ? 2u : 1u;

        static bool s_LoggedStereoEvaluateOnce = false;

        for (uint32_t eye = 0; eye < evaluateCount; ++eye)
        {
            const RenderCamera* camera = view.camera;
            if (perEye && view.multiviewCameraCount > eye && view.multiviewCameras[eye] != nullptr)
                camera = view.multiviewCameras[eye];

            const std::optional<uint32_t> layer     = perEye ? std::optional<uint32_t> {eye} : std::nullopt;
            rhi::Texture*                 outputTex = perEye ? textures.eyeOutputs[eye] : textures.output;

            std::vector<UpscalerResourceTag> tags;
            appendResourceTag(tags, UpscalerResourceRole::eScalingInputColor, textures.color, backendApi, layer);
            // The staging output is a dedicated single-layer image; never slice it.
            appendResourceTag(tags, UpscalerResourceRole::eScalingOutputColor, outputTex, backendApi, std::nullopt);
            appendResourceTag(tags, UpscalerResourceRole::eDepth, textures.depth, backendApi, layer);
            appendResourceTag(tags, UpscalerResourceRole::eMotionVectors, textures.motion, backendApi, layer);
            // Exposure is a 1x1-style auxiliary input shared by both eyes.
            appendResourceTag(tags, UpscalerResourceRole::eExposure, textures.exposure, backendApi, std::nullopt);

            const uint32_t           eyeIndex   = perEye ? eye : (camera != nullptr ? camera->viewIndex : 0u);
            const UpscalerViewportId viewportId = camera != nullptr ?
                                                      makeUpscalerViewportId(camera->uuid, eyeIndex) :
                                                      UpscalerViewportId {eyeIndex};

            if (perEye && !s_LoggedStereoEvaluateOnce)
            {
                VULTRA_CORE_INFO("[UpscalerEvaluate] stereo eye {}: viewport={} colorView={:#x} "
                                 "outputView={:#x} render={}x{} output={}x{} camera='{}' viewIndex={}",
                                 eye,
                                 viewportId,
                                 tags.size() > 0 ? tags[0].resource.imageViewHandle : 0u,
                                 tags.size() > 1 ? tags[1].resource.imageViewHandle : 0u,
                                 textures.color->getExtent().width,
                                 textures.color->getExtent().height,
                                 outputTex->getExtent().width,
                                 outputTex->getExtent().height,
                                 camera != nullptr ? camera->name : "<null>",
                                 camera != nullptr ? camera->viewIndex : 0u);
            }

            const NativeCommandContext command {
                .commandBufferHandle = rc.cb.getHandle(),
                .frameIndex          = rc.frame.frameIndex,
                .viewportId          = viewportId,
                .frameToken =
                    {
                        .frameIndex = rc.frame.frameIndex,
                        .viewSlot   = viewportId,
                    },
            };
            upscaler.beginFrame(command);

            const UpscalerEvaluateContext eval {
                .command      = command,
                .settings     = settings,
                .constants    = makeUpscalerConstants(camera, view.extent, textures.motion != nullptr),
                .renderExtent = textures.color->getExtent(),
                .outputExtent = outputTex->getExtent(),
                .resources    = tags,
            };

            if (!upscaler.evaluate(eval))
            {
                if (perEye)
                    VULTRA_CORE_WARN("[UpscalerEvaluate] stereo eye {} evaluate failed; falling back to blit for "
                                     "both eyes.",
                                     eye);
                return false;
            }
        }

        if (perEye)
        {
            // Assemble the layered output from the per-eye stagings after both evaluations,
            // so no provider-side barrier can touch the layered image between eye writes.
            for (uint32_t eye = 0; eye < 2u; ++eye)
            {
                rc.cb.blit(*textures.eyeOutputs[eye],
                           *textures.output,
                           rhi::TexelFilter::eNearest,
                           0u,
                           0u,
                           0u,
                           eye,
                           1u);
            }
            s_LoggedStereoEvaluateOnce = true;
        }

        return true;
    }
} // namespace vultra
