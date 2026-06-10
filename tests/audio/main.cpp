// Headless audio smoke test: generates a small WAV into a temp asset root, lets the
// AssetSystem import scan cook it to a vaudio asset, then exercises the AudioSystem
// (one-shot playback, component reconcile, entity-destroyed sweep). Must pass on
// machines without an audio device: in that case backendReady() is false and the
// service APIs degrade to safe no-ops, which is what gets asserted instead.
#include <vultra/core/engine/engine.hpp>
#include <vultra/core/timing/timing_system.hpp>
#include <vultra/function/asset/asset_system.hpp>
#include <vultra/function/audio/audio_system.hpp>
#include <vultra/function/jobs/job_system.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/audio_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/audio_listener_component.hpp>
#include <vultra/function/world/components/audio_source_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world_system.hpp>

#include <vasset/vasset_type.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <thread>
#include <vector>

namespace
{
    // 0.3 s 440 Hz sine, mono, 22050 Hz, s16 PCM.
    bool writeTestWav(const std::filesystem::path& path)
    {
        constexpr uint32_t sampleRate = 22050;
        constexpr uint32_t frameCount = sampleRate * 3 / 10;
        constexpr uint16_t channels   = 1;
        constexpr uint16_t bitsPerSample = 16;

        std::vector<int16_t> samples(frameCount);
        for (uint32_t i = 0; i < frameCount; ++i)
        {
            const double t = static_cast<double>(i) / sampleRate;
            samples[i] = static_cast<int16_t>(0.5 * 32767.0 * std::sin(2.0 * std::numbers::pi * 440.0 * t));
        }

        const uint32_t dataSize   = frameCount * channels * (bitsPerSample / 8);
        const uint32_t byteRate   = sampleRate * channels * (bitsPerSample / 8);
        const uint16_t blockAlign = channels * (bitsPerSample / 8);
        const uint32_t riffSize   = 36 + dataSize;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary);
        if (!file)
            return false;

        const auto writeRaw = [&](const void* data, size_t size) {
            file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        };
        const auto writeU32 = [&](uint32_t v) { writeRaw(&v, sizeof(v)); };
        const auto writeU16 = [&](uint16_t v) { writeRaw(&v, sizeof(v)); };

        writeRaw("RIFF", 4);
        writeU32(riffSize);
        writeRaw("WAVE", 4);
        writeRaw("fmt ", 4);
        writeU32(16);
        writeU16(1); // PCM
        writeU16(channels);
        writeU32(sampleRate);
        writeU32(byteRate);
        writeU16(blockAlign);
        writeU16(bitsPerSample);
        writeRaw("data", 4);
        writeU32(dataSize);
        writeRaw(samples.data(), dataSize);
        return static_cast<bool>(file);
    }

    int fail(vultra::Engine& engine, int code, const char* message)
    {
        std::fprintf(stderr, "%s\n", message);
        engine.shutdownCore();
        return code;
    }
} // namespace

int main()
{
    const auto assetRoot =
        std::filesystem::temp_directory_path() / "vultra-audio-test" / std::to_string(static_cast<uint32_t>(::time(nullptr)));
    if (!writeTestWav(assetRoot / "sounds" / "test_tone.wav"))
    {
        std::fprintf(stderr, "failed to write test wav\n");
        return 1;
    }

    vultra::Engine engine;
    engine.emplaceSubsystem<vultra::TimingSystem>();
    engine.emplaceSubsystem<vultra::JobSystem>();
    engine.emplaceSubsystem<vultra::WorldSystem>();
    engine.emplaceSubsystem<vultra::AssetSystem>();
    engine.emplaceSubsystem<vultra::AudioSystem>();

    if (!engine.initCore())
    {
        std::fprintf(stderr, "engine init failed\n");
        return 2;
    }

    auto& assets = engine.ctx().services.require<vultra::IAssetService>();
    assets.configure(vultra::AssetSystemDesc {
        .assetRoot        = assetRoot.generic_string(),
        .enableImportScan = true,
        .asyncLoading     = false,
    });

    // The import scan must have cooked the wav into an audio registry entry.
    vultra::CoreUUID clipUuid {};
    for (const auto& [key, entry] : assets.registry().getRegistry())
    {
        if (entry.type == vasset::VAssetType::eAudio)
        {
            vbase::UUID parsed {};
            if (vbase::try_parse_uuid(key.c_str(), parsed))
                clipUuid = vultra::CoreUUID {parsed};
            break;
        }
    }
    if (!clipUuid.valid())
        return fail(engine, 3, "import scan produced no audio registry entry");

    auto handle = assets.loadAudioSync(clipUuid);
    if (!handle.ready() || !handle.cpu())
        return fail(engine, 4, "loadAudioSync failed for cooked clip");
    const auto* cpu = handle.cpu();
    if (cpu->frameCount == 0 || cpu->sampleRate != 22050 || cpu->channels != 1 || cpu->audioData.empty())
        return fail(engine, 5, "cooked vaudio metadata mismatch");

    auto& audio = engine.ctx().services.require<vultra::IAudioService>();

    if (!audio.backendReady())
    {
        // No audio device (CI): verify the audio-less mode stays safe and bail green.
        if (audio.playOneShot(clipUuid) != vultra::kInvalidSoundId)
            return fail(engine, 6, "audio-less mode returned a live sound id");
        std::printf("audio backend unavailable; cooked-asset path verified, playback skipped\n");
        engine.shutdownCore();
        return 0;
    }

    // --- one-shot lifecycle ---
    const auto oneShot = audio.playOneShot(clipUuid);
    if (oneShot == vultra::kInvalidSoundId)
        return fail(engine, 7, "playOneShot failed");
    if (!audio.isPlaying(oneShot))
        return fail(engine, 8, "one-shot not playing after start");
    // The mixer consumes the 0.3 s clip in real time, so wall-clock time has to pass
    // between ticks (tickFrame's dt is simulation time only). Allow up to ~5 s.
    for (int i = 0; i < 100 && audio.debugActiveSoundCount() != 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        engine.tickFrame(vultra::fsec {0.05f});
    }
    if (audio.isPlaying(oneShot) || audio.debugActiveSoundCount() != 0)
        return fail(engine, 9, "one-shot was not recycled after finishing");

    // --- component lifecycle + destroyed-entity sweep ---
    auto& world = engine.ctx().services.require<vultra::IWorldService>().world();
    auto& reg   = world.registry();

    auto listener = world.createEntity();
    reg.get_or_emplace<vultra::TransformComponent>(listener).dirty = true;
    reg.emplace<vultra::AudioListenerComponent>(listener);

    auto source = world.createEntity();
    auto& sourceTransform = reg.get_or_emplace<vultra::TransformComponent>(source);
    sourceTransform.position = {2.0f, 0.0f, 0.0f};
    sourceTransform.dirty    = true;
    reg.emplace<vultra::AudioSourceComponent>(source,
                                              vultra::AudioSourceComponent {
                                                  .clip = clipUuid,
                                                  .loop = true,
                                              });

    engine.tickFrame(vultra::fsec {1.0f / 60.0f});
    engine.tickFrame(vultra::fsec {1.0f / 60.0f});
    if (audio.debugActiveSoundCount() != 1)
        return fail(engine, 10, "component source did not spawn a sound");

    world.destroyEntity(source);
    engine.tickFrame(vultra::fsec {1.0f / 60.0f});
    if (audio.debugActiveSoundCount() != 0)
        return fail(engine, 11, "destroyed entity did not release its sound");

    // --- editor play-mode gating ---
    auto source2 = world.createEntity();
    reg.get_or_emplace<vultra::TransformComponent>(source2).dirty = true;
    reg.emplace<vultra::AudioSourceComponent>(source2,
                                              vultra::AudioSourceComponent {
                                                  .clip = clipUuid,
                                                  .loop = true,
                                              });
    engine.tickFrame(vultra::fsec {1.0f / 60.0f});
    if (audio.debugActiveSoundCount() != 1)
        return fail(engine, 12, "second component source did not spawn");
    audio.setPlaybackState(false, false);
    if (audio.debugActiveSoundCount() != 0)
        return fail(engine, 13, "playback stop did not release sounds");

    std::printf("audio smoke test passed\n");
    engine.shutdownCore();
    return 0;
}
