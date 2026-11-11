#include <vultra/platform/android/arguments.hpp>

#include <game-activity/GameActivity.h>

#include <optional>

namespace vultra::platform
{
    namespace
    {
        // Owns one pushed JNI local frame and, when this thread was detached, the thread attachment.
        class JavaScope
        {
        public:
            JavaScope(JavaVM& vm, JNIEnv& env, bool attached) :
                env(env),
                m_Vm(vm),
                m_Attached(attached)
            {
            }

            ~JavaScope()
            {
                env.PopLocalFrame(nullptr);
                if (m_Attached)
                {
                    m_Vm.DetachCurrentThread();
                }
            }

            JavaScope(const JavaScope&)            = delete;
            JavaScope& operator=(const JavaScope&) = delete;

            std::expected<void, Error> check(const char* operation)
            {
                if (env.ExceptionCheck())
                {
                    env.ExceptionClear();
                    return makeError("{}", operation);
                }
                return {};
            }

            JNIEnv& env;

        private:
            JavaVM& m_Vm;
            bool    m_Attached;
        };

        std::expected<void, Error> enterJava(JavaVM& vm, std::optional<JavaScope>& scope)
        {
            JNIEnv*    env      = nullptr;
            bool       attached = false;
            const auto status   = vm.GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            if (status == JNI_EDETACHED)
            {
                if (vm.AttachCurrentThread(&env, nullptr) != JNI_OK)
                {
                    return makeError("Attach GameActivity native thread to Java VM");
                }
                attached = true;
            }
            else if (status != JNI_OK)
            {
                return makeError("Get GameActivity Java environment");
            }
            if (env->PushLocalFrame(16) != JNI_OK)
            {
                if (attached)
                {
                    vm.DetachCurrentThread();
                }
                return makeError("Allocate GameActivity JNI local frame");
            }
            scope.emplace(vm, *env, attached);
            return {};
        }
    } // namespace

    std::expected<std::vector<std::string>, Error> androidArguments(GameActivity& activity)
    {
        std::optional<JavaScope> scope;
        if (auto entered = enterJava(*activity.vm, scope); !entered)
        {
            return std::unexpected(entered.error());
        }
        auto&      env           = scope->env;
        const auto activityClass = env.GetObjectClass(activity.javaGameActivity);
        const auto getIntent     = env.GetMethodID(activityClass, "getIntent", "()Landroid/content/Intent;");
        if (auto checked = scope->check("Find GameActivity Intent accessor"); !checked)
        {
            return std::unexpected(checked.error());
        }
        const auto intent = env.CallObjectMethod(activity.javaGameActivity, getIntent);
        if (auto checked = scope->check("Read GameActivity Intent"); !checked)
        {
            return std::unexpected(checked.error());
        }
        const auto intentClass = env.GetObjectClass(intent);
        const auto getArguments =
            env.GetMethodID(intentClass, "getStringArrayExtra", "(Ljava/lang/String;)[Ljava/lang/String;");
        if (auto checked = scope->check("Find Intent argument accessor"); !checked)
        {
            return std::unexpected(checked.error());
        }
        const auto key       = env.NewStringUTF("vultra.arguments");
        const auto arguments = static_cast<jobjectArray>(env.CallObjectMethod(intent, getArguments, key));
        if (auto checked = scope->check("Read vultra.arguments from Intent"); !checked)
        {
            return std::unexpected(checked.error());
        }
        std::vector<std::string> result;
        if (!arguments)
        {
            return result;
        }
        const auto stringClass = env.FindClass("java/lang/String");
        const auto getBytes    = env.GetMethodID(stringClass, "getBytes", "(Ljava/lang/String;)[B");
        const auto encoding    = env.NewStringUTF("UTF-8");
        if (auto checked = scope->check("Find UTF-8 string encoder"); !checked)
        {
            return std::unexpected(checked.error());
        }
        const auto count = env.GetArrayLength(arguments);
        for (jsize index = 0; index < count; ++index)
        {
            const auto argument = env.GetObjectArrayElement(arguments, index);
            if (!argument)
            {
                return makeError("GameActivity argument array contains null");
            }
            const auto bytes = static_cast<jbyteArray>(env.CallObjectMethod(argument, getBytes, encoding));
            if (auto checked = scope->check("Encode GameActivity argument as UTF-8"); !checked)
            {
                return std::unexpected(checked.error());
            }
            const auto  size = env.GetArrayLength(bytes);
            std::string text(size_t(size), char(0));
            env.GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(text.data()));
            if (auto checked = scope->check("Copy GameActivity argument"); !checked)
            {
                return std::unexpected(checked.error());
            }
            result.push_back(std::move(text));
            env.DeleteLocalRef(bytes);
            env.DeleteLocalRef(argument);
        }
        return result;
    }
} // namespace vultra::platform
