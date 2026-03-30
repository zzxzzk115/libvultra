#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <vultra/platform/android/android_demo_app_launcher.hpp>

extern "C" void android_main(android_app* app)
{
    vultra::platform::android::AndroidDemoAppLauncher::run(app);
}
