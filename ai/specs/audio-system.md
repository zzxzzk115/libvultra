# Audio System (miniaudio + vaudio assets)

Status: v1 implemented (2026-06). Backend: **miniaudio** (`ma_engine`), dependency owned by the
**vasset** layer. Audio clips are first-class vasset assets cooked to the engine-internal
**vaudio** container.

## Data flow

```
source (.wav/.mp3/.flac/.ogg)
  └─ vasset VAudioImporter (ma_decoder; stb_vorbis for ogg)
       ├─ decode → PCM (optional resample / mono fold / normalize), or passthrough (keep encoded bytes)
       └─ writes imported/audio/<name> (VAUDIO container) + <source>.vimport + registry entry + import DB record
runtime
  └─ AssetSystem::loadAudioSync/Async(uuid|uri) → AssetHandle<vasset::VAudio, CpuAsset>
       └─ AudioSystem clip cache (holds the handle; bytes must outlive sounds)
            ├─ PCM        → ma_resource_manager_register_decoded_data
            └─ passthrough → ma_resource_manager_register_encoded_data (wav/mp3/flac runtime decode)
                 └─ ma_sound_init_from_file(registered name, MA_SOUND_FLAG_DECODE) → ma_engine mix
                      desktop: WASAPI/CoreAudio/ALSA · android: AAudio · wasm: Web Audio
```

## vasset layer (`external/vasset`)

- `VAssetType::eAudio` ("audio").
- `vasset/vaudio.hpp`: `VAudio { uuid, name, storage, sampleRate, channels, frameCount, duration,
  audioData, sourceFileName }`, `VAudioStorage { ePCM16, ePCMF32, ePassthroughWav/Mp3/Flac }`.
  Container: `VAUDIO` magic (16B) + `version=1` + `flags` + `rawSize` + payload (same
  writeRaw/writeString/writeBytes scheme as vanim/vskel). `saveAudio/loadAudio/loadAudioFromMemory`.
- `vasset/audio_import_params.hpp`: **subtype is keyed by source format** (`wav/mp3/flac/ogg`,
  auto-derived from the extension; `.vimport` `audio.subtype` can override). Per-subtype canonical
  defaults: wav/ogg → `pcm16`, mp3/flac → `passthrough`. Params (sparse `.vimport` persistence,
  only non-default values are written): `audio.storage`, `audio.target_sample_rate` (0 = keep,
  PCM only), `audio.force_mono`, `audio.normalize`, `audio.bitrate_kbps`, `audio.quality`.
  **bitrate/quality are schema-reserved for a future lossy encoder (Vorbis/Opus) and inactive in
  v1** — miniaudio has no encoder.
- `VAudioImporter` (`vasset_importers.cpp`): `isValidAudio` → folder-scan + single-file dispatch;
  skip-if-current via `importerVersion "audio:1"` / `outputSchema "vaudio:1"` / source+params hashes.
  Ogg never passes through (the runtime has no vorbis decoder): always decoded to PCM at import
  via `stb_vorbis` (impl TU `src/stb_vorbis_impl.cpp`), then converted with `ma_data_converter`.
- miniaudio package: `add_requires("miniaudio 0.11.25")` (xmake-repo, header-only),
  `add_packages("miniaudio", {public = true})` on the `vasset` target so libvultra inherits the
  header. Single implementation TU: `src/miniaudio_impl.cpp` (in the runtime lib, all platforms,
  because the engine needs `ma_engine` everywhere including wasm).

## Engine layer (libvultra)

- AssetSystem: `m_AudioCache` (CPU-only, like skeleton/animation), `loadAudioSync/Async`
  (uuid + uri overloads) on `IAssetService`; included in memory stats, shutdown clear, and
  zero-ref CPU release.
- `AudioSystem` (`function/audio/`): `EngineSubsystem + IAudioService`, pimpl (miniaudio.h never
  in public headers). **Registered after SceneSystem and before ScriptSystem** (ScriptSystem
  captures `IAudioService` into the Lua `ScriptContext` during its `onInit`).
  - `onInit`: provide service; `tryGet` assets/worlds (headless-safe); explicit
    `ma_resource_manager` (+ `ma_engine`). Device/engine init failure ⇒ **audio-less mode**
    (warn, every API safe no-op, `backendReady()==false`) — never fails engine boot.
  - `onUpdate`: recycles finished one-shots (`ma_sound_at_end`) and fade-out-stopped sounds.
  - `onPreRender` (after WorldSystem refreshed `TransformComponent::worldMatrix`):
    listener pose from the first `primary` `AudioListenerComponent` (forward = −Z column,
    up = +Y column); **when no listener component exists it falls back to the active
    `ICameraService` camera (`RenderCamera::inverseView`)** so spatial audio works against the
    default view without a manual listener entity; `AudioSourceComponent` reconcile
    (lazy spawn, dirty-check live edits,
    per-frame `ma_sound_set_position`, natural-finish writes `playing=false` back); sweep of
    entries whose entity/component vanished (**entity destroyed ⇒ sound stops**, covers scene
    reload).
  - Threading: miniaudio owns its mixer thread; **all `ma_*` calls are confined to the main
    engine thread** (sound setters are mixer-thread-safe).
  - Editor gating: `setPlaybackState(playing, paused)` — stop drops all instances (re-seeded on
    next play), pause stops/resumes currently-playing sounds.
- Components: `AudioSourceComponent { clip(CoreUUID), volume, pitch, loop, playOnStart, playing,
  spatial, minDistance, maxDistance, rolloff }`, `AudioListenerComponent { primary }` —
  entt::meta + ComponentRegistry registration (`scene_reflection.cpp`, `scene_system.cpp`).
- `IAudioService`: preload/unloadClip, playOneShot(+At, 3D), playMusic/stopMusic (single slot,
  fades via `ma_sound_set_fade_in_milliseconds` / `ma_sound_stop_with_fade_in_milliseconds`),
  handle ops (stop/pause/resume/setVolume/setPitch/setLooping/isPlaying), entity ops
  (play/pause/stop), master volume, `backendReady()`, `debugActiveSoundCount()`.
- Lua (`script_audio_binding.cpp`): `Audio` table — clip strings accept either a UUID or any
  `scheme://` uri. `playOneShot(clip[, vol, pitch])`, `playOneShotAt(clip, vec3[, vol, pitch])`,
  `playMusic(clip[, {volume,pitch,loop,fadeInMs}])`, `stopMusic([fadeMs])`,
  `stopSound/pauseSound/resumeSound/setVolume/setPitch/setLooping/isPlaying(id)`,
  `play/pause/stop(entity)`, `setMasterVolume/masterVolume/backendReady`.
- Editor: AudioSource/AudioListener in Add Component ("Audio" category) + generic meta field
  drawing; source-asset inspector gets an Audio Import section (subtype display, storage combo
  [passthrough disabled for ogg], sample rate / mono / normalize, reserved bitrate/quality
  greyed out) writing the `.vimport` and queueing reimport; play-mode gating wired in
  `editor_app.cpp::syncPlaybackState`. i18n keys added to en/zh-CN/ja/ko.

## WASM / WebGPU 注意点

Audio is orthogonal to WebGPU; on web it targets the **Web Audio API**:

- miniaudio's Emscripten backend uses ScriptProcessorNode by default (console deprecation
  warnings are expected). AudioWorklet would need the package `worklets=true` config plus
  `-sAUDIO_WORKLET=1 -sWASM_WORKERS=1`, which conflicts with the current no-pthread
  `-sASYNCIFY` link — documented future option.
- No pthreads ⇒ resource manager is initialized with `jobThreadCount=0` +
  `MA_RESOURCE_MANAGER_FLAG_NO_THREADING`; sounds are always created with `MA_SOUND_FLAG_DECODE`
  (fully decoded synchronously at creation), so no job processing is needed.
- **Gesture unlock**: browsers suspend the AudioContext until a user gesture. AudioSystem
  subscribes to window KeyDown/MouseButtonDown (GLFW/Emscripten dispatches these synchronously
  inside the DOM gesture stack) and calls `ma_engine_start` once; until then
  `backendReady()==false` and already-started sounds become audible after the resume.
- Import is offline/desktop (`VULTRA_HAS_VASSET_IMPORT`); wasm only reads cooked vaudio from the
  VPK (MEMFS). Example wasm targets must list their audio folders in `vpk.include_paths`.

## Verification

- `tests/audio` (`xmake b test-audio && xmake r test-audio`): generates a WAV into a temp asset
  root, runs the import scan headlessly, asserts cooked metadata, one-shot lifecycle/recycle,
  component spawn + destroyed-entity sweep, play-mode stop. Green without an audio device
  (audio-less branch).
- `examples/audio` (`xmake b example-audio && xmake r example-audio`): orbiting spatial source
  (coin.wav), click = one-shot, M/S = music fade toggle/stop. Wasm:
  `xmake f -p wasm --vultra_build_examples=y -y && xmake b example-audio`, serve the target dir,
  verify unlock-on-first-click.

## Future work

- Lossy re-encode at import (libvorbis/libopus) to activate `audio.bitrate_kbps`/`audio.quality`;
  runtime decoders to match.
- Streaming large BGM (`MA_SOUND_FLAG_STREAM` + custom `ma_vfs` over the engine VFS) instead of
  full decode-on-load.
- AudioWorklet output on wasm (needs pthread-enabled link).
- Async clip loading through the JobSystem; UUID-addressed audio asset picker UI for the
  AudioSource inspector (currently a raw UUID field via the generic meta drawer).
- Doppler/velocity, sound groups/buses, per-scene listener overrides.

Note: the listener falls back to the active camera when no `AudioListenerComponent` is present,
so a fresh scene with only a camera already has a working listener (add an `AudioListener`
component to override).
