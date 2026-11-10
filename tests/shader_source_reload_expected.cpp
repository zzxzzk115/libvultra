#include <atomic>
#include <cerrno>
#include <memory>
#include <pthread.h>

namespace
{
    int                  threadCalls = 0;
    int                  rejectCall  = 0;
    std::atomic_uint32_t activeThreads {0};

    struct ThreadEntry
    {
        void* (*function)(void*);
        void* context;
    };

    void* enterTestThread(void* value)
    {
        std::unique_ptr<ThreadEntry> entry(static_cast<ThreadEntry*>(value));
        auto*                        result = entry->function(entry->context);
        --activeThreads;
        return result;
    }

    int createTestThread(pthread_t* thread, const pthread_attr_t* attributes, void* (*function)(void*), void* context)
    {
        if (++threadCalls == rejectCall)
        {
            return EAGAIN;
        }
        auto entry = std::make_unique<ThreadEntry>(ThreadEntry {function, context});
        ++activeThreads;
        const auto status = pthread_create(thread, attributes, enterTestThread, entry.get());
        if (status != 0)
        {
            --activeThreads;
            return status;
        }
        entry.release();
        return 0;
    }
} // namespace

// Isolate the private owner and its vendored watches at their actual native creation boundary.
#define pthread_create createTestThread
#include "../source/servers/src/rendering/shader_source_reload.cpp"
#undef pthread_create

#include <vultra/platform/os/file.hpp>

#include <cstdio>
#include <thread>

namespace
{
    constexpr std::string_view kSource = R"(Shader "Tests/Background"
{
    SubShader { Pass { Name "Compute" Compute main SLANGPROGRAM
static const float externalValue = 1;
RWStructuredBuffer<float> output;
[numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { output[id.x] = externalValue; }
ENDSLANG
    } }
})";

    std::expected<void, std::string> probe()
    {
        using namespace vultra;
        using Reload            = detail::ShaderSourceReload;
        const auto      root    = std::filesystem::absolute(std::filesystem::path("build/.tmp/source-reload-expected") /
                                                    StableId::generate().toString());
        const auto      file    = root / "authored/background.vshader";
        const auto      include = root / "external/input.inc";
        std::error_code directoryError;
        std::filesystem::create_directories(file.parent_path(), directoryError);
        if (directoryError)
        {
            return std::unexpected("Create authored fixture directory: " + directoryError.message());
        }
        std::filesystem::create_directories(include.parent_path(), directoryError);
        if (directoryError)
        {
            return std::unexpected("Create external fixture directory: " + directoryError.message());
        }
        if (auto written = writeFileAtomicallyExpected(file, std::as_bytes(std::span(kSource))); !written)
        {
            return written;
        }
        for (uint32_t round = 0; round < 64; ++round)
        {
            threadCalls = 0;
            rejectCall  = 1;
            Reload::Job failed;
            failed.file = file;
            auto result = failed.startExpected();
            if (result || result.error().find("Start shader compiler thread:") == std::string::npos || failed.done ||
                activeThreads != 0)
            {
                return std::unexpected("Native compiler thread failure was accepted or leaked a worker");
            }
        }
        rejectCall = 0;
        Reload::Job compiled;
        compiled.file = file;
        if (auto launched = compiled.startExpected(); !launched)
        {
            return launched;
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!compiled.done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        compiled.worker.reset();
        if (!compiled.candidate || activeThreads != 0)
        {
            return std::unexpected(compiled.candidate ? "Compiler worker did not join" : compiled.candidate.error());
        }
        const auto& asset = *compiled.candidate;
        for (int failure : {1, 2})
        {
            for (uint32_t round = 0; round < 32; ++round)
            {
                threadCalls = 0;
                rejectCall  = failure;
                std::atomic<uint64_t> revision {0};
                Reload                reload(file, {}, revision);
                auto                  plan = reload.prepareAcceptedExpected(asset);
                if (plan || plan.error().find("Create watch thread:") == std::string::npos || activeThreads != 0)
                {
                    return std::unexpected("Native runtime watch failure leaked callbacks or accepted a partial plan");
                }
            }
        }
        rejectCall = 0;
        {
            Reload::Job invalid;
            invalid.file = root / "missing.vshader";
            if (auto launched = invalid.startExpected(); !launched)
            {
                return launched;
            }
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!invalid.done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            invalid.worker.reset();
            if (invalid.candidate || invalid.candidate.error().find("missing.vshader") == std::string::npos)
            {
                return std::unexpected("Background compiler did not return its source error");
            }
        }
        {
            Reload::Job pending;
            pending.file = file;
            if (auto launched = pending.startExpected(); !launched)
            {
                return launched;
            }
            // Destruction joins even without polling the result.
        }
        if (activeThreads != 0)
        {
            return std::unexpected("Compiler owner teardown returned with a live worker");
        }
        {
            std::atomic<uint64_t> revision {0};
            Reload                reload(file, {}, revision);
            auto                  plan = reload.prepareAcceptedExpected(asset);
            if (!plan)
            {
                return std::unexpected(plan.error());
            }
            reload.accept(std::move(*plan));
            constexpr std::string_view firstInclude = "static const float externalValue = 1;\n";
            if (auto written = writeFileAtomicallyExpected(include, std::as_bytes(std::span(firstInclude))); !written)
            {
                return written;
            }
            std::string       candidate(kSource);
            const std::string declaration = "static const float externalValue = 1;";
            candidate.replace(candidate.find(declaration), declaration.size(), "#include \"../external/input.inc\"");
            if (auto written = writeFileAtomicallyExpected(file, std::as_bytes(std::span(candidate))); !written)
            {
                return written;
            }
            auto rejected = ShaderAsset::compileExpected(file);
            if (!rejected)
            {
                return std::unexpected(rejected.error());
            }
            const auto oldCode =
                rejected->subshaders.front().passes.front().programs.begin()->second.shaders.front().words;
            if (auto observed = reload.observeCandidateExpected(*rejected); !observed)
            {
                return observed;
            }
            bool     accepting  = false;
            uint32_t rejections = 0;
            auto     publish    = [&](ShaderAsset value, uint64_t) -> std::expected<bool, std::string>
            {
                if (auto observed = reload.observeCandidateExpected(value); !observed)
                {
                    return std::unexpected(observed.error());
                }
                if (!accepting)
                {
                    ++rejections;
                    return std::unexpected("injected unpublished candidate");
                }
                if (value.subshaders.front().passes.front().programs.begin()->second.shaders.front().words == oldCode)
                {
                    return std::unexpected("External dependency repair did not change compiled code");
                }
                auto accepted = reload.prepareAcceptedExpected(value);
                if (!accepted)
                {
                    return std::unexpected(accepted.error());
                }
                reload.accept(std::move(*accepted));
                return true;
            };
            std::string diagnostics;
            deadline              = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            auto     quietSince   = std::chrono::steady_clock::now();
            uint64_t lastRevision = revision.load();
            while (std::chrono::steady_clock::now() < deadline)
            {
                auto polled = reload.pollExpected(publish, diagnostics);
                if (!polled && polled.error() != "injected unpublished candidate")
                {
                    return std::unexpected(polled.error());
                }
                const auto now = std::chrono::steady_clock::now();
                if (reload.job || revision.load() != lastRevision)
                {
                    lastRevision = revision.load();
                    quietSince   = now;
                }
                if (rejections && !reload.job && now - quietSince >= std::chrono::seconds(1))
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!rejections || reload.job)
            {
                return std::unexpected("Candidate rejection did not settle before the dependency-only repair");
            }
            accepting                           = true;
            constexpr std::string_view repaired = "static const float externalValue = 2;\n";
            if (auto written = writeFileAtomicallyExpected(include, std::as_bytes(std::span(repaired))); !written)
            {
                return written;
            }
            deadline       = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            bool published = false;
            while (!published && std::chrono::steady_clock::now() < deadline)
            {
                auto polled = reload.pollExpected(publish, diagnostics);
                if (!polled)
                {
                    return std::unexpected(polled.error());
                }
                published = *polled;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!published)
            {
                return std::unexpected("Rejected candidate's external .inc repair did not trigger a new compile");
            }
        }
        if (activeThreads != 0)
        {
            return std::unexpected("Source reload teardown retained worker/watch callbacks");
        }
        std::puts("Expected source reload: 64 compiler/64 watch native-start failures, background errors, owner join "
                  "and rejected external .inc dependency-only repair passed");
        return {};
    }
} // namespace

int main()
{
    auto result = probe();
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
