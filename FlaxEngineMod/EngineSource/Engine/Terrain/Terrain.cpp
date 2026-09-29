// Copyright (c) Wojciech Figat. All rights reserved.

#include "Terrain.h"
#include "TerrainPatch.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Math/Ray.h"

#include "Engine/Core/Collections/Dictionary.h"
#include "Engine/Core/Math/Int2.h"
#include "Engine/Core/Collections/HashSet.h"
#include "Engine/Level/Scene/SceneRendering.h"
#include "Engine/Serialization/Serialization.h"
#include "Engine/Physics/Physics.h"
#include "Engine/Physics/PhysicsScene.h"
#include "Engine/Physics/PhysicalMaterial.h"
#include "Engine/Physics/PhysicsBackend.h"
#include "Engine/Content/Deprecated.h"
#include "Engine/Graphics/RenderView.h"
#include "Engine/Graphics/RenderTask.h"
#include "Engine/Graphics/Textures/GPUTexture.h"
#include "Engine/Level/Scene/Scene.h"
#include "Engine/Profiler/ProfilerCPU.h"
#include "Engine/Profiler/ProfilerMemory.h"
#include "Engine/Renderer/GlobalSignDistanceFieldPass.h"
#include "Engine/Renderer/GI/GlobalSurfaceAtlasPass.h"

Terrain::Terrain(const SpawnParams& params)
    : PhysicsColliderActor(params)
    , _lodBias(0)
    , _forcedLod(-1)
    , _collisionLod(-1)
    , _lodCount(0)
    , _chunkSize(0)
    , _scaleInLightmap(0.1f)
    , _lodDistribution(0.6f)
    , _boundsExtent(Vector3::Zero)
    , _cachedScale(1.0f)
{
    _drawCategory = SceneRendering::SceneDrawAsync;
    _physicalMaterials.Resize(8);
}

Terrain::~Terrain()
{
    // Cleanup
    _patches.ClearDelete();
}



void Terrain::UpdateBounds()
{
    PROFILE_CPU();
    _box = BoundingBox(_transform.Translation);
    for (int32 i = 0; i < _patches.Count(); i++)
    {
        auto patch = _patches[i];
        patch->UpdateBounds();
        BoundingBox::Merge(_box, patch->_bounds, _box);
    }
    BoundingSphere::FromBox(_box, _sphere);
    if (_sceneRenderingKey != -1)
        GetSceneRendering()->UpdateActor(this, _sceneRenderingKey, ISceneRenderingListener::Bounds);
}

void Terrain::CacheNeighbors()
{
    PROFILE_CPU();
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        for (int32 chunkIndex = 0; chunkIndex < Terrain::ChunksCount; chunkIndex++)
        {
            patch->Chunks[chunkIndex].CacheNeighbors();
        }
    }
}

void Terrain::UpdateLayerBits()
{
    if (_patches.IsEmpty())
        return;

    // Own layer ID
    const uint32 mask0 = GetLayerMask();

    // Own layer mask
    const uint32 mask1 = Physics::LayerMasks[GetLayer()];

    // Update the shapes layer bits
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        if (patch->HasCollision())
        {
            PhysicsBackend::SetShapeFilterMask(patch->_physicsShape, mask0, mask1);
        }
    }
}

void Terrain::RemoveLightmap()
{
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        patch->RemoveLightmap();
    }
}

bool Terrain::RayCast(const Vector3& origin, const Vector3& direction, float& resultHitDistance, float maxDistance) const
{
    float minDistance = MAX_float;
    bool result = false;
    const Ray ray(origin, direction);

    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        if (patch->HasCollision() &&
            patch->_bounds.Intersects(ray) &&
            patch->RayCast(origin, direction, resultHitDistance, maxDistance) &&
            resultHitDistance < minDistance)
        {
            minDistance = resultHitDistance;
            result = true;
        }
    }

    resultHitDistance = minDistance;
    return result;
}

bool Terrain::RayCast(const Vector3& origin, const Vector3& direction, float& resultHitDistance, TerrainChunk*& resultChunk, float maxDistance) const
{
    float minDistance = MAX_float;
    TerrainChunk* minChunk = nullptr;
    bool result = false;
    const Ray ray(origin, direction);

    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        if (patch->HasCollision() &&
            patch->_bounds.Intersects(ray) &&
            patch->RayCast(origin, direction, resultHitDistance, resultChunk, maxDistance) &&
            resultHitDistance < minDistance)
        {
            minDistance = resultHitDistance;
            minChunk = resultChunk;
            result = true;
        }
    }

    resultHitDistance = minDistance;
    resultChunk = minChunk;
    return result;
}

bool Terrain::RayCast(const Ray& ray, float& resultHitDistance, Int2& resultPatchCoord, Int2& resultChunkCoord, float maxDistance) const
{
    TerrainChunk* resultChunk;
    if (RayCast(ray.Position, ray.Direction, resultHitDistance, resultChunk, maxDistance))
    {
        resultPatchCoord.X = resultChunk->GetPatch()->GetX();
        resultPatchCoord.Y = resultChunk->GetPatch()->GetZ();
        resultChunkCoord.X = resultChunk->GetX();
        resultChunkCoord.Y = resultChunk->GetZ();
        return true;
    }

    resultPatchCoord = Int2::Zero;
    resultChunkCoord = Int2::Zero;
    return false;
}

bool Terrain::RayCast(const Vector3& origin, const Vector3& direction, RayCastHit& hitInfo, float maxDistance) const
{
    float minDistance = MAX_float;
    bool result = false;
    RayCastHit tmpHit;
    const Ray ray(origin, direction);
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        if (patch->HasCollision() &&
            patch->_bounds.Intersects(ray) &&
            patch->RayCast(origin, direction, tmpHit, maxDistance) &&
            tmpHit.Distance < minDistance)
        {
            minDistance = tmpHit.Distance;
            hitInfo = tmpHit;
            result = true;
        }
    }
    return result;
}

void Terrain::ClosestPoint(const Vector3& point, Vector3& result) const
{
    Real minDistance = MAX_Real;
    Vector3 tmp;
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        if (patch->HasCollision())
        {
            patch->ClosestPoint(point, tmp);
            const auto distance = Vector3::DistanceSquared(point, tmp);
            if (distance < minDistance)
            {
                minDistance = distance;
                result = tmp;
            }
        }
    }
}

bool Terrain::ContainsPoint(const Vector3& point) const
{
    return false;
}




void Terrain::DrawPatch(const RenderContext& renderContext, const Int2& patchCoord, MaterialBase* material, int32 lodIndex) const
{
    auto patch = GetPatch(patchCoord);
    if (patch)
    {
        for (int32 i = 0; i < Terrain::ChunksCount; i++)
            patch->Chunks[i].Draw(renderContext, material, lodIndex);
    }
}

void Terrain::DrawChunk(const RenderContext& renderContext, const Int2& patchCoord, const Int2& chunkCoord, MaterialBase* material, int32 lodIndex) const
{
    auto patch = GetPatch(patchCoord);
    if (patch)
    {
        const auto chunk = patch->GetChunk(chunkCoord);
        if (chunk)
        {
            chunk->Draw(renderContext, material, lodIndex);
        }
    }
}

#if TERRAIN_USE_PHYSICS_DEBUG

void Terrain::DrawPhysicsDebug(RenderView& view)
{
    PROFILE_CPU();
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        _patches[pathIndex]->DrawPhysicsDebug(view);
    }
}

#endif

void Terrain::SetLODDistribution(float value)
{
    _lodDistribution = value;
}

void Terrain::SetScaleInLightmap(float value)
{
    _scaleInLightmap = value;
}

void Terrain::SetBoundsExtent(const Vector3& value)
{
    if (_boundsExtent == value)
        return;

    _boundsExtent = value;
    UpdateBounds();
}

void Terrain::SetCollisionLOD(int32 value)
{
    value = Math::Clamp(value, -1, TERRAIN_MAX_LODS);
    if (value == _collisionLod)
        return;

    _collisionLod = value;

#if !BUILD_RELEASE
    for (int32 i = 0; i < _patches.Count(); i++)
    {
        const auto patch = _patches[i];
        if (patch->HasCollision())
        {
            LOG(Warning, "Changing Terrain CollisionLOD has no effect for patches that have already collision created. Patch {0}x{1} won't be updated.", patch->_x, patch->_z);
        }
    }
#endif
}

void Terrain::SetPhysicalMaterials(const Array<JsonAssetReference<PhysicalMaterial>, FixedAllocation<8>>& value)
{
    PROFILE_MEM(LevelTerrain);
    _physicalMaterials = value;
    _physicalMaterials.Resize(8);
    JsonAsset* materials[8];
    for (int32 i = 0; i < 8; i++)
        materials[i] = _physicalMaterials[i];
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches.Get()[pathIndex];
        if (patch->HasCollision())
            PhysicsBackend::SetShapeMaterials(patch->_physicsShape, ToSpan(materials, 8));
    }
}

int32 Terrain::GetHeightmapSize() const
{
    return GetChunkSize() * ChunksCountEdge + 1;
}

float Terrain::GetPatchSize() const
{
    return TERRAIN_UNITS_PER_VERTEX * ChunksCountEdge * GetChunkSize();
}

TerrainPatch* Terrain::GetPatch(const Int2& patchCoord) const
{
    return GetPatch(patchCoord.X, patchCoord.Y);
}

TerrainPatch* Terrain::GetPatch(int32 x, int32 z) const
{
    TerrainPatch* result = nullptr;
    for (int32 i = 0; i < _patches.Count(); i++)
    {
        const auto patch = _patches[i];
        if (patch->_x == x && patch->_z == z)
        {
            result = patch;
            break;
        }
    }
    return result;
}

int32 Terrain::GetPatchIndex(const Int2& patchCoord) const
{
    for (int32 i = 0; i < _patches.Count(); i++)
    {
        const auto patch = _patches[i];
        if (patch->_x == patchCoord.X && patch->_z == patchCoord.Y)
            return i;
    }
    return -1;
}

void Terrain::GetPatchCoord(int32 patchIndex, Int2& patchCoord) const
{
    const auto patch = GetPatch(patchIndex);
    if (patch)
    {
        patchCoord.X = patch->GetX();
        patchCoord.Y = patch->GetZ();
    }
}

void Terrain::GetPatchBounds(int32 patchIndex, BoundingBox& bounds) const
{
    const auto patch = GetPatch(patchIndex);
    if (patch)
    {
        bounds = patch->GetBounds();
    }
}

void Terrain::GetChunkBounds(int32 patchIndex, int32 chunkIndex, BoundingBox& bounds) const
{
    const auto patch = GetPatch(patchIndex);
    if (patch)
    {
        const auto chunk = patch->GetChunk(chunkIndex);
        if (chunk)
        {
            bounds = chunk->GetBounds();
        }
    }
}

MaterialBase* Terrain::GetChunkOverrideMaterial(const Int2& patchCoord, const Int2& chunkCoord) const
{
    auto patch = GetPatch(patchCoord);
    if (patch)
    {
        const auto chunk = patch->GetChunk(chunkCoord);
        if (chunk)
        {
            return chunk->OverrideMaterial;
        }
    }
    return nullptr;
}

void Terrain::SetChunkOverrideMaterial(const Int2& patchCoord, const Int2& chunkCoord, MaterialBase* value)
{
    auto patch = GetPatch(patchCoord);
    if (patch)
    {
        const auto chunk = patch->GetChunk(chunkCoord);
        if (chunk)
        {
            chunk->OverrideMaterial = value;
        }
    }
}

#if TERRAIN_EDITING

bool Terrain::SetupPatchHeightMap(const Int2& patchCoord, int32 heightMapLength, const float* heightMap, const byte* holesMask, bool forceUseVirtualStorage)
{
    auto patch = GetPatch(patchCoord);
    if (patch)
    {
        return patch->SetupHeightMap(heightMapLength, heightMap, holesMask, forceUseVirtualStorage);
    }
    return true;
}

bool Terrain::SetupPatchSplatMap(const Int2& patchCoord, int32 index, int32 splatMapLength, const Color32* splatMap, bool forceUseVirtualStorage)
{
    auto patch = GetPatch(patchCoord);
    if (patch)
    {
        return patch->SetupSplatMap(index, splatMapLength, splatMap, forceUseVirtualStorage);
    }
    return true;
}

#endif

#if TERRAIN_EDITING

void Terrain::Setup(int32 lodCount, int32 chunkSize)
{
    LOG(Info, "Terrain setup for {0} LODs ({1} chunk edge quads)", lodCount, chunkSize);

    _patches.ClearDelete();

    _lodCount = lodCount;
    _chunkSize = chunkSize;
}

void Terrain::AddPatches(const Int2& numberOfPatches)
{
    PROFILE_MEM(LevelTerrain);
    if (_chunkSize == 0)
        Setup();
    _patches.ClearDelete();
    _patches.EnsureCapacity(numberOfPatches.X * numberOfPatches.Y);

    for (int32 z = 0; z < numberOfPatches.Y; z++)
    {
        for (int32 x = 0; x < numberOfPatches.X; x++)
        {
            auto patch = ::New<TerrainPatch>();
            patch->Init(this, x, z);
            _patches.Add(patch);
        }
    }

    CacheNeighbors();

    if (IsDuringPlay())
    {
        for (int32 i = 0; i < _patches.Count(); i++)
        {
            auto patch = _patches[i];
            patch->UpdateTransform();
            patch->CreateCollision();
        }
        UpdateLayerBits();
    }

    UpdateBounds();
}

void Terrain::AddPatch(const Int2& patchCoord)
{
    auto patch = GetPatch(patchCoord);
    if (patch != nullptr)
    {
        LOG(Warning, "Cannot add patch at {0}x{1}. The patch at the given location already exists.", patchCoord.X, patchCoord.Y);
        return;
    }
    PROFILE_MEM(LevelTerrain);
    if (_chunkSize == 0)
        Setup();

    patch = ::New<TerrainPatch>();
    patch->Init(this, patchCoord.X, patchCoord.Y);
    _patches.Add(patch);

    CacheNeighbors();

    if (IsDuringPlay())
    {
        patch->UpdateTransform();
        patch->CreateCollision();
        UpdateLayerBits();
    }

    UpdateBounds();
}

void Terrain::RemovePatch(const Int2& patchCoord)
{
    const auto patch = GetPatch(patchCoord);
    if (patch == nullptr)
    {
        LOG(Warning, "Cannot remove patch at {0}x{1}. It does not exist.", patchCoord.X, patchCoord.Y);
        return;
    }

    ::Delete(patch);
    _patches.Remove(patch);

    CacheNeighbors();

    if (IsDuringPlay())
    {
        UpdateBounds();
    }
}

#endif

void Terrain::Draw(RenderContextBatch& renderContextBatch)
{
    PROFILE_CPU();
    if (DrawSetup(renderContextBatch.GetMainContext()))
        return;
    HashSet<TerrainChunk*, RendererAllocation> drawnChunks;
    for (RenderContext& renderContext : renderContextBatch.Contexts)
    {
        const DrawPass drawModes = DrawModes & renderContext.View.Pass;
        if (drawModes == DrawPass::None)
            continue;
        DrawImpl(renderContext, drawnChunks);
    }
}

void Terrain::Draw(RenderContext& renderContext)
{
    const DrawPass drawModes = DrawModes & renderContext.View.Pass;
    if (drawModes == DrawPass::None)
        return;
    PROFILE_CPU();
    if (DrawSetup(renderContext))
        return;
    HashSet<TerrainChunk*, RendererAllocation> drawnChunks;
    DrawImpl(renderContext, drawnChunks);
}

bool Terrain::DrawSetup(RenderContext& renderContext)
{
    // Special drawing modes
    const DrawPass drawModes = DrawModes & renderContext.View.Pass;
    if (drawModes == DrawPass::GlobalSDF)
    {
        const float chunkScale = 0.25f / (TERRAIN_UNITS_PER_VERTEX * (float)_chunkSize); // Patch heightfield is divided into 4x4 chunks
        for (const TerrainPatch* patch : _patches)
        {
            if (!patch->Heightmap)
                continue;
            GPUTexture* heightfield = patch->Heightmap->GetTexture();
            float size = (float)heightfield->Width();
            Float4 localToUV;
            localToUV.X = localToUV.Y = chunkScale * (size - 1) / size; // Skip the last edge texel
            localToUV.Z = localToUV.W = 0.5f / size; // Include half-texel offset
            Transform patchTransform;
            patchTransform.Translation = patch->_offset + Vector3(0, patch->_yOffset, 0);
            patchTransform.Orientation = Quaternion::Identity;
            patchTransform.Scale = Float3(1.0f, patch->_yHeight, 1.0f);
            patchTransform = _transform.LocalToWorld(patchTransform);
            GlobalSignDistanceFieldPass::Instance()->RasterizeHeightfield(this, heightfield, patchTransform, patch->_bounds, localToUV);
        }
        return true;
    }
    if (drawModes == DrawPass::GlobalSurfaceAtlas)
    {
        for (TerrainPatch* patch : _patches)
        {
            if (!patch->Heightmap)
                continue;
            Matrix localToWorld, worldToLocal;
            BoundingSphere chunkSphere;
            BoundingBox localBounds;
            for (int32 chunkIndex = 0; chunkIndex < Terrain::ChunksCount; chunkIndex++)
            {
                TerrainChunk* chunk = &patch->Chunks[chunkIndex];
                chunk->GetTransform().GetWorld(localToWorld); // TODO: large-worlds
                Matrix::Invert(localToWorld, worldToLocal);
                BoundingBox::Transform(chunk->GetBounds(), worldToLocal, localBounds);
                BoundingSphere::FromBox(chunk->GetBounds(), chunkSphere);
                GlobalSurfaceAtlasPass::Instance()->RasterizeActor(this, chunk, chunkSphere, chunk->GetTransform(), localBounds, 1 << 2, false);
            }
        }
        return true;
    }




    // 重置 _nativeComputedLod = 0 的隐患（与本次 C# 锁定脚本无关，纯引擎语义问题）：
    //
    // _nativeComputedLod 是只读快照，不参与渲染和邻居 morph，只给外部(C#/调试)读取。
    // 每帧在 DrawSetup 里把它归零，等于对外声称"本帧这个块原生 LOD 是 0"，
    // 但实际语义是"本帧这个块没被 PrepareDraw"，两者被混为一谈。
    //
    // 可能隐患：
    //   1. 语义误导：视锥外的块 NativeComputedLod 恒为 0，无法区分
    //      "原生真的是 0" 和 "本帧根本没算"。任何基于该值的外部逻辑
    //      (剔除、统计、调试、未来脚本) 都会把视锥外块误判为最精细。
    //   2. 统计失真：若用它做 LOD 分布统计/性能分析，视锥外块会全部
    //      被计入 LOD0，得出错误分布。
    //   3. 帧间不一致：同一块在"移出视锥"瞬间，NativeComputedLod 从真实值
    //      跳变为 0，再移回又跳回真实值，形成无意义的帧间跳变，
    //      对任何做差/做历史平滑的逻辑都是噪声。
    //   4. 掩盖真实问题：如果某块因流送/资源问题长期不被 PrepareDraw，
    //      归零会让它看起来"一切正常(LOD0)"，问题被隐藏。
    //
    // 两种可选修法(暂不实施)：
    //   A. 不重置，保留上次 PrepareDraw 的值 —— 但会变成"过期值"，
    //      且无法表达"从未计算过"。
    //   B. 重置为 -1 哨兵值，并在 API 上区分"有效值"与"未计算"，
    //      或另加 _preparedThisFrame 标志，让读取方明确知道本帧是否有效。

    // Reset cached LOD for chunks (prevent LOD transition from invisible chunks)
    for (int32 patchIndex = 0; patchIndex < _patches.Count(); patchIndex++)
    {
        const auto patch = _patches[patchIndex];
        for (int32 chunkIndex = 0; chunkIndex < Terrain::ChunksCount; chunkIndex++)
        {
            auto chunk = &patch->Chunks[chunkIndex];
            chunk->_cachedDrawLOD = 0;
            chunk->_nativeComputedLod = 0;
        }
    }

    return false;
}

void Terrain::DrawImpl(RenderContext& renderContext, HashSet<TerrainChunk*, RendererAllocation>& drawnChunks)
{
    // Collect chunks to render and calculate LOD/material for them (required to be done before to gather NeighborLOD)
    Array<TerrainChunk*, RendererAllocation> drawChunks;

    // Frustum vs Box culling for patches
    const BoundingFrustum frustum = renderContext.View.CullingFrustum;
    const Vector3 origin = renderContext.View.Origin;
    for (int32 patchIndex = 0; patchIndex < _patches.Count(); patchIndex++)
    {
        const auto patch = _patches[patchIndex];
        BoundingBox bounds(patch->_bounds.Minimum - origin, patch->_bounds.Maximum - origin);
        if (renderContext.View.IsCullingDisabled || frustum.Intersects(bounds))
        {
            // Skip if has no heightmap or it's not loaded
            if (patch->Heightmap == nullptr || patch->Heightmap->GetTexture()->ResidentMipLevels() == 0)
                continue;

            // Frustum vs Box culling for chunks
            for (int32 chunkIndex = 0; chunkIndex < Terrain::ChunksCount; chunkIndex++)
            {
                auto chunk = &patch->Chunks[chunkIndex];
                bounds = BoundingBox(chunk->_bounds.Minimum - origin, chunk->_bounds.Maximum - origin);
                if (renderContext.View.IsCullingDisabled || frustum.Intersects(bounds))
                {
                    if (!drawnChunks.Contains(chunk) && !chunk->PrepareDraw(renderContext))
                        continue;

                    // Add chunk for drawing
                    drawChunks.Add(chunk);
                    drawnChunks.Add(chunk);
                }
            }
        }
    }

    // Draw all visible chunks
    for (int32 i = 0; i < drawChunks.Count(); i++)
    {
        drawChunks.Get()[i]->Draw(renderContext);
    }
}

#if USE_EDITOR

//#include "Engine/Debug/DebugDraw.h"

void Terrain::OnDebugDrawSelected()
{
    Actor::OnDebugDrawSelected();

    /*
    // TODO: add editor option to draw selected terrain chunks bounds?
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        for (int32 chunkIndex = 0; chunkIndex < Terrain::ChunksCount; chunkIndex++)
        {
            auto chunk = &patch->Chunks[chunkIndex];
            DebugDraw::DrawBox(chunk->_bounds, Color(chunk->_x / (float)Terrain::ChunksCountEdge, 1.0f, chunk->_z / (float)Terrain::ChunksCountEdge));
        }
    }
    */
}

#endif

bool Terrain::IntersectsItself(const Ray& ray, Real& distance, Vector3& normal)
{
    float minDistance = MAX_float;
    Vector3 minDistanceNormal = Vector3::Up;
    float tmpDistance;
    Vector3 tmpNormal;
    bool result = false;

    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        if (patch->HasCollision() &&
            patch->_bounds.Intersects(ray) &&
            patch->RayCast(ray.Position, ray.Direction, tmpDistance, tmpNormal) &&
            tmpDistance < minDistance)
        {
            minDistance = tmpDistance;
            minDistanceNormal = tmpNormal;
            result = true;
        }
    }

    distance = minDistance;
    normal = minDistanceNormal;
    return result;
}

void Terrain::Serialize(SerializeStream& stream, const void* otherObj)
{
    // Base
    Actor::Serialize(stream, otherObj);

    SERIALIZE_GET_OTHER_OBJ(Terrain);

    SERIALIZE_MEMBER(LODBias, _lodBias);
    SERIALIZE_MEMBER(ForcedLOD, _forcedLod);
    SERIALIZE_MEMBER(LODDistribution, _lodDistribution);
    SERIALIZE_MEMBER(ScaleInLightmap, _scaleInLightmap);
    SERIALIZE_MEMBER(BoundsExtent, _boundsExtent);
    SERIALIZE_MEMBER(CollisionLOD, _collisionLod);
    SERIALIZE_MEMBER(PhysicalMaterials, _physicalMaterials);
    SERIALIZE(Material);
    SERIALIZE(DrawModes);

    SERIALIZE_MEMBER(LODCount, _lodCount);
    SERIALIZE_MEMBER(ChunkSize, _chunkSize);

    if (_patches.HasItems())
    {
        stream.JKEY("Patches");
        stream.StartArray();
        for (int32 patchIndex = 0; patchIndex < _patches.Count(); patchIndex++)
        {
            stream.StartObject();
            _patches[patchIndex]->Serialize(stream, other && other->_patches.Count() == _patches.Count() ? other->_patches[patchIndex] : nullptr);
            stream.EndObject();
        }
        stream.EndArray();
    }
}

void Terrain::Deserialize(DeserializeStream& stream, ISerializeModifier* modifier)
{
    PROFILE_MEM(LevelTerrain);

    // Base
    Actor::Deserialize(stream, modifier);

    auto member = stream.FindMember("LODBias");
    if (member != stream.MemberEnd() && member->value.IsInt())
    {
        SetLODBias(member->value.GetInt());
    }

    member = stream.FindMember("ForcedLOD");
    if (member != stream.MemberEnd() && member->value.IsInt())
    {
        SetForcedLOD(member->value.GetInt());
    }

    member = stream.FindMember("CollisionLOD");
    if (member != stream.MemberEnd() && member->value.IsInt())
    {
        SetCollisionLOD(member->value.GetInt());
    }

    DESERIALIZE_MEMBER(LODDistribution, _lodDistribution);
    DESERIALIZE_MEMBER(ScaleInLightmap, _scaleInLightmap);
    DESERIALIZE_MEMBER(BoundsExtent, _boundsExtent);
    DESERIALIZE_MEMBER(PhysicalMaterials, _physicalMaterials);
    DESERIALIZE(Material);
    DESERIALIZE(DrawModes);

    member = stream.FindMember("LODCount");
    if (member != stream.MemberEnd() && member->value.IsInt())
    {
        _lodCount = member->value.GetInt();
    }

    member = stream.FindMember("ChunkSize");
    if (member != stream.MemberEnd() && member->value.IsInt())
    {
        _chunkSize = member->value.GetInt();
    }

    member = stream.FindMember("Patches");
    if (member != stream.MemberEnd() && member->value.IsArray())
    {
        auto& patchesData = member->value;
        const auto patchesCount = (int32)patchesData.Size();

        // Update patches if collection size is different
        if (patchesCount != _patches.Count())
        {
            _patches.ClearDelete();

            for (int32 i = 0; i < patchesCount; i++)
            {
                auto patch = ::New<TerrainPatch>();
                patch->Init(this, 0, 0);
                _patches.Add(patch);
            }
        }

        // Load patches
        for (int32 i = 0; i < patchesCount; i++)
        {
            auto patch = _patches[i];
            auto& patchData = patchesData[i];

            patch->Deserialize(patchData, modifier);
        }

#if !BUILD_RELEASE
        // Validate patches locations
        for (int32 i = 0; i < patchesCount; i++)
        {
            const auto patch = _patches[i];
            for (int32 j = i + 1; j < patchesCount; j++)
            {
                if (_patches[j]->_x == patch->_x && _patches[j]->_z == patch->_z)
                {
                    LOG(Warning, "Invalid terrain data! Overlapping terrain patches.");
                }
            }
        }
#endif
    }

    // [Deprecated on 07.02.2022, expires on 07.02.2024]
    if (modifier->EngineBuild <= 6330)
    {
        MARK_CONTENT_DEPRECATED();
        DrawModes |= DrawPass::GlobalSDF;
    }
    // [Deprecated on 27.04.2022, expires on 27.04.2024]
    if (modifier->EngineBuild <= 6331)
    {
        MARK_CONTENT_DEPRECATED();
        DrawModes |= DrawPass::GlobalSurfaceAtlas;
    }

    // [Deprecated on 15.02.2024, expires on 15.02.2026]
    JsonAssetReference<PhysicalMaterial> PhysicalMaterial;
    DESERIALIZE(PhysicalMaterial);
    if (PhysicalMaterial)
    {
        MARK_CONTENT_DEPRECATED();
        for (auto& e : _physicalMaterials)
            e = PhysicalMaterial;
    }
}

RigidBody* Terrain::GetAttachedRigidBody() const
{
    // Terrains are always static things
    return nullptr;
}

void Terrain::OnEnable()
{
    GetScene()->Navigation.Actors.Add(this);
    GetSceneRendering()->AddActor(this, _sceneRenderingKey);
#if TERRAIN_USE_PHYSICS_DEBUG
    GetSceneRendering()->AddPhysicsDebug(this);
#endif
    void* scene = GetPhysicsScene()->GetPhysicsScene();
    for (int32 i = 0; i < _patches.Count(); i++)
    {
        auto patch = _patches[i];
        if (patch->_physicsActor)
            PhysicsBackend::AddSceneActor(scene, patch->_physicsActor);
    }

    // Base
    Actor::OnEnable();
}

void Terrain::OnDisable()
{
    GetScene()->Navigation.Actors.Remove(this);
    GetSceneRendering()->RemoveActor(this, _sceneRenderingKey);
#if TERRAIN_USE_PHYSICS_DEBUG
    GetSceneRendering()->RemovePhysicsDebug(this);
#endif
    void* scene = GetPhysicsScene()->GetPhysicsScene();
    for (int32 i = 0; i < _patches.Count(); i++)
    {
        auto patch = _patches[i];
        if (patch->_physicsActor)
            PhysicsBackend::RemoveSceneActor(scene, patch->_physicsActor);
    }

    // Base
    Actor::OnDisable();
}

void Terrain::OnTransformChanged()
{
    // Base
    Actor::OnTransformChanged();

    for (int32 i = 0; i < _patches.Count(); i++)
    {
        auto patch = _patches[i];
        patch->UpdateTransform();
    }
    if (_cachedScale != _transform.Scale)
    {
        _cachedScale = _transform.Scale;
        for (int32 i = 0; i < _patches.Count(); i++)
        {
            auto patch = _patches[i];
            if (patch->HasCollision())
            {
                patch->UpdateCollisionScale();
            }
        }
    }
    UpdateBounds();
}

void Terrain::OnLayerChanged()
{
    // Base
    Actor::OnLayerChanged();

    UpdateLayerBits();
    if (_sceneRenderingKey != -1)
        GetSceneRendering()->UpdateActor(this, _sceneRenderingKey, ISceneRenderingListener::Layer);
}

void Terrain::OnActiveInTreeChanged()
{
    // Base
    Actor::OnActiveInTreeChanged();

    // Update physics
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        if (patch->HasCollision())
        {
            PhysicsBackend::SetShapeState(patch->_physicsShape, IsActiveInHierarchy(), false);
        }
    }
}

void Terrain::OnPhysicsSceneChanged(PhysicsScene* previous)
{
    PhysicsColliderActor::OnPhysicsSceneChanged(previous);

    for (auto patch : _patches)
        patch->OnPhysicsSceneChanged(previous);
}

void Terrain::BeginPlay(SceneBeginData* data)
{
    CacheNeighbors();
    _cachedScale = _transform.Scale;
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        if (!patch->HasCollision())
        {
            patch->CreateCollision();
        }
    }
    UpdateLayerBits();

    // Base
    Actor::BeginPlay(data);
}

void Terrain::EndPlay()
{
    for (int32 pathIndex = 0; pathIndex < _patches.Count(); pathIndex++)
    {
        const auto patch = _patches[pathIndex];
        if (patch->HasCollision())
        {
            patch->DestroyCollision();
        }
    }

    // Base
    Actor::EndPlay();
}








#if TERRAIN_EDITING


Color32 Get_HM_Height(int width, const Array<Color32>& hm, int x, int y) {
    return hm[y * width + x];
    //return hm[x * width + y];
}

void Set_HM_Height(int width, Array<Color32>& hm, int x, int y, Color32 h) {
    hm[y * width + x] = h;
    //hm[x * width + y] = h;
}


// 变换2字节压缩高度值 为 0～1 的浮点高度
FORCE_INLINE float GetNormalizedHeight(const Color32& raw)
{
    const uint16 quantizedHeight = raw.R | (raw.G << 8);
    const float normalizedHeight = (float)quantizedHeight / MAX_uint16;
    return normalizedHeight;
}


// 传入归一化的高度数据，设置RG为指定量化高度，保留B、A（法线数据不变）
FORCE_INLINE void WriteHeightToRG(Color32& raw, const float normalizedHeight)
{
    const uint16 quantizedHeight = (uint16)(normalizedHeight * MAX_uint16);

    raw.R = (uint8)(quantizedHeight & 0xff);
    raw.G = (uint8)((quantizedHeight >> 8) & 0xff);
}

// 解量化 uint8 [-128~127] → float(-1 ~ 1)
FORCE_INLINE float UnpackNormal(uint8 v)
{
    return (static_cast<float>(v) - 128.0f) / 127.0f;
}
FORCE_INLINE uint8 PackNormal(float val)
{
    val = Math::Clamp(val, -1.0f, 1.0f);
    return static_cast<uint8>(val * 127.0f + 128.0f);
}

/*
// UnpackNormal: uint8(0‑255) → float(-1 ~ +1)
inline float UnpackNormal(uint8 val)
{
    return (val / 255.0f) * 2.0f - 1.0f;
}
*/


// 根据xz重建完整单位地形法线
Float3 ReconstructNormal(float nx, float nz)
{
    const float nySq = 1.0f - nx * nx - nz * nz;
    const float ny = Math::Sqrt(Math::Max(nySq, 0.0f));
    return Float3(nx, ny, nz);
}

/*
// 完全复刻hlsl DecodeHeightmapNormal，球面压缩，不是octahedral！
Float3 ReconstructNormal(float nxCompressed, float nzCompressed)
{
    float nx = nxCompressed;
    float nz = nzCompressed;
    float dotVal = nx * nx + nz * nz;
    float ny = sqrtf(Math::Saturate(1.0f - dotVal));
    Float3 normal(nx, ny, nz);
    normal.Normalize();
    return normal;
}
*/


// 融合两条边界法线
Float3 BlendTerrainNormal(const Float3& n1, const Float3& n2)
{
    Float3 sum = n1 + n2;
    // 防止两个法线完全反向，sum接近0
    if (sum.LengthSquared() < 1e-9f)
        return n1;

    sum.Normalize();

    return sum;
}


typedef struct {
    float patchOffsetA;
    float patchHeightA;
    float patchOffsetB;
    float patchHeightB;
} PatchInfo;

void avgColor32(const PatchInfo& pi, const Color32& colA, const Color32& colB, Color32& colAVG_A, Color32& colAVG_B )
{

    float normalizedHeightA = GetNormalizedHeight(colA);
    const float heightA = (normalizedHeightA * pi.patchHeightA) + pi.patchOffsetA;

    float normalizedHeightB = GetNormalizedHeight(colB);
    const float heightB = (normalizedHeightB * pi.patchHeightB) + pi.patchOffsetB;


    // --- 平均高度 ---
    const float heightAvg = (heightA + heightB) * 0.5f;

    normalizedHeightA = (heightAvg - pi.patchOffsetA) / pi.patchHeightA;
    normalizedHeightB = (heightAvg - pi.patchOffsetB) / pi.patchHeightB;
    //传入归一化的高度数据，设置RG为指定量化高度，保留B、A（法线数据不变）
    WriteHeightToRG(colAVG_A, normalizedHeightA);
    WriteHeightToRG(colAVG_B, normalizedHeightB);

    // --- 平均法线 ---
    // 从像素读出
    float nx1 = UnpackNormal(colA.B);
    float nz1 = UnpackNormal(colA.A);
    float nx2 = UnpackNormal(colB.B);
    float nz2 = UnpackNormal(colB.A);

    // 恢复完整三维法线
    Float3 n1 = ReconstructNormal(nx1, nz1);
    Float3 n2 = ReconstructNormal(nx2, nz2);

    // 向量融合归一
    Float3 nBlend = BlendTerrainNormal(n1, n2);

    // 仅保存 X,Z 回像素
    colAVG_A.B = colAVG_B.B = PackNormal(nBlend.X);
    colAVG_A.A = colAVG_B.A = PackNormal(nBlend.Z);
}



void Terrain::RepairHeightmapEdge()
{
    PROFILE_CPU();
    if (_patches.IsEmpty())
    {
        LOG(Warning, "RepairHeightmapEdge: No terrain patches exist.");
        return;
    }

    // 构建 Patch坐标 -> Patch 对象映射表
    Dictionary<Int2,TerrainPatch*> patchGrid;

    for (int32 i = 0; i < _patches.Count(); i++)
    {
        TerrainPatch* patch = _patches[i];
        
        if (!patch->Heightmap.Get()) {
            LOG(Warning, "RepairHeightmapEdge: Heightmap Texture is NULL.");
            return;
        }
        patch->Heightmap.Get()->GetPixels(patch->tmpHeightmapPixels); // 读取高度图数据到临时缓存

        Int2 patchCoord;
        GetPatchCoord(i, patchCoord);
        patchGrid[patchCoord] = patch;
    }

    // 记录所有被修改的Patch，最后统一刷新
    HashSet<TerrainPatch*> dirtyPatches;

    // 取高度图尺寸（同地形所有Patch高度图尺寸一致）
    TerrainPatch* firstPatch = _patches[0];
    const int32 hmSize = firstPatch->Heightmap->Width();
    if (hmSize <= 0)
    {
        LOG(Error, "RepairHeightmapEdge: Invalid heightmap size");
        return;
    }

    
    // 遍历全部Patch
    for (const auto& kv : patchGrid)
    {
        const Int2 patchPos = kv.Key;
        TerrainPatch* patch = kv.Value;
        Array<Color32>& hmA = patch->tmpHeightmapPixels;

        // ========== X轴相邻：当前Patch 右边界 <=> 右侧邻居左边界 ==========
        const Int2 rightNeighborPos(patchPos.X + 1, patchPos.Y);
        TerrainPatch** rightNeighborPtr = patchGrid.TryGet(rightNeighborPos);
        if (rightNeighborPtr && *rightNeighborPtr)
        {
            TerrainPatch* neighbor = *rightNeighborPtr;
            Array<Color32>& hmB = neighbor->tmpHeightmapPixels;

            PatchInfo pi;
            pi.patchHeightA = patch->GetHeightY();
            pi.patchOffsetA = patch->GetOffsetY();
            pi.patchHeightB = neighbor->GetHeightY();
            pi.patchOffsetB = neighbor->GetOffsetY();

            dirtyPatches.Add(patch);
            dirtyPatches.Add(neighbor);
            for (int32 y = 0; y < hmSize; y++)
            {
                const Color32 colA = Get_HM_Height(hmSize, hmA, hmSize - 1, y);
                const Color32 colB = Get_HM_Height(hmSize, hmB, 0, y);
                Color32 avgA,avgB;
                avgColor32(pi, colA, colB, avgA, avgB);
                Set_HM_Height(hmSize, hmA, hmSize - 1, y, avgA);
                Set_HM_Height(hmSize, hmB, 0, y, avgB);
            }
        }
        
        // ========== Z轴相邻：当前Patch 顶部边界 <=> 上方邻居底部边界 ==========
        const Int2 topNeighborPos(patchPos.X, patchPos.Y + 1);
        TerrainPatch** topNeighborPtr = patchGrid.TryGet(topNeighborPos);
        if (topNeighborPtr && *topNeighborPtr)
        {
            TerrainPatch* neighbor = *topNeighborPtr;
            Array<Color32>& hmB = neighbor->tmpHeightmapPixels;

            PatchInfo pi;
            pi.patchHeightA = patch->GetHeightY();
            pi.patchOffsetA = patch->GetOffsetY();
            pi.patchHeightB = neighbor->GetHeightY();
            pi.patchOffsetB = neighbor->GetOffsetY();

            dirtyPatches.Add(patch);
            dirtyPatches.Add(neighbor);
            for (int32 x = 0; x < hmSize; x++)
            {
                const Color32 colA = Get_HM_Height(hmSize, hmA, x, hmSize - 1);
                const Color32 colB = Get_HM_Height(hmSize, hmB, x, 0);
                Color32 avgA, avgB;
                avgColor32(pi, colA, colB, avgA, avgB);
                Set_HM_Height(hmSize, hmA, x, hmSize - 1, avgA);
                Set_HM_Height(hmSize, hmB, x, 0, avgB);
            }
        }
        
    }

    

    // 【重要】刷新修改后的Patch高度图纹理
    auto iter = dirtyPatches.Begin();
    while (iter.IsNotEnd())
    {
        TerrainPatch* patch = iter->Item;
        GPUTexture* gpuTex = patch->Heightmap->GetTexture();
        if (!gpuTex || !gpuTex->IsAllocated())
        {
            ++iter;
            continue;
        }


        //for (int i = 0; i < patch->tmpHeightmapPixels.Count(); i++) patch->tmpHeightmapPixels[i] = Color32();


        if (patch->OverwriteEncodedHeightmapFull(patch->tmpHeightmapPixels))
        {
            LOG(Error, "Patch Heightmap Texture Overwrite failed!");
        }


        //持久化写入磁盘（场景保存/手动调用均可）
        //patch->SaveHeightData();


        /*
        // 准备字节数据：Color32数组转为BytesContainer
        const Span<Color32> pixelSpan = ToSpan(patch->tmpHeightmapPixels);
        BytesContainer uploadBytes((byte*)pixelSpan.Get(), pixelSpan.Length() * sizeof(Color32));

        // 获取mip0对齐后的pitch
        const uint32 rowPitch = gpuTex->RowPitch(0);
        const uint32 slicePitch = gpuTex->SlicePitch(0);

        // 异步上传mip0，copyData=true防止内存生命周期问题
        gpuTex->UploadMipMapAsync(uploadBytes, 0, rowPitch, slicePitch, true);
        */

        ++iter;
    }

    LOG(Info, "RepairHeightmapEdge finished. Modified patches: {0}", dirtyPatches.Count());
}






//-------------------------------------------------------------- added by afench
// 根据Patch全局高度图坐标，查找对应Patch并采样高度
float Terrain::SampleHeightAtGlobalCoord(int32 x, int32 z) const
{
    const int32 patchHmSize = GetHeightmapSize();
    const int32 patchLocalMax = patchHmSize - 1;

    // 全局高度坐标 → Patch网格坐标 + Patch局部坐标
    int32 patchGridX = x / patchLocalMax;
    int32 patchGridZ = z / patchLocalMax;
    int32 localX = x % patchLocalMax;
    int32 localZ = z % patchLocalMax;

    // 处理负数坐标取模问题
    if (localX < 0)
    {
        localX += patchLocalMax;
        patchGridX -= 1;
    }
    if (localZ < 0)
    {
        localZ += patchLocalMax;
        patchGridZ -= 1;
    }

    // 获取目标Patch
    TerrainPatch* patch = GetPatch(patchGridX, patchGridZ);
    if (!patch)
    {
        return 0.0f;
    }

    // 钳位，防止越界
    localX = Math::Clamp(localX, 0, patchLocalMax);
    localZ = Math::Clamp(localZ, 0, patchLocalMax);

    float* heightData = patch->GetHeightmapData();
    if (heightData == nullptr)
    {
        return 0.0f;
    }
    const int32 idx = localZ * patchHmSize + localX;
    return heightData[idx];

}
//-------------------------------------------------------------------------------




float Terrain::SampleWorldHeight(float worldX, float worldZ) const
{
    // 1. 世界坐标转到Terrain Actor本地空间（去除Actor位置、旋转、缩放）
    Vector3 localPos = GetTransform().WorldToLocal(Vector3(worldX, 0, worldZ));

    // 2. terrain patch本地XZ -> 转为全局高度图网格整数坐标
    const float unitsPerVertex = TERRAIN_UNITS_PER_VERTEX;
    int32 globalHmX = Math::RoundToInt(localPos.X / unitsPerVertex);
    int32 globalHmZ = Math::RoundToInt(localPos.Z / unitsPerVertex);

    // 读取Patch局部空间Y高度
    const float patchLocalY = SampleHeightAtGlobalCoord(globalHmX, globalHmZ);
    if (Math::IsZero(patchLocalY) && GetPatch(globalHmX / (GetHeightmapSize() - 1), globalHmZ / (GetHeightmapSize() - 1)) == nullptr)
        return 0.0f;

    // 转回世界空间Y
    Vector3 worldPoint = GetTransform().LocalToWorld(Vector3(localPos.X, patchLocalY, localPos.Z));
    return worldPoint.Y;
}



float Terrain::SampleWorldHeightBilinear(float worldX, float worldZ) const
{
    // 1. 世界坐标转到Terrain Actor本地空间（去除Actor位置、旋转、缩放）
    Vector3 localPos = GetTransform().WorldToLocal(Vector3(worldX, 0, worldZ));

    const float unitsPerVertex = TERRAIN_UNITS_PER_VERTEX;

    const float hmFloatX = localPos.X / unitsPerVertex;
    const float hmFloatZ = localPos.Z / unitsPerVertex;

    int32 x0 = (int32)Math::Floor(hmFloatX);
    int32 z0 = (int32)Math::Floor(hmFloatZ);
    int32 x1 = x0 + 1;
    int32 z1 = z0 + 1;

    // 插值权重 [0~1]
    const float fx = hmFloatX - x0;
    const float fz = hmFloatZ - z0;

    const float h00 = SampleHeightAtGlobalCoord(x0, z0);
    const float h10 = SampleHeightAtGlobalCoord(x1, z0);
    const float h01 = SampleHeightAtGlobalCoord(x0, z1);
    const float h11 = SampleHeightAtGlobalCoord(x1, z1);

    Int2 p00(x0 / (GetHeightmapSize() - 1), z0 / (GetHeightmapSize() - 1));
    if (!HasPatch(p00))
        return 0.0f;

    const float h0 = Math::Lerp(h00, h10, fx);
    const float h1 = Math::Lerp(h01, h11, fx);
    const float patchLocalY = Math::Lerp(h0, h1, fz);

    // 变换回世界空间Y
    Vector3 worldPoint = GetTransform().LocalToWorld(Vector3(localPos.X, patchLocalY, localPos.Z));
    return worldPoint.Y;
}



bool Terrain::SampleNormalRawAtGlobalCoord(int32 x, int32 z, float& outNx, float& outNz) const
{
    const int32 patchHmSize = GetHeightmapSize();
    const int32 patchLocalMax = patchHmSize - 1;

    // 全局高度坐标 → Patch网格坐标 + Patch局部坐标，和SampleHeightAtGlobalCoord完全一致
    int32 patchGridX = x / patchLocalMax;
    int32 patchGridZ = z / patchLocalMax;
    int32 localX = x % patchLocalMax;
    int32 localZ = z % patchLocalMax;

    // 负数坐标取模修正，复制SampleHeightAtGlobalCoord逻辑
    if (localX < 0)
    {
        localX += patchLocalMax;
        patchGridX -= 1;
    }
    if (localZ < 0)
    {
        localZ += patchLocalMax;
        patchGridZ -= 1;
    }

    TerrainPatch* patch = GetPatch(patchGridX, patchGridZ);
    if (!patch)
        return false;

    localX = Math::Clamp(localX, 0, patchLocalMax);
    localZ = Math::Clamp(localZ, 0, patchLocalMax);

    // 关键：需要patch加载heightmap像素缓存 tmpHeightmapPixels
    // 注意！调用RepairHeightmapEdge会填充tmpHeightmapPixels；编辑器下调用RepairHeightmapEdge后有效。
    // 如果游戏运行时需要运行时采样法线，需要自己调用 patch->Heightmap->GetPixels(patch->tmpHeightmapPixels);
    auto& pixels = patch->tmpHeightmapPixels;
    if (pixels.IsEmpty())
    {
        // 没有缓存像素，无法读取BA打包法线
        return false;
    }

    int32 idx = localZ * patchHmSize + localX;
    //int32 idx = localX * patchHmSize + localZ;
    const Color32& pixel = pixels[idx];

    outNx = UnpackNormal(pixel.B);
    outNz = UnpackNormal(pixel.A);
    return true;
}


Vector3 Terrain::SampleWorldNormal(float worldX, float worldZ) const
{
    // ========== 和SampleWorldHeight逻辑完全一致：世界坐标转Terrain本地 ==========
    Vector3 localPos = GetTransform().WorldToLocal(Vector3(worldX, 0, worldZ));
    const float unitsPerVertex = TERRAIN_UNITS_PER_VERTEX;

    int32 globalHmX = Math::RoundToInt(localPos.X / unitsPerVertex);
    int32 globalHmZ = Math::RoundToInt(localPos.Z / unitsPerVertex);

    float nx, nz;
    if (!SampleNormalRawAtGlobalCoord(globalHmX, globalHmZ, nx, nz)) return Vector3::Up;

    // 重建patch本地空间法线
    Float3 localNormal = ReconstructNormal(nx, nz);
//    return localNormal;

    // trans to world space
    Matrix worldMat;
    GetTransform().GetWorld(worldMat);

    Matrix worldInv;
    Matrix::Invert(worldMat, worldInv);
    Matrix invTranspose;
    Matrix::Transpose(worldInv, invTranspose);

    Vector3 worldNormal;
    Vector3::TransformNormal(localNormal, invTranspose, worldNormal);
    worldNormal.Normalize();

    return worldNormal;
}


/*
Vector3 Terrain::SampleWorldNormalBilinear(float worldX, float worldZ) const
{
    // 和SampleWorldHeightBilinear逻辑对齐
    Vector3 localPos = GetTransform().WorldToLocal(Vector3(worldX, 0, worldZ));
    const float unitsPerVertex = TERRAIN_UNITS_PER_VERTEX;
    const float hmFloatX = localPos.X / unitsPerVertex;
    const float hmFloatZ = localPos.Z / unitsPerVertex;

    int32 x0 = (int32)Math::Floor(hmFloatX);
    int32 z0 = (int32)Math::Floor(hmFloatZ);
    int32 x1 = x0 + 1;
    int32 z1 = z0 + 1;

    const float fx = hmFloatX - x0;
    const float fz = hmFloatZ - z0;

    float nx00, nz00;
    float nx10, nz10;
    float nx01, nz01;
    float nx11, nz11;

    if (!SampleNormalRawAtGlobalCoord(x0, z0, nx00, nz00) ||
        !SampleNormalRawAtGlobalCoord(x1, z0, nx10, nz10) ||
        !SampleNormalRawAtGlobalCoord(x0, z1, nx01, nz01) ||
        !SampleNormalRawAtGlobalCoord(x1, z1, nx11, nz11))
    {
        return Vector3::Up;
    }


    Float3 n00 = ReconstructNormal(nx00, nz00);
    Float3 n10 = ReconstructNormal(nx10, nz10);
    Float3 n01 = ReconstructNormal(nx01, nz01);
    Float3 n11 = ReconstructNormal(nx11, nz11);

    // 双线性插值完整法线xyz
    Float3 n0 = Math::Lerp(n00, n10, fx);
    Float3 n1 = Math::Lerp(n01, n11, fx);
    Float3 localNormal = Math::Lerp(n0, n1, fz);
    //return localNormal;
    

    // trans to world space
    Matrix worldMat;
    GetTransform().GetWorld(worldMat);

    Matrix worldInv;
    Matrix::Invert(worldMat, worldInv);
    Matrix invTranspose;
    Matrix::Transpose(worldInv, invTranspose);

    Vector3 worldNormal;
    Vector3::TransformNormal(localNormal, invTranspose, worldNormal);
    worldNormal.Normalize();

    return worldNormal;
   
}
*/

Vector3 Terrain::SampleWorldNormalBilinear(float worldX, float worldZ) const
{
    const float step = TERRAIN_UNITS_PER_VERTEX; // 顶点之间世界距离，和Shader一致

    // 采样4邻域高度，做有限差分
    float hC = SampleWorldHeightBilinear(worldX, worldZ);
    float hDx = SampleWorldHeightBilinear(worldX + step, worldZ);
    float hDz = SampleWorldHeightBilinear(worldX, worldZ + step);

    // 构造3个世界空间点
    Vector3 pC(worldX, hC, worldZ);
    Vector3 pX(worldX + step, hDx, worldZ);
    Vector3 pZ(worldX, hC, worldZ + step);

    // 两个切向向量
    Vector3 tX = pX - pC;
    Vector3 tZ = pZ - pC;

    // 叉乘求法线，归一化为世界空间单位法线
    Vector3 normal = Vector3::Cross(tZ, tX);
    normal.Normalize();

    return normal;
}

/*
TerrainChunk* Terrain::GetChunkAt(const Vector3& worldPos) const
{
    for (int32 i = 0; i < _patches.Count(); i++)
    {
        TerrainPatch* patch = _patches[i];
        for (int32 chunkIdx = 0; chunkIdx < Terrain::ChunksCount; chunkIdx++)
        {
            TerrainChunk* chunk = &patch->Chunks[chunkIdx];
            BoundingBox box = chunk->GetBounds();
            if (box.Contains(worldPos) == ContainmentType::Contains)
            {
                return chunk;
            }
        }
    }
    return nullptr;
}
*/

TerrainChunk* Terrain::GetChunkAt(const Vector3& worldPos) const
{
    if (_patches.IsEmpty() || _chunkSize == 0)
        return nullptr;

    // 世界坐标转到 Terrain 本地空间（处理 actor 旋转/缩放/平移）
    const Vector3 localPos = _transform.WorldToLocal(worldPos);

    const float chunkWorldSize = (float)_chunkSize * TERRAIN_UNITS_PER_VERTEX;
    const float patchWorldSize = chunkWorldSize * (float)ChunksCountEdge;

    // 用 floor 处理负坐标，不能用 (int) 强转（向零取整）
    const int32 patchX = (int32)Math::Floor(localPos.X / patchWorldSize);
    const int32 patchZ = (int32)Math::Floor(localPos.Z / patchWorldSize);

    // 算 patch 内本地偏移，再算 chunk 局部坐标
    const float localInPatchX = localPos.X - (float)patchX * patchWorldSize;
    const float localInPatchZ = localPos.Z - (float)patchZ * patchWorldSize;

    int32 chunkX = (int32)Math::Floor(localInPatchX / chunkWorldSize);
    int32 chunkZ = (int32)Math::Floor(localInPatchZ / chunkWorldSize);

    // 浮点误差保护：恰好落在 patch 右/下边界时，floor 可能算出 ChunksCountEdge
    chunkX = Math::Clamp(chunkX, 0, (int32)ChunksCountEdge - 1);
    chunkZ = Math::Clamp(chunkZ, 0, (int32)ChunksCountEdge - 1);

    // 直接按坐标取 patch，不遍历。GetPatch(x,z) 内部还是线性查找，
    // 但 patch 数量少时影响小；若 patch 很多可另建字典缓存。
    TerrainPatch* patch = GetPatch(patchX, patchZ);
    if (!patch)
        return nullptr;

    // 用线性索引取 chunk，和 GetChunk(x,z) 等价
    const int32 chunkIndex = chunkZ * (int32)ChunksCountEdge + chunkX;
    return patch->GetChunk(chunkIndex);
}


#endif
