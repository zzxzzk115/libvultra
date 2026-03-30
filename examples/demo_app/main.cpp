#include <vultra/core/app/demo_app_entry.hpp>
#include <vultra/core/app/demo_app_host.hpp>

#include "demo_app_logic.hpp"

using namespace vultra;

class DemoApp final : public DemoAppHost
{
protected:
    void onPostConfigureDemo(Engine& engine) override { examples::setupDemoScene(engine); }
};

VULTRA_DEMO_APP_MAIN(DemoApp)
