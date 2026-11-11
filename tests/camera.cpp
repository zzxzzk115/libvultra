#include "../examples/common/colored_mesh.hpp"
#include "support/check.hpp"
#include "window_events.hpp"

#include <vultra/app/camera/fps_camera.hpp>
#include <vultra/app/camera/orbit_camera.hpp>
#include <vultra/app/imgui_app.hpp>
#include <vultra/gpu/image_readback.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    // Present initial frames before exact comparisons: Wayland negotiates surface size and scale asynchronously.
    constexpr uint64_t kWarmupFrames = 4;

    using support::must;
    using support::require;

    using Result = std::expected<void, vultra::Error>;

    void inputAndControllers()
    {
        using namespace vultra;
        Input input;
        input.setKey(KeyCode::eW, InputAction::ePress);
        input.setKey(KeyCode::eW, InputAction::eRelease);
        input.addMouseScroll({0, 0.5f});
        input.addMouseScroll({0, 1.5f});
        input.advanceFrame();
        require(input.isKeyPressed(KeyCode::eW) && input.isKeyReleased(KeyCode::eW) && !input.isKeyHeld(KeyCode::eW),
                "A quick press/release was lost between polls");
        require(input.mouseScrollDelta().y == 2, "Scroll events were not accumulated");
        OrbitCamera orbit;
        const float distance = orbit.distance;
        orbit.update(input, {800, 600}, {.mouse = true});
        require(orbit.distance == distance, "UI scroll changed the orbit camera");
        orbit.update(input, {800, 600});
        require(orbit.distance < distance, "Wheel up did not dolly towards the target");
        const float zoomed = orbit.distance;
        input.advanceFrame();
        orbit.update(input, {800, 600});
        require(orbit.distance == zoomed && !input.isKeyPressed(KeyCode::eW) && !input.isKeyReleased(KeyCode::eW),
                "Input transients repeated on a later frame");

        input.setMousePosition({100, 100});
        input.setMouseButton(MouseCode::eMiddle, true);
        input.advanceFrame();
        input.setMousePosition({120, 110});
        input.advanceFrame();
        const auto offset    = orbit.position() - orbit.center;
        const auto oldCenter = orbit.center;
        orbit.update(input, {800, 600});
        require(glm::length(orbit.center - oldCenter) > 0.01f &&
                    glm::length(orbit.position() - orbit.center - offset) < 0.00001f,
                "Panning must translate the eye and target together");
        input.setMouseButton(MouseCode::eMiddle, false);
        input.setMouseButton(MouseCode::eLeft, true);
        input.advanceFrame();
        input.setMousePosition({140, 10000});
        input.advanceFrame();
        const float yaw = orbit.yaw;
        orbit.update(input, {800, 600});
        require(orbit.yaw != yaw && std::abs(orbit.pitch) <= 1.5f,
                "Orbit dragging failed or crossed a polar singularity");

        input.setKey(KeyCode::eW, InputAction::ePress);
        input.setKey(KeyCode::eD, InputAction::ePress);
        input.advanceFrame();
        FpsCamera  fps;
        const auto start = fps.position;
        fps.update(input, 0.5f, {.keyboard = true});
        require(fps.position == start, "Typing into UI moved the camera");
        fps.update(input, 0.5f);
        require(std::abs(glm::length(fps.position - start) - fps.speed * 0.5f) < 0.00001f,
                "Diagonal movement changed the configured speed");
        const auto beforeFast = fps.position;
        input.setKey(KeyCode::eLShift, InputAction::ePress);
        input.setKey(KeyCode::eW, InputAction::eRepeat);
        input.advanceFrame();
        require(input.isKeyRepeated(KeyCode::eW) && !input.isKeyPressed(KeyCode::eW), "Key repeat became a press");
        fps.update(input, 0.5f);
        require(std::abs(glm::length(fps.position - beforeFast) - fps.speed * fps.fastMultiplier * 0.5f) < 0.00001f,
                "Shift movement multiplier failed");
        input.setFocused(false);
        input.advanceFrame();
        const auto beforeFocus = fps.position;
        fps.update(input, 1);
        require(fps.position == beforeFocus && !input.isKeyHeld(KeyCode::eW) && input.isKeyReleased(KeyCode::eW),
                "Lost focus left a key held");
        input.setFocused(true);
        input.setMousePosition({5000, 5000});
        input.advanceFrame();
        require(input.mousePositionDelta() == glm::vec2(0), "Focus regain produced a pointer jump");
        Input anotherWindow;
        anotherWindow.advanceFrame();
        require(anotherWindow.mousePosition() == glm::vec2(0), "Window input state leaked to another window");
    }

    class CameraApp final : public vultra::ImGuiApp
    {
    public:
        static inline const vultra::AppConfig kConfig {"Vultra - camera input test", {640, 480}};

        // The fixture's own GPU objects are expected to build; a failure ends the test.
        explicit CameraApp(Services services) :
            ImGuiApp(std::move(services), kConfig),
            m_Triangle(must(sample::ColoredMesh::create(getDevice(),
                                                        getSwapchain().format(),
                                                        sample::kTriangleVertices,
                                                        sample::kTriangleIndices))),
            m_Events(getWindow())
        {
            m_Camera.yaw      = 0;
            m_Camera.pitch    = 0;
            m_Camera.distance = 3;
            m_Events.focus(true);
        }

    private:
        Result onPreUpdate(float) override
        {
            ImGui::SetNextFrameWantCaptureMouse(frameCount() == kWarmupFrames + 3);
            return {};
        }

        Result onPreRender() override
        {
            if (auto result = ImGuiApp::onPreRender(); !result)
            {
                return result;
            }
            const auto& input = getWindow().input();
            if (frameCount() == kWarmupFrames + 1)
            {
                require(input.mouseScrollDelta().y == 2, "Window did not receive backend scroll events");
                require(input.isKeyHeld(vultra::KeyCode::eW) && input.isKeyPressed(vultra::KeyCode::eW),
                        "Backend key translation or GUI event forwarding failed");
                require(ImGui::GetIO().MouseWheel == 0, "Fixture must read after ImGui clears its wheel");
            }
            m_Camera.update(input, getWindow().size(), getEditorGui().inputCapture());
            return {};
        }

        Result onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
        {
            const auto camera = m_Camera.camera(getSwapchain().size());
            const auto matrix = camera.projection * camera.view;
            std::copy_n(glm::value_ptr(matrix), 16, m_Triangle->parameters.transform.begin());
            const float clear[4] {0, 0, 0, 1};
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            return m_Triangle->draw(cmd, target, clear);
        }

        Result onPostRender(vultra::Texture& target) override
        {
            if (frameCount() < kWarmupFrames)
            {
                return {};
            }
            const auto testFrame = frameCount() - kWarmupFrames;
            const auto image     = must(vultra::readback(getDevice(), target));
            size_t     pixels    = 0;
            for (size_t i = 0; i < image.rgba.size(); i += 4)
            {
                pixels += image.rgba[i] + image.rgba[i + 1] + image.rgba[i + 2] > 0.1f;
            }
            if (testFrame == 0)
            {
                require(pixels > 100, "Camera fixture is not visible");
                m_BaselinePixels = pixels;
                m_Events.scroll(0.5f);
                m_Events.scroll(1.5f);
                m_Events.pressForward();
            }
            else if (testFrame == 1)
            {
                require(pixels > m_BaselinePixels * 1.4f, "Wheel zoom did not enlarge the rendered model");
                m_Zoomed = image;
            }
            else if (testFrame == 2)
            {
                require(image.rgba == m_Zoomed.rgba, "Wheel input repeated without another event");
                m_Events.scroll(-2);
            }
            else if (testFrame == 3)
            {
                require(image.rgba == m_Zoomed.rgba, "Captured UI scroll changed the rendered model");
                m_Events.scroll(-2);
            }
            else if (testFrame == 4)
            {
                require(pixels == m_BaselinePixels, "Opposite wheel input did not restore the framing");
                m_Events.focus(false);
                m_Events.scroll(2);
            }
            else if (testFrame == 5)
            {
                require(!getWindow().input().focused() && getWindow().input().isKeyReleased(vultra::KeyCode::eW),
                        "Backend focus event failed to release held keys");
                require(pixels == m_BaselinePixels, "An unfocused window moved the camera");
            }
            return {};
        }

        vultra::OrbitCamera                  m_Camera;
        std::unique_ptr<sample::ColoredMesh> m_Triangle;
        test::WindowEvents                   m_Events;
        size_t                               m_BaselinePixels = 0;
        vultra::Image                        m_Zoomed;
    };
} // namespace

int main(int argc, char** argv)
{
    const bool offline = argc == 2 && std::string_view(argv[1]) == "--offline";
    require(argc == 1 || offline, "Usage: test-camera [--offline]");
    inputAndControllers();
    if (offline)
    {
        std::cout << "Offline camera tests passed: input state, orbit/pan/zoom, FPS, focus and UI capture\n";
        return 0;
    }
    CameraApp app(must(vultra::ImGuiApp::createServices(CameraApp::kConfig, {.persistLayout = false})));
    must(app.run(kWarmupFrames + 6));
    require(app.frameCount() == kWarmupFrames + 6, "Camera regression did not complete every frame");
    std::cout << "Camera tests passed: window input, backend/ImGui event forwarding, orbit/pan/zoom, FPS, focus, UI "
                 "capture, GPU zoom\n";
    return 0;
}
