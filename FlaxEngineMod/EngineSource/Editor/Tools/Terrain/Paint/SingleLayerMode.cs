// Copyright (c) Wojciech Figat. All rights reserved.

using FlaxEngine;

namespace FlaxEditor.Tools.Terrain.Paint
{
    /// <summary>
    /// Paint tool mode. Edits terrain splatmap by painting with the single layer on top of the others.
    /// </summary>
    /// <seealso cref="FlaxEditor.Tools.Terrain.Paint.Mode" />
    [HideInEditor]
    public sealed class SingleLayerMode : Mode
    {
        /// <summary>
        /// The paint layers.
        /// </summary>
        public enum Layers
        {
            /// <summary>
            /// The layer 0.
            /// </summary>
            Layer0,

            /// <summary>
            /// The layer 0.
            /// </summary>
            Layer1,

            /// <summary>
            /// The layer 2.
            /// </summary>
            Layer2,

            /// <summary>
            /// The layer 3.
            /// </summary>
            Layer3,

            /// <summary>
            /// The layer 4.
            /// </summary>
            Layer4,

            /// <summary>
            /// The layer 5.
            /// </summary>
            Layer5,

            /// <summary>
            /// The layer 6.
            /// </summary>
            Layer6,

            /// <summary>
            /// The layer 7.
            /// </summary>
            Layer7,
        }

        /// <summary>
        /// The layer to paint with it.
        /// </summary>
        [EditorOrder(10), Tooltip("The layer to paint on. Terrain material can access a per-layer blend weight to perform material or texture blending."), CustomEditorAlias("FlaxEditor.CustomEditors.Editors.TerrainLayerEditor")]
        public Layers Layer = Layers.Layer0;

        [EditorOrder(15), Tooltip("import single-channel splatmap to current layer"), CustomEditorAlias("FlaxEditor.Tools.Terrain.Paint.TerrainSplatmapImportEditor")]
        public string SplatmapImport = "";

        /// <inheritdoc />
        public override int ActiveSplatmapIndex => (int)Layer < 4 ? 0 : 1;

        /// <inheritdoc />
        public override unsafe void Apply(ref ApplyParams p)
        {
            var strength = p.Strength;
            var layer = (int)Layer;
            var brushPosition = p.Gizmo.CursorPosition;
            var c = layer % 4;

            Profiler.BeginEvent("Apply Brush");
            bool otherModified = false;
            for (int z = 0; z < p.ModifiedSize.Y; z++)
            {
                var zz = z + p.ModifiedOffset.Y;
                for (int x = 0; x < p.ModifiedSize.X; x++)
                {
                    var xx = x + p.ModifiedOffset.X;
                    var src = (Color)p.SourceData[zz * p.HeightmapSize + xx];

                    var samplePositionLocal = p.PatchPositionLocal + new Vector3(xx * FlaxEngine.Terrain.UnitsPerVertex, 0, zz * FlaxEngine.Terrain.UnitsPerVertex);
                    Vector3.Transform(ref samplePositionLocal, ref p.TerrainWorld, out Vector3 samplePositionWorld);

                    var sample = Mathf.Saturate(p.Brush.Sample(ref brushPosition, ref samplePositionWorld));
                    var paintAmount = sample * strength;

                    if (paintAmount < 0.0f)
                        continue;

                    float terHeight = p.Terrain.SampleWorldHeightBilinear(samplePositionWorld.X, samplePositionWorld.Z);
                    if (terHeight < p.minHeight || terHeight > p.maxHeight) paintAmount = 0;

                    if (!p.surfaceOrientation.IsZero)
                    {
                        Vector3 surfaceNormal = p.Terrain.SampleWorldNormalBilinear(samplePositionWorld.X, samplePositionWorld.Z);
                        float dotv = Vector3.Dot(surfaceNormal, p.surfaceOrientation.Normalized);
                        dotv -= 0.5f;
                        if (dotv >= 0) paintAmount *= dotv;
                        if (dotv < 0) paintAmount = 0;
                    }

                    Color srcNew;
                    if (p.IsEraseMode)
                    {
                        // 擦除：只减当前层
                        srcNew = src;
                        srcNew[c] = Mathf.Saturate(src[c] - paintAmount);
                    }
                    else
                    {
                        // 旧逻辑：刷当前层，其他层按比例减（保留备份）
                        var srcOther = (Color)p.SourceDataOther[zz * p.HeightmapSize + xx];
                        var otherLayersSum = src.ValuesSum + srcOther.ValuesSum - src[c];
                        var decreaseAmount = otherLayersSum > 0.0001f ? paintAmount / otherLayersSum : 0f;
                        
                        srcNew = Color.Clamp(src - src * decreaseAmount, Color.Zero, Color.White);
                        srcNew[c] = Mathf.Saturate(src[c] + paintAmount);
                        
                        srcOther = Color.Clamp(srcOther - srcOther * decreaseAmount, Color.Zero, Color.White);
                        p.TempBufferOther[z * p.ModifiedSize.X + x] = srcOther;
                        otherModified = true;

                        // 新逻辑：只改当前层，其他层不动
                        //srcNew = src;
                        //srcNew[c] = Mathf.Saturate(src[c] + paintAmount);
                    }

                    p.TempBuffer[z * p.ModifiedSize.X + x] = srcNew;
                }
            }
            Profiler.EndEvent();

            TerrainTools.ModifySplatMap(p.Terrain, ref p.PatchCoord, p.SplatmapIndex, p.TempBuffer, ref p.ModifiedOffset, ref p.ModifiedSize);
            if (otherModified)
                TerrainTools.ModifySplatMap(p.Terrain, ref p.PatchCoord, p.SplatmapIndexOther, p.TempBufferOther, ref p.ModifiedOffset, ref p.ModifiedSize);
        }




    }
}
