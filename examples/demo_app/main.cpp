#include <vultra/core/app/app_host.hpp>
#include <vultra/core/input/input_system.hpp>
#include <vultra/core/os/window_system.hpp>

using namespace vultra;

class DemoAppHost : public AppHost
{
protected:
    void onConfigure(Engine& engine) override
    {
        engine.emplaceSubsystem<WindowSystem>();
        engine.emplaceSubsystem<InputSystem>();
    }

    void onPollEvents() override
    {
        auto& win = engineCtx().services.require<IWindowService>().window();
        win.pollEvents();

		auto& input = engineCtx().services.require<IInputService>();
		if (input.getKeyDown(KeyCode::eEscape))
		{
			win.close();
		}
    }

    bool onShouldClose() const override
    {
        auto& win = engineCtx().services.require<IWindowService>().window();
        return win.shouldClose();
    }
};

int main()
{
    DemoAppHost app {};
    return app.run();
}