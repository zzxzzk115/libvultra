#include "../common/triangle.hpp"
#include "../common/xr_sample.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

class XrTriangleApp final : public sample::XrSample
{
public:
    explicit XrTriangleApp(const sample::Options& options) :
        XrSample(options, "Vultra | OpenXR - Stereo Triangle and Mirror"),
        m_Triangle(device(), eyeFormat(), "examples/research/shaders/triangle.slang")
    {
        m_Triangle.parameters.decodeSrgb = 1;
    }

private:
    void onUpdate(float) override
    {
        m_Triangle.pipeline->poll();
    }

    void onRenderEye(VriCommandBuffer* cmd, const vultra::XREye& eye, uint32_t) override
    {
        const auto transform = eye.viewProjection() * glm::translate(glm::mat4(1), glm::vec3(0, 0, -2.5f));
        std::copy_n(glm::value_ptr(transform), 16, m_Triangle.parameters.transform.begin());
        const float clear[] {0.035f / 12.92f,
                             std::pow((0.045f + 0.055f) / 1.055f, 2.4f),
                             std::pow((0.065f + 0.055f) / 1.055f, 2.4f),
                             1};
        eye.color->transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        m_Triangle.draw(cmd, *eye.color, clear);
    }

    Triangle m_Triangle;
};

int main(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    XrTriangleApp app(*options);
    app.run(options->frames);
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
