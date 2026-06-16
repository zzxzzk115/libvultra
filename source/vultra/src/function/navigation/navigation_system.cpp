#include "vultra/function/navigation/navigation_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/function/debug_draw/debug_draw_interface.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/physics_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/character_controller_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/nav_agent_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <vasset/vmesh.hpp>

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_map>

namespace vultra
{
    namespace
    {
        // Solo-mesh build tuning. Cell size/height drive navmesh resolution; the agent
        // dimensions are the defaults used for the bake (per-agent radius/height refine
        // queries, but the navmesh itself is baked once for this profile).
        constexpr float kCellSize             = 0.3f;
        constexpr float kCellHeight           = 0.2f;
        constexpr float kAgentHeight          = 2.0f;
        constexpr float kAgentRadius          = 0.4f;
        constexpr float kAgentMaxClimb        = 0.4f;
        constexpr float kAgentMaxSlopeDegrees = 45.0f;
        constexpr float kEdgeMaxLen           = 12.0f;
        constexpr float kEdgeMaxError         = 1.3f;
        constexpr float kRegionMinSize        = 8.0f;
        constexpr float kRegionMergeSize      = 20.0f;
        constexpr float kDetailSampleDist     = 6.0f;
        constexpr float kDetailSampleMaxError = 1.0f;
        constexpr int   kMaxPathPolys         = 256;
        constexpr int   kMaxPathPoints        = 256;
    } // namespace

    struct NavigationSystem::Impl
    {
        dtNavMesh*      navMesh {nullptr};
        dtNavMeshQuery* navQuery {nullptr};
        // Detail-mesh edges captured at bake time for debug visualization.
        std::vector<glm::vec3>                              debugEdges;
        std::unordered_map<entt::entity, std::vector<glm::vec3>> agentPaths;

        ~Impl() { release(); }

        void release()
        {
            if (navQuery)
            {
                dtFreeNavMeshQuery(navQuery);
                navQuery = nullptr;
            }
            if (navMesh)
            {
                dtFreeNavMesh(navMesh);
                navMesh = nullptr;
            }
            debugEdges.clear();
            agentPaths.clear();
        }
    };

    NavigationSystem::NavigationSystem()  = default;
    NavigationSystem::~NavigationSystem() = default;

    bool NavigationSystem::onInit()
    {
        VULTRA_CORE_INFO("[NavigationSystem] Initializing...");
        m_WorldService   = ctx().services.tryGet<IWorldService>();
        m_AssetService   = ctx().services.tryGet<IAssetService>();
        m_PhysicsService = ctx().services.tryGet<IPhysicsService>();
        m_Impl           = std::make_unique<Impl>();
        ctx().services.provide<INavigationService>(this);
        VULTRA_CORE_INFO("[NavigationSystem] Initialized");
        return true;
    }

    void NavigationSystem::onShutdown()
    {
        if (m_Impl)
            m_Impl->release();
        m_Impl.reset();
    }

    void NavigationSystem::onUpdate(fsec dt)
    {
        if (!m_Impl || !m_Impl->navQuery)
            return;
        steerAgents(dt.count());
        if (m_DebugDraw)
            drawDebug();
    }

    bool NavigationSystem::isBaked() const { return m_Impl && m_Impl->navQuery != nullptr; }

    void NavigationSystem::setDebugDrawEnabled(bool enabled) { m_DebugDraw = enabled; }

    bool NavigationSystem::collectWorldGeometry(std::vector<float>& outVerts, std::vector<int>& outIndices) const
    {
        if (!m_WorldService || !m_AssetService)
            return false;

        auto& reg  = m_WorldService->world().registry();
        auto  view = reg.view<MeshComponent, TransformComponent>();
        for (auto entity : view)
        {
            const auto& mesh = view.get<MeshComponent>(entity);
            // Builtin primitives have no CPU asset to read; only imported meshes are baked.
            if (mesh.builtinGeometry != std::numeric_limits<uint32_t>::max() || !mesh.mesh.valid())
                continue;

            auto                 handle = m_AssetService->loadMeshSync(mesh.mesh);
            const vasset::VMesh* cpu    = handle.cpu();
            if (!cpu || cpu->positions.empty() || cpu->indices.size() < 3)
                continue;

            const auto&     transform   = view.get<TransformComponent>(entity);
            const glm::mat4 worldMatrix = transform.worldMatrix;
            const int       base        = static_cast<int>(outVerts.size() / 3);
            for (const auto& p : cpu->positions)
            {
                const glm::vec3 world = glm::vec3(worldMatrix * glm::vec4(p, 1.0f));
                outVerts.push_back(world.x);
                outVerts.push_back(world.y);
                outVerts.push_back(world.z);
            }
            for (size_t i = 0; i + 2 < cpu->indices.size(); i += 3)
            {
                outIndices.push_back(base + static_cast<int>(cpu->indices[i]));
                outIndices.push_back(base + static_cast<int>(cpu->indices[i + 1]));
                outIndices.push_back(base + static_cast<int>(cpu->indices[i + 2]));
            }
        }
        return outVerts.size() >= 9 && outIndices.size() >= 3;
    }

    bool NavigationSystem::bake()
    {
        if (!m_Impl)
            return false;
        // Services may initialize after navigation; resolve lazily.
        if (!m_WorldService)
            m_WorldService = ctx().services.tryGet<IWorldService>();
        if (!m_AssetService)
            m_AssetService = ctx().services.tryGet<IAssetService>();
        m_Impl->release();

        std::vector<float> verts;
        std::vector<int>   tris;
        if (!collectWorldGeometry(verts, tris))
        {
            VULTRA_CORE_WARN("[NavigationSystem] No bakeable geometry found");
            return false;
        }
        const int nverts = static_cast<int>(verts.size() / 3);
        const int ntris  = static_cast<int>(tris.size() / 3);

        rcContext rcCtx(false);

        rcConfig cfg {};
        cfg.cs                     = kCellSize;
        cfg.ch                     = kCellHeight;
        cfg.walkableSlopeAngle     = kAgentMaxSlopeDegrees;
        cfg.walkableHeight         = static_cast<int>(std::ceil(kAgentHeight / cfg.ch));
        cfg.walkableClimb          = static_cast<int>(std::floor(kAgentMaxClimb / cfg.ch));
        cfg.walkableRadius         = static_cast<int>(std::ceil(kAgentRadius / cfg.cs));
        cfg.maxEdgeLen             = static_cast<int>(kEdgeMaxLen / cfg.cs);
        cfg.maxSimplificationError = kEdgeMaxError;
        cfg.minRegionArea          = static_cast<int>(rcSqr(kRegionMinSize));
        cfg.mergeRegionArea        = static_cast<int>(rcSqr(kRegionMergeSize));
        cfg.maxVertsPerPoly        = 6;
        cfg.detailSampleDist       = kDetailSampleDist < 0.9f ? 0.0f : cfg.cs * kDetailSampleDist;
        cfg.detailSampleMaxError   = cfg.ch * kDetailSampleMaxError;

        rcCalcBounds(verts.data(), nverts, cfg.bmin, cfg.bmax);
        rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

        bool             ok    = false;
        rcHeightfield*   solid = nullptr;
        rcCompactHeightfield* chf = nullptr;
        rcContourSet*    cset  = nullptr;
        rcPolyMesh*      pmesh = nullptr;
        rcPolyMeshDetail* dmesh = nullptr;
        unsigned char*   triareas = nullptr;

        do
        {
            solid = rcAllocHeightfield();
            if (!solid || !rcCreateHeightfield(&rcCtx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
                break;

            triareas = new (std::nothrow) unsigned char[ntris];
            if (!triareas)
                break;
            std::memset(triareas, 0, static_cast<size_t>(ntris));
            rcMarkWalkableTriangles(&rcCtx, cfg.walkableSlopeAngle, verts.data(), nverts, tris.data(), ntris, triareas);
            if (!rcRasterizeTriangles(
                    &rcCtx, verts.data(), nverts, tris.data(), triareas, ntris, *solid, cfg.walkableClimb))
                break;

            rcFilterLowHangingWalkableObstacles(&rcCtx, cfg.walkableClimb, *solid);
            rcFilterLedgeSpans(&rcCtx, cfg.walkableHeight, cfg.walkableClimb, *solid);
            rcFilterWalkableLowHeightSpans(&rcCtx, cfg.walkableHeight, *solid);

            chf = rcAllocCompactHeightfield();
            if (!chf || !rcBuildCompactHeightfield(&rcCtx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf))
                break;
            if (!rcErodeWalkableArea(&rcCtx, cfg.walkableRadius, *chf))
                break;
            if (!rcBuildDistanceField(&rcCtx, *chf))
                break;
            if (!rcBuildRegions(&rcCtx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
                break;

            cset = rcAllocContourSet();
            if (!cset || !rcBuildContours(&rcCtx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
                break;

            pmesh = rcAllocPolyMesh();
            if (!pmesh || !rcBuildPolyMesh(&rcCtx, *cset, cfg.maxVertsPerPoly, *pmesh))
                break;

            dmesh = rcAllocPolyMeshDetail();
            if (!dmesh ||
                !rcBuildPolyMeshDetail(&rcCtx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh))
                break;

            // Mark every walkable poly as traversable (single area/flag).
            for (int i = 0; i < pmesh->npolys; ++i)
            {
                if (pmesh->areas[i] == RC_WALKABLE_AREA)
                    pmesh->flags[i] = 1;
            }

            dtNavMeshCreateParams params {};
            params.verts            = pmesh->verts;
            params.vertCount        = pmesh->nverts;
            params.polys            = pmesh->polys;
            params.polyAreas        = pmesh->areas;
            params.polyFlags        = pmesh->flags;
            params.polyCount        = pmesh->npolys;
            params.nvp              = pmesh->nvp;
            params.detailMeshes     = dmesh->meshes;
            params.detailVerts      = dmesh->verts;
            params.detailVertsCount = dmesh->nverts;
            params.detailTris       = dmesh->tris;
            params.detailTriCount   = dmesh->ntris;
            params.walkableHeight   = kAgentHeight;
            params.walkableRadius   = kAgentRadius;
            params.walkableClimb    = kAgentMaxClimb;
            rcVcopy(params.bmin, pmesh->bmin);
            rcVcopy(params.bmax, pmesh->bmax);
            params.cs          = cfg.cs;
            params.ch          = cfg.ch;
            params.buildBvTree = true;

            unsigned char* navData     = nullptr;
            int            navDataSize = 0;
            if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
                break;

            m_Impl->navMesh = dtAllocNavMesh();
            if (!m_Impl->navMesh || dtStatusFailed(m_Impl->navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA)))
            {
                dtFree(navData);
                break;
            }
            m_Impl->navQuery = dtAllocNavMeshQuery();
            if (!m_Impl->navQuery || dtStatusFailed(m_Impl->navQuery->init(m_Impl->navMesh, 2048)))
                break;

            // Capture detail-mesh edges for debug draw.
            m_Impl->debugEdges.clear();
            for (int i = 0; i < dmesh->nmeshes; ++i)
            {
                const unsigned int* m       = &dmesh->meshes[i * 4];
                const unsigned int  bverts  = m[0];
                const unsigned int  btris   = m[2];
                const unsigned int  ntrisLocal = m[3];
                const float*        dverts  = &dmesh->verts[bverts * 3];
                const unsigned char* dtris  = &dmesh->tris[btris * 4];
                for (unsigned int t = 0; t < ntrisLocal; ++t)
                {
                    const unsigned char* tri = &dtris[t * 4];
                    glm::vec3            p[3];
                    for (int k = 0; k < 3; ++k)
                        p[k] = glm::vec3(dverts[tri[k] * 3 + 0], dverts[tri[k] * 3 + 1], dverts[tri[k] * 3 + 2]);
                    for (int k = 0; k < 3; ++k)
                    {
                        m_Impl->debugEdges.push_back(p[k]);
                        m_Impl->debugEdges.push_back(p[(k + 1) % 3]);
                    }
                }
            }

            ok = true;
        } while (false);

        delete[] triareas;
        if (solid)
            rcFreeHeightField(solid);
        if (chf)
            rcFreeCompactHeightfield(chf);
        if (cset)
            rcFreeContourSet(cset);
        if (pmesh)
            rcFreePolyMesh(pmesh);
        if (dmesh)
            rcFreePolyMeshDetail(dmesh);

        if (!ok)
        {
            m_Impl->release();
            VULTRA_CORE_WARN("[NavigationSystem] Navmesh bake failed");
            return false;
        }
        VULTRA_CORE_INFO("[NavigationSystem] Baked navmesh from {} triangles", ntris);
        return true;
    }

    std::vector<glm::vec3> NavigationSystem::findPath(const glm::vec3& start, const glm::vec3& end) const
    {
        std::vector<glm::vec3> out;
        if (!m_Impl || !m_Impl->navQuery)
            return out;

        dtQueryFilter filter;
        const float   extents[3] = {2.0f, 4.0f, 2.0f};
        const float   startPos[3] = {start.x, start.y, start.z};
        const float   endPos[3]   = {end.x, end.y, end.z};

        dtPolyRef startRef = 0;
        dtPolyRef endRef   = 0;
        float     startNearest[3];
        float     endNearest[3];
        m_Impl->navQuery->findNearestPoly(startPos, extents, &filter, &startRef, startNearest);
        m_Impl->navQuery->findNearestPoly(endPos, extents, &filter, &endRef, endNearest);
        if (!startRef || !endRef)
            return out;

        dtPolyRef polys[kMaxPathPolys];
        int       npolys = 0;
        if (dtStatusFailed(m_Impl->navQuery->findPath(
                startRef, endRef, startNearest, endNearest, &filter, polys, &npolys, kMaxPathPolys)) ||
            npolys == 0)
            return out;

        float         straight[kMaxPathPoints * 3];
        unsigned char straightFlags[kMaxPathPoints];
        dtPolyRef     straightRefs[kMaxPathPoints];
        int           nstraight = 0;
        m_Impl->navQuery->findStraightPath(
            startNearest, endNearest, polys, npolys, straight, straightFlags, straightRefs, &nstraight, kMaxPathPoints);

        out.reserve(static_cast<size_t>(nstraight));
        for (int i = 0; i < nstraight; ++i)
            out.emplace_back(straight[i * 3 + 0], straight[i * 3 + 1], straight[i * 3 + 2]);
        return out;
    }

    glm::vec3 NavigationSystem::nearestPoint(const glm::vec3& point) const
    {
        if (!m_Impl || !m_Impl->navQuery)
            return point;
        dtQueryFilter filter;
        const float   extents[3] = {2.0f, 4.0f, 2.0f};
        const float   pos[3]     = {point.x, point.y, point.z};
        dtPolyRef     ref        = 0;
        float         nearest[3];
        m_Impl->navQuery->findNearestPoly(pos, extents, &filter, &ref, nearest);
        if (!ref)
            return point;
        return {nearest[0], nearest[1], nearest[2]};
    }

    bool NavigationSystem::setAgentDestination(entt::entity entity, const glm::vec3& target)
    {
        if (!m_WorldService)
            return false;
        auto& reg   = m_WorldService->world().registry();
        auto* agent = reg.try_get<NavAgentComponent>(entity);
        if (!agent)
            return false;
        agent->targetPosition = target;
        agent->hasTarget      = true;
        return true;
    }

    void NavigationSystem::stopAgent(entt::entity entity)
    {
        if (!m_WorldService)
            return;
        auto& reg   = m_WorldService->world().registry();
        auto* agent = reg.try_get<NavAgentComponent>(entity);
        if (!agent)
            return;
        agent->hasTarget = false;
        agent->moving    = false;
        if (m_Impl)
            m_Impl->agentPaths.erase(entity);
    }

    bool NavigationSystem::agentHasPath(entt::entity entity) const
    {
        if (!m_WorldService)
            return false;
        auto& reg   = m_WorldService->world().registry();
        auto* agent = reg.try_get<NavAgentComponent>(entity);
        return agent && agent->moving;
    }

    void NavigationSystem::steerAgents(float dt)
    {
        if (!m_PhysicsService)
            m_PhysicsService = ctx().services.tryGet<IPhysicsService>();
        if (!m_WorldService || dt <= 0.0f)
            return;
        auto& reg  = m_WorldService->world().registry();
        auto  view = reg.view<NavAgentComponent, TransformComponent>();
        for (auto entity : view)
        {
            auto& agent     = view.get<NavAgentComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            if (!agent.hasTarget)
            {
                agent.moving = false;
                continue;
            }

            const glm::vec3 pos      = transform.position;
            const float     planarSq = (agent.targetPosition.x - pos.x) * (agent.targetPosition.x - pos.x) +
                                   (agent.targetPosition.z - pos.z) * (agent.targetPosition.z - pos.z);
            if (planarSq <= agent.stoppingDistance * agent.stoppingDistance)
            {
                agent.hasTarget = false;
                agent.moving    = false;
                if (m_PhysicsService && reg.all_of<CharacterControllerComponent>(entity))
                    m_PhysicsService->characterMove(entity, glm::vec3 {0.0f});
                if (m_Impl)
                    m_Impl->agentPaths.erase(entity);
                continue;
            }

            const auto path = findPath(pos, agent.targetPosition);
            if (m_Impl)
                m_Impl->agentPaths[entity] = path;
            if (path.size() < 2)
            {
                agent.moving = false;
                continue;
            }

            // Steer toward the next waypoint (skip the first point, which is our position).
            const glm::vec3 next = path[1];
            glm::vec3       dir  = next - pos;
            dir.y                = 0.0f;
            const float len      = glm::length(dir);
            if (len < 1.0e-4f)
            {
                agent.moving = false;
                continue;
            }
            dir /= len;
            agent.moving = true;

            const glm::vec3 velocity = dir * agent.speed;
            if (m_PhysicsService && reg.all_of<CharacterControllerComponent>(entity))
            {
                m_PhysicsService->characterMove(entity, velocity);
            }
            else
            {
                transform.position += velocity * dt;
                transform.dirty = true;
            }
        }
    }

    void NavigationSystem::drawDebug() const
    {
        if (!m_Impl)
            return;
        const float navColor[3]  = {0.2f, 0.6f, 1.0f};
        const float pathColor[3] = {1.0f, 0.85f, 0.1f};
        for (size_t i = 0; i + 1 < m_Impl->debugEdges.size(); i += 2)
            dd::line(glm::value_ptr(m_Impl->debugEdges[i]), glm::value_ptr(m_Impl->debugEdges[i + 1]), navColor);
        for (const auto& [entity, path] : m_Impl->agentPaths)
        {
            (void)entity;
            for (size_t i = 0; i + 1 < path.size(); ++i)
                dd::line(glm::value_ptr(path[i]), glm::value_ptr(path[i + 1]), pathColor);
        }
    }
} // namespace vultra
