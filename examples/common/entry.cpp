#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/app_launch.hpp>

#if defined(__ANDROID__)
#include <vultra/platform/android/arguments.hpp>

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <exception>
#include <string>
#include <vector>
#endif

int runExample(const vultra::AppLaunch& launch, int argc, char** argv);

#if defined(__ANDROID__)
extern "C" void android_main(android_app* activity)
{
    try
    {
        auto arguments = vultra::platform::androidArguments(*activity->activity);
        arguments.insert(arguments.begin(), "vultra-example");
        std::vector<char*> argv;
        for (auto& argument : arguments)
        {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);
        const auto status = runExample({activity}, int(argv.size()) - 1, argv.data());
        if (status)
        {
            vultra::Logger::app().error("Example failed with status {}", status);
        }
    }
    catch (const std::exception& error)
    {
        vultra::Logger::app().error("Android example: {}", error.what());
    }
    GameActivity_finish(activity->activity);
}
#else
int main(int argc, char** argv)
{
    return runExample({}, argc, argv);
}
#endif
