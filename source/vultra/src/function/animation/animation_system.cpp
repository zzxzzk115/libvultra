#include "vultra/function/animation/animation_system.hpp"

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/animator_component.hpp"
#include "vultra/function/world/components/skin_palette_component.hpp"
#include "vultra/function/world/world.hpp"

#include <ozz/animation/runtime/animation.h>
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

        glm::mat4 toGlm(const ozz::math::Float4x4& m)
        {
            glm::mat4 out(1.0f);
            for (int i = 0; i < 4; ++i)
            {
                alignas(16) float values[4] {};
                _mm_storeu_ps(values, m.cols[i]);
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
        return true;
    }

    void AnimationSystem::onShutdown()
    {
        m_Animations.clear();
        m_Skeletons.clear();
        m_Assets = nullptr;
        m_Worlds = nullptr;
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

        if (m_PlaybackPaused && m_SingleStepRequests > 0u)
            --m_SingleStepRequests;

        updateWorld(m_Worlds->world(), dt);
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

            const auto* skeleton  = runtimeSkeleton(animator.skeleton);
            const auto* animation = runtimeAnimation(animator.animation);
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
            palette.skeleton = animator.skeleton;
            palette.matrices.resize(models.size());
            for (size_t i = 0; i < models.size(); ++i)
                palette.matrices[i] = toGlm(models[i]);
        }
    }
} // namespace vultra
