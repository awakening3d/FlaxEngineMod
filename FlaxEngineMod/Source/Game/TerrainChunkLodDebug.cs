using FlaxEngine;
using System.Text;
public class TerrainChunkLodDebug : Script
{
    private float _timer;
    private const float PrintInterval = 1f;
    [Tooltip("Camera used to sample terrain chunk under view")]
    public Actor CameraActor;
    public override void OnUpdate()
    {
        _timer += Time.DeltaTime;
        if (_timer < PrintInterval)
            return;
        _timer = 0;
        Terrain terrain = Actor as Terrain;
        if (terrain == null)
        {
            Debug.LogWarning("TerrainChunkLodDebug: Actor is not Terrain");
            return;
        }
        if (CameraActor == null)
        {
            Debug.LogWarning("TerrainChunkLodDebug: Assign CameraActor in inspector");
            return;
        }
        Vector3 camPos = CameraActor.Position;
        TerrainChunk hitChunk = terrain.GetChunkAt(camPos);

        // ========== 全部Chunk遍历，打印【全局Chunk坐标】 ==========
        StringBuilder sbAllChunks = new StringBuilder();
        sbAllChunks.Append("ALL_CHUNKS_LOD_GLOBAL:");
        int patchCount = terrain.PatchesCount;
        const int ChunksPerPatch = Terrain.PatchChunksCount;
        for (int p = 0; p < patchCount; p++)
        {
            TerrainPatch patch = terrain.GetPatch(p);
            if (patch == null) continue;
            for (int c = 0; c < ChunksPerPatch; c++)
            {
                TerrainChunk chunk = patch.GetChunk(c);
                if (chunk == null) continue;
                // 计算全局Chunk坐标！！！修复核心
                int localCx = chunk.GetX();
                int localCz = chunk.GetZ();
                int globalCx = patch.PatchX * ChunksPerPatch + localCx;
                int globalCz = patch.PatchZ * ChunksPerPatch + localCz;

                sbAllChunks.Append($" ({globalCx},{globalCz})[{chunk.NativeComputedLOD}|{chunk.CachedDrawLOD}]");
            }
        }
        Debug.Log(sbAllChunks.ToString());

        // ========== 相机所在Chunk + 边缘判断日志 ==========
        if (hitChunk != null)
        {
			// =====新增：隐藏相机所在的Chunk=====
			//hitChunk.Visible = false;
	
            BoundingBox chunkBounds = hitChunk.GetBounds();
            float chunkSizeX = chunkBounds.Maximum.X - chunkBounds.Minimum.X;
            float chunkSizeZ = chunkBounds.Maximum.Z - chunkBounds.Minimum.Z;
            float distToMinX = camPos.X - chunkBounds.Minimum.X;
            float distToMaxX = chunkBounds.Maximum.X - camPos.X;
            float distToMinZ = camPos.Z - chunkBounds.Minimum.Z;
            float distToMaxZ = chunkBounds.Maximum.Z - camPos.Z;
            // 边缘阈值：Chunk边长的20%，你可以在代码里改这个比例
            float edgeRatio = 0.2f;
            float edgeThreshX = chunkSizeX * edgeRatio;
            float edgeThreshZ = chunkSizeZ * edgeRatio;
            bool nearLeft = distToMinX <= edgeThreshX;
            bool nearRight = distToMaxX <= edgeThreshX;
            bool nearBottom = distToMinZ <= edgeThreshZ;
            bool nearTop = distToMaxZ <= edgeThreshZ;
            bool isInEdgeZone = nearLeft || nearRight || nearBottom || nearTop;
            Debug.Log("===== Camera Chunk Edge Check =====");
            Debug.Log($"CamPos: {camPos}");
            Debug.Log($"Chunk X:{hitChunk.GetX()}, Z:{hitChunk.GetZ()}");
            Debug.Log($"Chunk Bounds Min:{chunkBounds.Minimum}, Max:{chunkBounds.Maximum}");
            Debug.Log($"Chunk Size X:{chunkSizeX:F2}, Z:{chunkSizeZ:F2}");
            Debug.Log($"DistToMinX:{distToMinX:F2}, DistToMaxX:{distToMaxX:F2}");
            Debug.Log($"DistToMinZ:{distToMinZ:F2}, DistToMaxZ:{distToMaxZ:F2}");
            Debug.Log($"EdgeThresh X:{edgeThreshX:F2}, Z:{edgeThreshZ:F2}");
            Debug.Log($"nearLeft:{nearLeft}, nearRight:{nearRight}, nearBottom:{nearBottom}, nearTop:{nearTop}");
            Debug.Log($"IsInEdgeZone: {isInEdgeZone}");
            Debug.Log($"Chunk NativeComputedLOD:{hitChunk.NativeComputedLOD}, CachedDrawLOD:{hitChunk.CachedDrawLOD}");
			// 计算到四边的归一距离（0=贴边，1=Chunk中心）
			float nxMinX = distToMinX / chunkSizeX;
			float nxMaxX = distToMaxX / chunkSizeX;
			float nzMinZ = distToMinZ / chunkSizeZ;
			float nzMaxZ = distToMaxZ / chunkSizeZ;
			// 取最小归一距离 = 离最近一条Chunk边有多远
			float nearestEdgeNorm = Mathf.Min(nxMinX, nxMaxX, nzMinZ, nzMaxZ);
			Debug.Log($"Normalized distance to nearest edge: {nearestEdgeNorm:F4}");			
            // 打印4邻Chunk LOD，用来比对接缝两侧
            TerrainChunk nRight = GetRelativeChunk(terrain, hitChunk, 1, 0);
            TerrainChunk nLeft = GetRelativeChunk(terrain, hitChunk, -1, 0);
            TerrainChunk nTop = GetRelativeChunk(terrain, hitChunk, 0, 1);
            TerrainChunk nBottom = GetRelativeChunk(terrain, hitChunk, 0, -1);
            if(nRight != null) Debug.Log($"Neighbor Right X:{nRight.GetX()},Z:{nRight.GetZ()}, NativeLOD:{nRight.NativeComputedLOD}, DrawLOD:{nRight.CachedDrawLOD}");
            if(nLeft != null) Debug.Log($"Neighbor Left X:{nLeft.GetX()},Z:{nLeft.GetZ()}, NativeLOD:{nLeft.NativeComputedLOD}, DrawLOD:{nLeft.CachedDrawLOD}");
            if(nTop != null) Debug.Log($"Neighbor Top X:{nTop.GetX()},Z:{nTop.GetZ()}, NativeLOD:{nTop.NativeComputedLOD}, DrawLOD:{nTop.CachedDrawLOD}");
            if(nBottom != null) Debug.Log($"Neighbor Bottom X:{nBottom.GetX()},Z:{nBottom.GetZ()}, NativeLOD:{nBottom.NativeComputedLOD}, DrawLOD:{nBottom.CachedDrawLOD}");
        }
        else
        {
            Debug.LogWarning("TerrainChunkLodDebug: GetChunkAt returned null, camera outside terrain");
        }
    }
    /// <summary>
    /// 获取相邻Chunk，自动跨Patch（Flax Patch固定4x4 Chunk）
    /// </summary>
    private TerrainChunk GetRelativeChunk(Terrain terrain, TerrainChunk srcChunk, int dx, int dz)
    {
        int cx = srcChunk.GetX();
        int cz = srcChunk.GetZ();
        TerrainPatch srcPatch = srcChunk.GetPatch();
        if (srcPatch == null) return null;
        const int ChunksPerPatch = Terrain.PatchChunksCount;
        int nx = cx + dx;
        int nz = cz + dz;
        int patchX = srcPatch.PatchX;
        int patchZ = srcPatch.PatchZ;
        int targetCx, targetCz;
        if (nx < 0)
        {
            patchX--;
            targetCx = ChunksPerPatch - 1;
        }
        else if (nx >= ChunksPerPatch)
        {
            patchX++;
            targetCx = 0;
        }
        else
        {
            targetCx = nx;
        }
        if (nz < 0)
        {
            patchZ--;
            targetCz = ChunksPerPatch - 1;
        }
        else if (nz >= ChunksPerPatch)
        {
            patchZ++;
            targetCz = 0;
        }
        else
        {
            targetCz = nz;
        }
        TerrainPatch targetPatch = terrain.GetPatch(patchX, patchZ);
        if (targetPatch == null) return null;
        return targetPatch.GetChunk((ushort)targetCx, (ushort)targetCz);
    }
}
