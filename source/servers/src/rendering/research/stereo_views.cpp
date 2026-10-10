#include <vultra/servers/rendering/research/stereo_views.hpp>

#include <glm/ext/matrix_transform.hpp>

namespace vultra
{
    StereoFrameViews makeStereoFrameViews(const FpsCamera&      rigCamera,
                                          const XRFrame&        frame,
                                          std::array<Extent, 2> sizes,
                                          float                 ipd,
                                          uint64_t              index,
                                          const HeadsetProfile* profile,
                                          const glm::mat4&      trackingPose)
    {
        return makeStereoFrameViews(rigCamera.camera(sizes[0]), frame, sizes, ipd, index, profile, trackingPose);
    }

    StereoFrameViews makeStereoFrameViews(const RenderCamera&   rigCamera,
                                          const XRFrame&        frame,
                                          std::array<Extent, 2> sizes,
                                          float                 ipd,
                                          uint64_t              index,
                                          const HeadsetProfile* profile,
                                          const glm::mat4&      trackingPose)
    {
        StereoFrameViews result;
        result.index      = index;
        result.cameras[0] = rigCamera;
        auto rig          = glm::inverse(rigCamera.view);
        if (!frame.shouldRender && trackingPose != glm::mat4(1))
        {
            rig *= trackingPose;
        }
        for (size_t eye = 0; eye < 2; ++eye)
        {
            if (frame.shouldRender)
            {
                const auto eyeWorld     = frame.eyes[eye].poseMatrix();
                result.cameras[eye + 1] = {glm::inverse(rig * eyeWorld),
                                           frame.eyes[eye].viewProjection(rigCamera.nearPlane, rigCamera.farPlane) *
                                               eyeWorld,
                                           rigCamera.nearPlane,
                                           rigCamera.farPlane};
            }
            else
            {
                const auto offset       = profile ? profile->eyes[eye].pose :
                                                    glm::translate(glm::mat4(1), glm::vec3((eye ? 0.5f : -0.5f) * ipd, 0, 0));
                result.cameras[eye + 1] = rigCamera;
                if (profile)
                {
                    result.cameras[eye + 1].projection =
                        eyeProjection(profile->eyes[eye], rigCamera.nearPlane, rigCamera.farPlane);
                }
                else
                {
                    result.cameras[eye + 1].projection[0][0] =
                        rigCamera.projection[1][1] * sizes[eye].height / sizes[eye].width;
                }
                result.cameras[eye + 1].view = glm::inverse(rig * offset);
            }
        }
        if (!frame.shouldRender && profile)
        {
            result.cameras[0].projection = result.cameras[1].projection;
        }
        if (!frame.shouldRender && (profile || trackingPose != glm::mat4(1)))
        {
            result.cameras[0].view = glm::inverse(rig);
        }
        // Source policy is deliberately explicit: tracked midpoint pose, left-eye projection.
        // Replace this policy alongside your method if its source camera has a different contract.
        if (frame.shouldRender)
        {
            result.cameras[0]      = result.cameras[1];
            result.cameras[0].view = glm::inverse(rig * frame.headPose());
        }
        return result;
    }

} // namespace vultra
