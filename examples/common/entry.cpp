#include <vultra/app/app_launch.hpp>
#include <vultra/core/base/logger.hpp>

#if defined(__ANDROID__)
#include <vultra/platform/android/arguments.hpp>

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <string>
#include <vector>
#endif

int runExample(const vultra::AppLaunch& launch, int argc, char** argv);

#if defined(__ANDROID__)
extern "C" void android_main(android_app* activity)
{
    auto arguments = vultra::platform::androidArguments(*activity->activity);
    if (!arguments)
    {
        vultra::Logger::app().error("Android example: {}", arguments.error().message);
        GameActivity_finish(activity->activity);
        return;
    }
    arguments->insert(arguments->begin(), "vultra-example");
    std::vector<char*> argv;
    for (auto& argument : *arguments)
    {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    const auto status = runExample({activity}, int(argv.size()) - 1, argv.data());
    if (status)
    {
        vultra::Logger::app().error("Example failed with status {}", status);
    }
    GameActivity_finish(activity->activity);
}
#else
int main(int argc, char** argv)
{
    return runExample({}, argc, argv);
}
#endif
