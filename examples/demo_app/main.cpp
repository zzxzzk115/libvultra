#include <vultra/core/app/demo_app_entry.hpp>
#include <vultra/core/app/demo_app_host.hpp>

#include "demo_app_logic.hpp"

using namespace vultra;

class DemoApp final : public DemoAppHost
{
protected:
    void onPostConfigureDemo(Engine& engine) override { examples::setupDemoScene(engine); }
    bool demoEnableExperimentalWebGPUContent() const override { return true; }
};

VULTRA_DEMO_APP_MAIN(DemoApp)
