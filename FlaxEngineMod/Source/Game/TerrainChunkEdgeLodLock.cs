using System;
using System.Collections.Generic;
using FlaxEngine;

/// <summary>
/// TerrainChunkEdgeLodLock: mounted on Terrain Actor, camera-driven, stabilize Chunk border LOD
/// Rule: Current(center) chunk NO LOCK. Only lock 8 adjacent chunks.
/// When center chunk changes, restore previous locked neighbors (unlock old 8 neighbors).
/// </summary>
public class TerrainChunkEdgeLodLock : Script
{
    [Header("Camera Reference")]
    public Actor CameraActor;

    [Header("Update Throttle")]
    [Tooltip("Update interval in seconds, reduce frequent C++ interop calls")]
    public float TickInterval = 0.2f;

    [Header("Patch Chunk Validation")]
    public bool PrintCalcDebugLog = false;
    public bool PrintLockDebugLog = false;

    private Terrain _terrain;
    private float _timer;
    private TerrainChunk _lastCenterChunk; // 目前仅记录，无实际用途
    private int _cachedChunkSide = -1;

    private HashSet<TerrainChunk> _lastLockedSet = new HashSet<TerrainChunk>();
    private HashSet<TerrainChunk> _currentLockedSet = new HashSet<TerrainChunk>();

    private readonly (int dx, int dz)[] _neighborOffsets = new (int, int)[]
    {
        (-1, -1), (-1, 0), (-1, 1),
        (0, -1),          (0, 1),
        (1, -1),  (1, 0), (1, 1)
    };

    public override void OnEnable()
    {
        _terrain = Actor as Terrain;
        Debug.Log($"TerrainChunkEdgeLodLock::OnEnable - Terrain actor = {(_terrain != null ? "OK" : "NULL")}");

        _lastLockedSet.Clear();
        _currentLockedSet.Clear();
        _lastCenterChunk = null;
        _cachedChunkSide = -1;

        if (_terrain == null)
        {
            Debug.LogError("TerrainChunkEdgeLodLock: Script must be attached to Terrain Actor!");
            return;
        }
        if (CameraActor)
        {
            if (PrintLockDebugLog) Debug.Log($"TerrainChunkEdgeLodLock::OnEnable - CameraActor assigned: {CameraActor.Name}");
        }
    }

    public override void OnDisable()
    {
        if (PrintLockDebugLog) Debug.Log("TerrainChunkEdgeLodLock::OnDisable -> Unlock all locked chunks");
		
		ClearSet(_lastLockedSet);
		_currentLockedSet.Clear();   // 只清集合，不动 ScriptForcedLOD
		_lastCenterChunk = null;
		_cachedChunkSide = -1;		
		
    }
	
	private Actor GetEffectiveCamera()
	{
		// 优先使用编辑器指定的相机
		if ( CameraActor && CameraActor.IsActive)
			return CameraActor;
		
		// 回退到引擎主相机
		return Camera.MainCamera;
	}

    public override void OnUpdate()
    {
        if (_terrain == null)
			return;
		
		var cam = GetEffectiveCamera();
		if (cam == null)
			return;

        _timer += Time.DeltaTime;
        if (_timer < TickInterval)
            return;
        _timer = 0;

        Vector3 camPos = cam.Position;
        TerrainChunk centerChunk = _terrain.GetChunkAt(camPos);

        _currentLockedSet.Clear();

        if (centerChunk == null)
        {
            if (PrintLockDebugLog) Debug.Log($"GetChunkAt({camPos}) -> NULL");
            ClearSet(_lastLockedSet);
            _lastCenterChunk = null;
            return;
        }

        int baseLod = centerChunk.CachedDrawLOD;

        TerrainPatch cp = centerChunk.GetPatch();
        if (cp != null)
        {
            int derived = DeriveChunkSide(cp.ChunksPerPatch);
            if (derived > 0)
                _cachedChunkSide = derived;
        }

        int foundNeighborCount = 0;
        foreach (var offset in _neighborOffsets)
        {
            var neighbor = GetRelativeChunk(centerChunk, offset.dx, offset.dz);
            if (neighbor != null)
            {
                _currentLockedSet.Add(neighbor);
                foundNeighborCount++;
            }
        }
		
        if (PrintLockDebugLog)
        {
            // 合并为一条 Tick 摘要
            Debug.Log($"[Tick] cam=({camPos.X:F0},{camPos.Z:F0}) center=({centerChunk.GetX()},{centerChunk.GetZ()}) patch=({cp?.PatchX},{cp?.PatchZ}) base={baseLod} found={foundNeighborCount} cur={_currentLockedSet.Count} last={_lastLockedSet.Count}");

            var (nativeLine, cachedLine, forcedLine) = BuildAllChunkLodSnapshot();
            Debug.Log(nativeLine);
            Debug.Log(cachedLine);
            Debug.Log(forcedLine);
		}

        ApplySetDiff(_currentLockedSet, baseLod);
        SwapSets();

        _lastCenterChunk = centerChunk;
    }

    private void SwapSets()
    {
        var temp = _lastLockedSet;
        _lastLockedSet = _currentLockedSet;
        _currentLockedSet = temp;
    }


    private void ApplySetDiff(HashSet<TerrainChunk> currentSet, int baseLod)
    {
        if (currentSet == null) return;

        var sbKeep = new System.Text.StringBuilder(64);
        var sbChanged = new System.Text.StringBuilder(64);

        foreach (var chunk in currentSet)
        {
            if (chunk == null) continue;

            bool isNew = !_lastLockedSet.Contains(chunk);
            int readBack = chunk.ScriptForcedLOD;

            if (readBack != baseLod)
            {
                chunk.ScriptForcedLOD = baseLod;
                int newReadBack = chunk.ScriptForcedLOD;
                sbChanged.Append($"{(isNew ? "LOCK" : "RE-LOCK")}({chunk.GetX()},{chunk.GetZ()})->{baseLod}(rb{newReadBack}) ");
            }
            else if (!isNew)
            {
                sbKeep.Append($"({chunk.GetX()},{chunk.GetZ()}) ");
            }
        }

        foreach (var chunk in _lastLockedSet)
        {
            if (chunk == null) continue;
            if (currentSet.Contains(chunk)) continue;

            chunk.ScriptForcedLOD = -1;
            sbChanged.Append($"UNLOCK({chunk.GetX()},{chunk.GetZ()}) ");
        }

        if (PrintLockDebugLog)
        {
            if (sbChanged.Length > 0) Debug.Log($"[Diff] {sbChanged}");
            if (sbKeep.Length > 0)    Debug.Log($"[Keep] {sbKeep}");
        }
    }

    private void ClearSet(HashSet<TerrainChunk> set)
    {
        if (set == null) return;

        foreach (var c in set)
        {
            if (c == null) continue;
            c.ScriptForcedLOD = -1;
        }

        if (PrintLockDebugLog)
            Debug.Log($"CLEAR {set.Count} chunks");

        set.Clear();
    }

    /// <summary>
    /// 从 ChunksPerPatch 推导单边 Chunk 数（如 16 -> 4）
    /// </summary>
    private static int DeriveChunkSide(int chunksPerPatch)
    {
        if (chunksPerPatch <= 0) return -1;
        int side = (int)Math.Round(Math.Sqrt(chunksPerPatch));
        if (side * side != chunksPerPatch) return -1;
        return side;
    }

    /// <summary>
    /// Get neighbor chunk by dx,dz offset, cross patch boundary automatically
    /// dx,dz ∈ {-1,0,1}
    /// </summary>
    private TerrainChunk GetRelativeChunk(TerrainChunk srcChunk, int dx, int dz)
    {
        if (srcChunk == null) return null;

        int srcLocalCx = srcChunk.GetX();
        int srcLocalCz = srcChunk.GetZ();

        TerrainPatch srcPatch = srcChunk.GetPatch();
        if (srcPatch == null)
        {
            Debug.LogWarning($"GetRelativeChunk: srcPatch NULL! srcChunk local X:{srcLocalCx},Z:{srcLocalCz}");
            return null;
        }

        int chunkSide = _cachedChunkSide;
        if (chunkSide <= 0)
        {
            chunkSide = DeriveChunkSide(srcPatch.ChunksPerPatch);
            if (chunkSide <= 0)
            {
                Debug.LogError($"GetRelativeChunk: cannot derive chunkSide from ChunksPerPatch={srcPatch.ChunksPerPatch}");
                return null;
            }
            _cachedChunkSide = chunkSide;
        }

        int globalCx = srcPatch.PatchX * chunkSide + srcLocalCx;
        int globalCz = srcPatch.PatchZ * chunkSide + srcLocalCz;

        int targetGlobalCx = globalCx + dx;
        int targetGlobalCz = globalCz + dz;

        int targetPatchX = FloorDiv(targetGlobalCx, chunkSide);
        int targetCx = targetGlobalCx - targetPatchX * chunkSide;

        int targetPatchZ = FloorDiv(targetGlobalCz, chunkSide);
        int targetCz = targetGlobalCz - targetPatchZ * chunkSide;

        if (PrintCalcDebugLog)
            Debug.Log($"[Calc] +({dx},{dz}) P({targetPatchX},{targetPatchZ}) L({targetCx},{targetCz})");

        var targetPatch = _terrain.GetPatch(targetPatchX, targetPatchZ);
        if (targetPatch == null)
        {
            if (PrintCalcDebugLog)
                Debug.LogWarning($"GetRelativeChunk: targetPatch NULL P({targetPatchX},{targetPatchZ})");
            return null;
        }

        if (targetCx < 0 || targetCx >= chunkSide || targetCz < 0 || targetCz >= chunkSide)
        {
            Debug.LogWarning($"GetRelativeChunk: local ({targetCx},{targetCz}) out of range [0,{chunkSide})");
            return null;
        }

        var resultChunk = targetPatch.GetChunk((ushort)targetCx, (ushort)targetCz);
        if (resultChunk == null)
        {
            if (PrintCalcDebugLog)
                Debug.LogWarning($"GetRelativeChunk: target chunk null P({targetPatchX},{targetPatchZ}) L({targetCx},{targetCz})");
        }
        return resultChunk;
    }

    /// <summary>
    /// 真正的地板除，负数也正确（C# '/' 是向零取整）
    /// </summary>
    private static int FloorDiv(int a, int b)
    {
        int q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0)))
            q--;
        return q;
    }

    // 三行 LOD 快照：Native / Cached / Forced
    // 约定：'-' = Forced 为 -1，'x' = 取不到，'?' = 值不在 0-9
    // Native = 本帧原生 SSE 结果（视锥外块被 DrawSetup 重置为 0，不代表真值）
    // Cached = 最终生效值
    // Forced = 脚本锁定值
    private (string nativeLine, string cachedLine, string forcedLine) BuildAllChunkLodSnapshot()
    {
        if (_terrain == null)
            return ("[NativeLOD] terrain null", "[CachedLOD] terrain null", "[ForcedLOD] terrain null");

        int patchCount = _terrain.PatchesCount;
        int chunksPerPatch = Terrain.PatchChunksCount;
        int capacity = patchCount * chunksPerPatch + 16;

        var sbNative = new System.Text.StringBuilder(capacity);
        var sbCached = new System.Text.StringBuilder(capacity);
        var sbForced = new System.Text.StringBuilder(capacity);

        sbNative.Append("[NativeLOD] ");
        sbCached.Append("[CachedLOD] ");
        sbForced.Append("[ForcedLOD] ");

        for (int p = 0; p < patchCount; p++)
        {
            TerrainPatch patch = _terrain.GetPatch(p);
            if (patch == null)
            {
                for (int c = 0; c < chunksPerPatch; c++) { sbNative.Append('x'); sbCached.Append('x'); sbForced.Append('x'); }
                continue;
            }

            for (int c = 0; c < chunksPerPatch; c++)
            {
                TerrainChunk chunk = patch.GetChunk(c);
                if (chunk == null)
                {
                    sbNative.Append('x'); sbCached.Append('x'); sbForced.Append('x');
                    continue;
                }

                int native = chunk.NativeComputedLOD;
                sbNative.Append(native >= 0 && native <= 9 ? (char)('0' + native) : '?');

                int cached = chunk.CachedDrawLOD;
                sbCached.Append(cached >= 0 && cached <= 9 ? (char)('0' + cached) : '?');

                int forced = chunk.ScriptForcedLOD;
                if (forced == -1) sbForced.Append('-');
                else if (forced >= 0 && forced <= 9) sbForced.Append((char)('0' + forced));
                else sbForced.Append('?');
            }
        }

        return (sbNative.ToString(), sbCached.ToString(), sbForced.ToString());
    }

}