#include "vultra/function/animation/animation_system.hpp"

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/services/timing_service.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/animator_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/skin_palette_component.hpp"
#include "vultra/function/world/world.hpp"

#include <vasset/vmesh.hpp>

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/containers/vector.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vultra
{
    namespace
    {
        class OzzReadStream final : public ozz::io::Stream
        {
        public:
            explicit OzzReadStream(const std::vector<std::byte>& bytes) : m_Bytes(bytes) {}

            bool opened() const override { return true; }
            size_t Read(void* buffer, size_t size) override
            {
                const size_t available = m_Offset < m_Bytes.size() ? m_Bytes.size() - m_Offset : 0u;
                const size_t count     = std::min(size, available);
                if (count > 0)
                {
                    std::memcpy(buffer, m_Bytes.data() + m_Offset, count);
                    m_Offset += count;
                }
                return count;
            }
            size_t Write(const void*, size_t) override { return 0u; }
            int Seek(int offset, Origin origin) override
            {
                size_t base = 0u;
                if (origin == kCurrent)
                    base = m_Offset;
                else if (origin == kEnd)
                    base = m_Bytes.size();
                const int64_t next = static_cast<int64_t>(base) + static_cast<int64_t>(offset);
                if (next < 0 || static_cast<size_t>(next) > m_Bytes.size())
                    return -1;
                m_Offset = static_cast<size_t>(next);
                return 0;
            }
            int Tell() const override { return static_cast<int>(m_Offset); }
            size_t Size() const override { return m_Bytes.size(); }

        private:
            const std::vector<std::byte>& m_Bytes;
            size_t                        m_Offset {0};
        };

        template<class T>
        std::unique_ptr<T> deserializeOzz(const std::vector<std::byte>& bytes)
        {
            if (bytes.empty())
                return nullptr;
            auto object = std::make_unique<T>();
            OzzReadStream stream(bytes);
            ozz::io::IArchive archive(&stream);
            archive >> *object;
            return object;
        }

        // The skeleton an animator should use: the explicit component skeleton if set, otherwise
        // the skeleton bundled in the entity's (skinned) mesh asset. This lets gameplay/editors
        // leave the skeleton empty and have it default to the mesh's own skeleton.
        CoreUUID resolveSkeletonFor(IAssetService*  assets,
                                    World&          world,
                                    entt::entity    entity,
                                    const CoreUUID& explicitSkeleton)
        {
            if (explicitSkeleton.valid())
                return explicitSkeleton;
            if (!assets)
                return {};
            auto& reg = world.registry();
            // The skinned mesh may live on the entity itself or a descendant (imported model roots
            // keep the mesh on a child), so search the subtree for a mesh that bundles a skeleton.
            const auto search = [&](auto&& self, entt::entity cursor) -> CoreUUID {
                if (cursor == entt::null || !reg.valid(cursor))
                    return {};
                if (const auto* mesh = reg.try_get<MeshComponent>(cursor); mesh && mesh->mesh.valid())
                {
                    auto        handle = assets->loadMeshSync(mesh->mesh);
                    const auto* cpu    = handle.cpu();
                    if (cpu && cpu->hasSkin && CoreUUID {cpu->skeleton}.valid())
                        return CoreUUID {cpu->skeleton};
                }
                for (auto child = world.firstChild(cursor); child != entt::null; child = world.nextSibling(child))
                    if (auto found = self(self, child); found.valid())
                        return found;
                return {};
            };
            return search(search, entity);
        }

        glm::mat4 toGlm(const ozz::math::Float4x4& m)
        {
            glm::mat4 out(1.0f);
            for (int i = 0; i < 4; ++i)
            {
                alignas(16) float values[4] {};
                // Use ozz's portable store rather than a raw SSE intrinsic: ozz's SimdFloat4 is
                // __m128 on x86 but a scalar struct under its reference/SIMD backends (e.g. wasm).
                ozz::math::StorePtrU(m.cols[i], values);
                out[i] = glm::vec4(values[0], values[1], values[2], values[3]);
            }
            return out;
        }
    } // namespace

    bool AnimationSystem::onInit()
    {
        ctx().services.provide<IAnimationService>(this);
        m_Assets = &ctx().services.require<IAssetService>();
        m_Worlds = &ctx().services.require<IWorldService>();
        m_Timing = ctx().services.tryGet<ITimingService>();
        return true;
    }

    void AnimationSystem::onShutdown()
    {
        m_Animations.clear();
        m_Skeletons.clear();
        m_Controllers.clear();
        m_Assets = nullptr;
        m_Worlds = nullptr;
        m_Timing = nullptr;
        m_PlaybackPlaying = true;
        m_PlaybackPaused = false;
        m_SingleStepRequests = 0u;
    }

    void AnimationSystem::setPlaybackState(bool playing, bool paused)
    {
        if (!playing)
            paused = false;

        m_PlaybackPlaying = playing;
        m_PlaybackPaused = paused;
        if (!m_PlaybackPlaying)
            m_SingleStepRequests = 0u;
    }

    void AnimationSystem::requestSingleStep()
    {
        if (!m_PlaybackPlaying)
            return;
        m_PlaybackPaused = true;
        ++m_SingleStepRequests;
    }

    bool AnimationSystem::play(entt::entity entity, bool restart)
    {
        if (!m_Worlds)
            return false;
        auto& reg = m_Worlds->world().registry();
        auto* animator = reg.try_get<AnimatorComponent>(entity);
        if (!animator)
            return false;
        if (restart)
            animator->time = 0.0f;
        animator->playing = true;
        animator->playOnStart = false;
        return true;
    }

    bool AnimationSystem::pause(entt::entity entity)
    {
        if (!m_Worlds)
            return false;
        auto* animator = m_Worlds->world().registry().try_get<AnimatorComponent>(entity);
        if (!animator)
            return false;
        animator->playing = false;
        animator->playOnStart = false;
        return true;
    }

    bool AnimationSystem::stop(entt::entity entity)
    {
        if (!m_Worlds)
            return false;
        auto* animator = m_Worlds->world().registry().try_get<AnimatorComponent>(entity);
        if (!animator)
            return false;
        animator->playing = false;
        animator->playOnStart = false;
        animator->time = 0.0f;
        return true;
    }

    bool AnimationSystem::setAnimation(entt::entity entity, const CoreUUID& animation, bool restart)
    {
        if (!m_Worlds)
            return false;
        auto* animator = m_Worlds->world().registry().try_get<AnimatorComponent>(entity);
        if (!animator)
            return false;
        animator->animation = animation;
        if (restart)
            animator->time = 0.0f;
        return true;
    }

    bool AnimationSystem::setTime(entt::entity entity, float seconds)
    {
        if (!m_Worlds)
            return false;
        auto* animator = m_Worlds->world().registry().try_get<AnimatorComponent>(entity);
        if (!animator)
            return false;
        const float duration = animationDuration(animator->animation);
        animator->time = duration > 0.0f ? std::clamp(seconds, 0.0f, duration) : std::max(seconds, 0.0f);
        return true;
    }

    bool AnimationSystem::setNormalizedTime(entt::entity entity, float normalizedTime)
    {
        if (!m_Worlds)
            return false;
        auto* animator = m_Worlds->world().registry().try_get<AnimatorComponent>(entity);
        if (!animator)
            return false;
        const float duration = animationDuration(animator->animation);
        animator->time = duration * std::clamp(normalizedTime, 0.0f, 1.0f);
        return duration > 0.0f;
    }

    bool AnimationSystem::setSpeed(entt::entity entity, float speed)
    {
        if (!m_Worlds)
            return false;
        auto* animator = m_Worlds->world().registry().try_get<AnimatorComponent>(entity);
        if (!animator)
            return false;
        animator->speed = speed;
        return true;
    }

    bool AnimationSystem::setLoop(entt::entity entity, bool loop)
    {
        if (!m_Worlds)
            return false;
        auto* animator = m_Worlds->world().registry().try_get<AnimatorComponent>(entity);
        if (!animator)
            return false;
        animator->loop = loop;
        return true;
    }

    AnimatorPlaybackState AnimationSystem::playbackState(entt::entity entity)
    {
        AnimatorPlaybackState out;
        if (!m_Worlds)
            return out;
        auto* animator = m_Worlds->world().registry().try_get<AnimatorComponent>(entity);
        if (!animator)
            return out;

        out.valid = true;
        out.playing = animator->playing;
        out.loop = animator->loop;
        out.speed = animator->speed;
        out.time = animator->time;
        out.duration = animationDuration(animator->animation);
        out.normalizedTime = out.duration > 0.0f ? std::clamp(animator->time / out.duration, 0.0f, 1.0f) : 0.0f;
        out.skeleton = animator->skeleton;
        out.animation = animator->animation;
        return out;
    }

    uint32_t AnimationSystem::jointCount(const CoreUUID& skeleton)
    {
        const auto* runtime = runtimeSkeleton(skeleton);
        return runtime ? static_cast<uint32_t>(runtime->num_joints()) : 0u;
    }

    float AnimationSystem::animationDuration(const CoreUUID& animation)
    {
        const auto* runtime = runtimeAnimation(animation);
        return runtime ? runtime->duration() : 0.0f;
    }

    const ozz::animation::Skeleton* AnimationSystem::runtimeSkeleton(const CoreUUID& uuid)
    {
        if (!uuid.valid() || !m_Assets)
            return nullptr;

        auto& entry = m_Skeletons[uuid];
        if (entry.skeleton)
            return entry.skeleton.get();

        entry.handle = m_Assets->loadSkeletonSync(uuid);
        if (!entry.handle.ready() || !entry.handle.cpu())
            return nullptr;

        entry.skeleton = deserializeOzz<ozz::animation::Skeleton>(entry.handle.cpu()->ozzData);
        if (!entry.skeleton)
            VULTRA_CORE_WARN("[AnimationSystem] Failed to deserialize skeleton {}", uuid.toString());
        return entry.skeleton.get();
    }

    const ozz::animation::Animation* AnimationSystem::runtimeAnimation(const CoreUUID& uuid)
    {
        if (!uuid.valid() || !m_Assets)
            return nullptr;

        auto& entry = m_Animations[uuid];
        if (entry.animation)
            return entry.animation.get();

        entry.handle = m_Assets->loadAnimationSync(uuid);
        if (!entry.handle.ready() || !entry.handle.cpu())
            return nullptr;

        entry.animation = deserializeOzz<ozz::animation::Animation>(entry.handle.cpu()->ozzData);
        if (!entry.animation)
            VULTRA_CORE_WARN("[AnimationSystem] Failed to deserialize animation {}", uuid.toString());
        return entry.animation.get();
    }

    void AnimationSystem::onUpdate(fsec dt)
    {
        if (!m_Worlds)
            return;

        if (!m_PlaybackPlaying)
            return;
        if (m_PlaybackPaused && m_SingleStepRequests == 0u)
            return;

        // During a single-step (paused + step requested) the real frame delta is tiny/erratic
        // - advance by the fixed timestep instead so headless stepping is deterministic.
        fsec stepDt = dt;
        if (m_PlaybackPaused && m_SingleStepRequests > 0u)
        {
            --m_SingleStepRequests;
            stepDt = fsec {m_Timing ? m_Timing->fixedDeltaTime() : 1.0f / 60.0f};
        }

        updateWorld(m_Worlds->world(), stepDt);
    }

    void AnimationSystem::updateWorld(World& world, fsec dt)
    {
        auto& reg  = world.registry();
        auto  view = reg.view<AnimatorComponent>();
        for (auto entity : view)
        {
            auto& animator = view.get<AnimatorComponent>(entity);
            if (!animator.playing && !animator.playOnStart)
                continue;
            if (animator.playOnStart)
            {
                animator.playing = true;
                animator.playOnStart = false;
            }

            // Graph mode runs the animator state machine; single-clip mode plays one clip.
            if (animator.mode == 1u)
            {
                updateGraphAnimator(world, entity, animator, dt);
                continue;
            }

            const auto  skeletonUuid = resolveSkeletonFor(m_Assets, world, entity, animator.skeleton);
            const auto* skeleton     = runtimeSkeleton(skeletonUuid);
            const auto* animation    = runtimeAnimation(animator.animation);
            if (!skeleton || !animation)
                continue;

            const float duration = std::max(animation->duration(), 0.0001f);
            if (animator.playing)
            {
                animator.time += dt.count() * animator.speed;
                if (animator.loop)
                {
                    animator.time = std::fmod(animator.time, duration);
                    if (animator.time < 0.0f)
                        animator.time += duration;
                }
                else
                {
                    animator.time = std::clamp(animator.time, 0.0f, duration);
                    if (animator.time >= duration)
                        animator.playing = false;
                }
            }

            const float ratio = std::clamp(animator.time / duration, 0.0f, 1.0f);

            ozz::animation::SamplingJob::Context context;
            context.Resize(animation->num_tracks());
            ozz::vector<ozz::math::SoaTransform> locals(skeleton->num_soa_joints());
            ozz::animation::SamplingJob sampling;
            sampling.animation = animation;
            sampling.context   = &context;
            sampling.ratio     = ratio;
            sampling.output    = ozz::make_span(locals);
            if (!sampling.Run())
                continue;

            ozz::vector<ozz::math::Float4x4> models(skeleton->num_joints());
            ozz::animation::LocalToModelJob localToModel;
            localToModel.skeleton = skeleton;
            localToModel.input    = ozz::make_span(locals);
            localToModel.output   = ozz::make_span(models);
            if (!localToModel.Run())
                continue;

            auto& palette = reg.get_or_emplace<SkinPaletteComponent>(entity);
            palette.skeleton = skeletonUuid;
            palette.matrices.resize(models.size());
            for (size_t i = 0; i < models.size(); ++i)
                palette.matrices[i] = toGlm(models[i]);
        }
    }

    AnimationSystem::ControllerRuntime* AnimationSystem::ensureController(entt::entity entity)
    {
        if (!m_Worlds)
            return nullptr;
        auto* cc = m_Worlds->world().registry().try_get<AnimatorComponent>(entity);
        if (!cc || cc->mode != 1u)
        {
            m_Controllers.erase(entity);
            return nullptr;
        }

        auto& rt = m_Controllers[entity];
        if (rt.loaded && rt.graphUri == cc->graph)
            return &rt;

        // (Re)load the graph from its text asset.
        rt = ControllerRuntime {};
        rt.graphUri = cc->graph;
        rt.loaded   = true;
        if (m_Assets && !cc->graph.empty())
        {
            auto text = m_Assets->loadTextAssetSync(cc->graph);
            if (text)
            {
                std::vector<std::string> diagnostics;
                if (auto graph = animator_graph::loadGraphFromText(text.value(), &diagnostics))
                    rt.graph = std::move(*graph);
                else
                    VULTRA_CORE_WARN("[AnimationSystem] Failed to parse animator graph {}", cc->graph);
            }
            else
            {
                // Surface the read failure instead of silently leaving an empty graph (which
                // would just make the entity stand still - e.g. an unpacked graph in a VPK build).
                VULTRA_CORE_WARN("[AnimationSystem] Failed to read animator graph '{}': {}", cc->graph,
                                 text.error());
            }
        }

        // Seed parameters from defaults and enter the initial state.
        for (const auto& p : rt.graph.parameters)
        {
            if (p.type == animator_graph::ParameterType::eBool)
                rt.bools[p.name] = p.defaultBool;
            else
                rt.floats[p.name] = p.defaultFloat;
        }
        rt.currentState = rt.graph.entry.empty() ? (rt.graph.states.empty() ? -1 : 0) :
                                                   rt.graph.stateIndex(rt.graph.entry);
        if (rt.currentState < 0 && !rt.graph.states.empty())
            rt.currentState = 0;
        return &rt;
    }

    bool AnimationSystem::conditionMet(ControllerRuntime& rt, const animator_graph::Condition& c) const
    {
        using CT = animator_graph::ConditionType;
        switch (c.type)
        {
            case CT::eGreater:
                return rt.floats.count(c.parameter) ? rt.floats.at(c.parameter) > c.threshold : false;
            case CT::eLess:
                return rt.floats.count(c.parameter) ? rt.floats.at(c.parameter) < c.threshold : false;
            case CT::eEqual:
                return rt.floats.count(c.parameter) ? std::fabs(rt.floats.at(c.parameter) - c.threshold) < 1e-4f : false;
            case CT::eNotEqual:
                return rt.floats.count(c.parameter) ? std::fabs(rt.floats.at(c.parameter) - c.threshold) >= 1e-4f : false;
            case CT::eTrue:
                return rt.bools.count(c.parameter) && rt.bools.at(c.parameter);
            case CT::eFalse:
                return rt.bools.count(c.parameter) ? !rt.bools.at(c.parameter) : true;
            case CT::eTrigger:
                return rt.triggers.count(c.parameter) > 0;
        }
        return false;
    }

    bool AnimationSystem::transitionReady(ControllerRuntime& rt,
                                          const animator_graph::Transition& t,
                                          float                             currentNormalized) const
    {
        if (t.hasExitTime && currentNormalized < t.exitTime)
            return false;
        for (const auto& c : t.conditions)
            if (!conditionMet(rt, c))
                return false;
        return true;
    }

    void AnimationSystem::updateGraphAnimator(World& world, entt::entity entity, AnimatorComponent& cc, fsec dt)
    {
        auto& reg = world.registry();
        {
            auto* rt = ensureController(entity);
            if (!rt || rt->graph.states.empty() || rt->currentState < 0)
                return;

            const float dtScaled = std::max(dt.count(), 0.0f) * cc.speed;

            const auto advance = [&](int stateIndex, float& time) {
                const auto& st       = rt->graph.states[static_cast<size_t>(stateIndex)];
                const float duration = std::max(animationDuration(st.animation), 0.0f);
                time += dtScaled * st.speed;
                if (duration > 0.0f)
                {
                    if (st.loop)
                    {
                        time = std::fmod(time, duration);
                        if (time < 0.0f)
                            time += duration;
                    }
                    else
                        time = std::clamp(time, 0.0f, duration);
                }
                return duration;
            };

            const float curDuration = advance(rt->currentState, rt->currentTime);
            const float curNorm     = curDuration > 0.0f ? std::clamp(rt->currentTime / curDuration, 0.0f, 1.0f) : 0.0f;

            if (rt->targetState >= 0)
            {
                advance(rt->targetState, rt->targetTime);
                rt->transitionTime += dtScaled;
                if (rt->transitionDuration <= 0.0f || rt->transitionTime >= rt->transitionDuration)
                {
                    rt->currentState       = rt->targetState;
                    rt->currentTime        = rt->targetTime;
                    rt->targetState        = -1;
                    rt->transitionTime     = 0.0f;
                    rt->transitionDuration = 0.0f;
                }
            }
            else
            {
                const animator_graph::Transition* chosen = nullptr;
                int                               chosenIndex = -1;
                const auto tryList = [&](const std::vector<animator_graph::Transition>& list) {
                    if (chosen)
                        return;
                    for (const auto& t : list)
                    {
                        const int ti = rt->graph.stateIndex(t.to);
                        if (ti < 0 || ti == rt->currentState)
                            continue;
                        if (transitionReady(*rt, t, curNorm))
                        {
                            chosen      = &t;
                            chosenIndex = ti;
                            break;
                        }
                    }
                };
                tryList(rt->graph.anyTransitions);
                tryList(rt->graph.states[static_cast<size_t>(rt->currentState)].transitions);

                if (chosen)
                {
                    for (const auto& c : chosen->conditions)
                        if (c.type == animator_graph::ConditionType::eTrigger)
                            rt->triggers.erase(c.parameter);

                    if (chosen->duration <= 0.0f)
                    {
                        rt->currentState = chosenIndex;
                        rt->currentTime  = 0.0f;
                    }
                    else
                    {
                        rt->targetState        = chosenIndex;
                        rt->targetTime         = 0.0f;
                        rt->transitionTime     = 0.0f;
                        rt->transitionDuration = chosen->duration;
                    }
                }
            }

            // Sample + blend into the skin palette (requires a loaded skeleton + clips).
            const auto  skeletonUuid = resolveSkeletonFor(m_Assets, world, entity, cc.skeleton);
            const auto* skeleton     = runtimeSkeleton(skeletonUuid);
            if (!skeleton)
                return;

            const auto sampleInto = [&](int stateIndex, float time, ozz::vector<ozz::math::SoaTransform>& out) -> bool {
                const auto* animation = runtimeAnimation(rt->graph.states[static_cast<size_t>(stateIndex)].animation);
                if (!animation)
                    return false;
                const float duration = std::max(animation->duration(), 0.0001f);
                ozz::animation::SamplingJob::Context context;
                context.Resize(animation->num_tracks());
                out.resize(skeleton->num_soa_joints());
                ozz::animation::SamplingJob sampling;
                sampling.animation = animation;
                sampling.context   = &context;
                sampling.ratio     = std::clamp(time / duration, 0.0f, 1.0f);
                sampling.output    = ozz::make_span(out);
                return sampling.Run();
            };

            ozz::vector<ozz::math::SoaTransform> localsA;
            if (!sampleInto(rt->currentState, rt->currentTime, localsA))
                return;

            ozz::vector<ozz::math::SoaTransform> blended;
            const ozz::vector<ozz::math::SoaTransform>* locals = &localsA;
            ozz::vector<ozz::math::SoaTransform> localsB;
            if (rt->targetState >= 0 && rt->transitionDuration > 0.0f &&
                sampleInto(rt->targetState, rt->targetTime, localsB))
            {
                const float w = std::clamp(rt->transitionTime / rt->transitionDuration, 0.0f, 1.0f);
                ozz::animation::BlendingJob::Layer layers[2];
                layers[0].transform = ozz::make_span(localsA);
                layers[0].weight    = 1.0f - w;
                layers[1].transform = ozz::make_span(localsB);
                layers[1].weight    = w;
                blended.resize(skeleton->num_soa_joints());
                ozz::animation::BlendingJob blend;
                blend.layers    = ozz::make_span(layers);
                blend.rest_pose = skeleton->joint_rest_poses();
                blend.output    = ozz::make_span(blended);
                if (blend.Run())
                    locals = &blended;
            }

            ozz::vector<ozz::math::Float4x4> models(skeleton->num_joints());
            ozz::animation::LocalToModelJob localToModel;
            localToModel.skeleton = skeleton;
            localToModel.input    = ozz::make_span(*locals);
            localToModel.output   = ozz::make_span(models);
            if (!localToModel.Run())
                return;

            auto& palette    = reg.get_or_emplace<SkinPaletteComponent>(entity);
            palette.skeleton = skeletonUuid;
            palette.matrices.resize(models.size());
            for (size_t i = 0; i < models.size(); ++i)
                palette.matrices[i] = toGlm(models[i]);
        }
    }

    bool AnimationSystem::setFloat(entt::entity entity, const std::string& name, float value)
    {
        auto* rt = ensureController(entity);
        if (!rt)
            return false;
        rt->floats[name] = value;
        return true;
    }

    bool AnimationSystem::setBool(entt::entity entity, const std::string& name, bool value)
    {
        auto* rt = ensureController(entity);
        if (!rt)
            return false;
        rt->bools[name] = value;
        return true;
    }

    bool AnimationSystem::setTrigger(entt::entity entity, const std::string& name)
    {
        auto* rt = ensureController(entity);
        if (!rt)
            return false;
        rt->triggers.insert(name);
        return true;
    }

    float AnimationSystem::getFloat(entt::entity entity, const std::string& name)
    {
        auto* rt = ensureController(entity);
        if (!rt)
            return 0.0f;
        const auto it = rt->floats.find(name);
        return it != rt->floats.end() ? it->second : 0.0f;
    }

    bool AnimationSystem::getBool(entt::entity entity, const std::string& name)
    {
        auto* rt = ensureController(entity);
        if (!rt)
            return false;
        const auto it = rt->bools.find(name);
        return it != rt->bools.end() ? it->second : false;
    }

    AnimatorControllerState AnimationSystem::controllerState(entt::entity entity)
    {
        AnimatorControllerState out;
        auto* rt = ensureController(entity);
        if (!rt || rt->currentState < 0 || rt->currentState >= static_cast<int>(rt->graph.states.size()))
            return out;
        out.valid        = true;
        out.currentState = rt->graph.states[static_cast<size_t>(rt->currentState)].name;
        out.transitioning = rt->targetState >= 0;
        if (out.transitioning)
        {
            out.nextState          = rt->graph.states[static_cast<size_t>(rt->targetState)].name;
            out.transitionProgress = rt->transitionDuration > 0.0f ?
                                         std::clamp(rt->transitionTime / rt->transitionDuration, 0.0f, 1.0f) :
                                         1.0f;
        }
        const float duration = std::max(animationDuration(rt->graph.states[static_cast<size_t>(rt->currentState)].animation), 0.0f);
        out.normalizedTime   = duration > 0.0f ? std::clamp(rt->currentTime / duration, 0.0f, 1.0f) : 0.0f;
        return out;
    }
} // namespace vultra
