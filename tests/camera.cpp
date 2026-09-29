#include "../examples/common/colored_mesh.hpp"

#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/camera/fps_camera.hpp>
#include <vultra/function/camera/orbit_camera.hpp>
#include <vultra/function/research/capture.hpp>

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

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
        CameraApp() :
            ImGuiApp({"Vultra - camera input test", {320, 240}}, {.persistLayout = false}),
            m_Triangle(getDevice(), getSwapchain().format(), sample::kTriangleVertices, sample::kTriangleIndices)
        {
            m_Camera.yaw      = 0;
            m_Camera.pitch    = 0;
            m_Camera.distance = 3;
            // Exercise the installed ImGui callback and its chain to Window, without OS input injection.
            auto* handle = getWindow().handle();
            m_Scroll     = glfwSetScrollCallback(handle, nullptr);
            glfwSetScrollCallback(handle, m_Scroll);
            // Script focus as well as input: other test windows may take the real desktop focus.
            m_Focus = glfwSetWindowFocusCallback(handle, nullptr);
            m_Focus(handle, GLFW_TRUE);
            m_Key = glfwSetKeyCallback(handle, nullptr);
            glfwSetKeyCallback(handle, m_Key);
        }

    private:
        void onPreUpdate(float) override
        {
            ImGui::SetNextFrameWantCaptureMouse(frameCount() == 3);
        }

        void onPreRender() override
        {
            ImGuiApp::onPreRender();
            const auto& input = getWindow().input();
            if (frameCount() == 1)
            {
                require(input.mouseScrollDelta().y == 2, "Window did not receive chained GLFW scroll events");
                require(input.isKeyHeld(vultra::KeyCode::eW) && input.isKeyPressed(vultra::KeyCode::eW),
                        "GLFW key translation or callback chaining failed");
                require(ImGui::GetIO().MouseWheel == 0, "Fixture must read after ImGui clears its wheel");
            }
            m_Camera.update(input, getWindow().size(), getGui().inputCapture());
        }

        void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
        {
            const auto camera = m_Camera.camera(getSwapchain().size());
            const auto matrix = camera.projection * camera.view;
            std::copy_n(glm::value_ptr(matrix), 16, m_Triangle.parameters.transform.begin());
            const float clear[4] {0, 0, 0, 1};
            m_Triangle.draw(cmd, target, clear);
        }

        void onPostRender(vultra::Texture& target) override
        {
            const auto image  = vultra::readback(getDevice(), target);
            size_t     pixels = 0;
            for (size_t i = 0; i < image.rgba.size(); i += 4)
            {
                pixels += image.rgba[i] + image.rgba[i + 1] + image.rgba[i + 2] > 0.1f;
            }
            if (frameCount() == 0)
            {
                require(pixels > 100, "Camera fixture is not visible");
                m_BaselinePixels = pixels;
                m_Scroll(getWindow().handle(), 0, 0.5);
                m_Scroll(getWindow().handle(), 0, 1.5);
                m_Key(getWindow().handle(), GLFW_KEY_W, 0, GLFW_PRESS, 0);
            }
            else if (frameCount() == 1)
            {
                require(pixels > m_BaselinePixels * 1.4f, "Wheel zoom did not enlarge the rendered model");
                m_Zoomed = image;
            }
            else if (frameCount() == 2)
            {
                require(image.rgba == m_Zoomed.rgba, "Wheel input repeated without another event");
                m_Scroll(getWindow().handle(), 0, -2);
            }
            else if (frameCount() == 3)
            {
                require(image.rgba == m_Zoomed.rgba, "Captured UI scroll changed the rendered model");
                m_Scroll(getWindow().handle(), 0, -2);
            }
            else if (frameCount() == 4)
            {
                require(pixels == m_BaselinePixels, "Opposite wheel input did not restore the framing");
                m_Focus(getWindow().handle(), GLFW_FALSE);
                m_Scroll(getWindow().handle(), 0, 2);
            }
            else if (frameCount() == 5)
            {
                require(!getWindow().input().focused() && getWindow().input().isKeyReleased(vultra::KeyCode::eW),
                        "GLFW focus callback failed to release held keys");
                require(pixels == m_BaselinePixels, "An unfocused window moved the camera");
            }
        }

        vultra::OrbitCamera m_Camera;
        sample::ColoredMesh m_Triangle;
        GLFWscrollfun       m_Scroll         = nullptr;
        GLFWwindowfocusfun  m_Focus          = nullptr;
        GLFWkeyfun          m_Key            = nullptr;
        size_t              m_BaselinePixels = 0;
        vultra::Image       m_Zoomed;
    };
} // namespace

int main()
try
{
    inputAndControllers();
    CameraApp app;
    app.run(6);
    require(app.frameCount() == 6, "Camera regression did not complete every frame");
    std::cout
        << "Camera tests passed: window input, GLFW/ImGui chaining, orbit/pan/zoom, FPS, focus, UI capture, GPU zoom\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
