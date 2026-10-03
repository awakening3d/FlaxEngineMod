using FlaxEngine;
using System;

namespace FlaxEditor.Tools.Terrain.Paint
{
    using Terrain = FlaxEngine.Terrain;

    public static class TerrainSplatmapImporter
    {
        // 返回 null = 成功，非 null = 错误信息
        public static string Import(SingleLayerMode mode, string filePath)
        {
            var terrain = GetSelectedTerrain();
            if (terrain == null)
            {
                LogError(filePath, "No terrain selected.");
                return "No terrain selected.";
            }

            // 引擎解码：拿原始像素 + 格式 + 尺寸
            // Flax 惯例：返回 true = 失败
            if (Editor.DecodeTextureFileToPixels(filePath, out byte[] pixels, out PixelFormat format, out int width, out int height))
            {
                LogError(filePath, "Failed to decode texture file.");
                return $"Failed to decode file: {filePath}";
            }

            int targetLayer = (int)mode.Layer;
            string error = ApplyToTerrain(terrain, pixels, format, width, height, targetLayer);

            if (error != null)
            {
                LogError(filePath, error);
                return error;
            }

            // 成功日志
            Debug.Log($"[SplatmapImport] Success: '{System.IO.Path.GetFileName(filePath)}' -> Layer {targetLayer} ({width}x{height}, {format})");
            return null;
        }

        private static string ApplyToTerrain(Terrain terrain, byte[] pixels, PixelFormat format, int srcW, int srcH, int targetLayer)
        {
            // 格式支持检查
            if (!IsFormatSupported(format))
            {
                return $"Unsupported pixel format: {format}. Supported: R8_UNorm, R16_UNorm, R8G8B8A8_UNorm, R8G8B8A8_UNorm_sRGB";
            }

            int heightmapSize = terrain.HeightmapSize;   // 509

            // 单轴 patch 数
            int patchGridX = 1, patchGridZ = 1;
            int patchCount = terrain.PatchesCount;
            for (int i = 0; i < patchCount; i++)
            {
                terrain.GetPatchCoord(i, out var c);
                if (c.X + 1 > patchGridX) patchGridX = c.X + 1;
                if (c.Y + 1 > patchGridZ) patchGridZ = c.Y + 1;
            }

            float uvPerPatchX = 1.0f / patchGridX;
            float uvPerPatchZ = 1.0f / patchGridZ;
            float inv = 1.0f / (heightmapSize - 1);   // 1/508

            int splatmapIndex = targetLayer < 4 ? 0 : 1;
            int channelIndex = targetLayer % 4;

            for (int p = 0; p < patchCount; p++)
            {
                terrain.GetPatchCoord(p, out var patchCoord);

                float uvStartX = patchCoord.X * uvPerPatchX;
                float uvStartZ = patchCoord.Y * uvPerPatchZ;

                unsafe
                {
                    Color32* existing = TerrainTools.GetSplatMapData(terrain, ref patchCoord, splatmapIndex);
                    if (existing == null)
                        return $"Failed to read splatmap for patch ({patchCoord.X},{patchCoord.Y})";

                    var newData = new Color32[heightmapSize * heightmapSize];
                    for (int z = 0; z < heightmapSize; z++)
                    {
                        for (int x = 0; x < heightmapSize; x++)
                        {
                            // UV 归一化（和引擎 GenerateTerrain 一致）
                            float u = uvStartX + x * inv * uvPerPatchX;
                            float v = uvStartZ + z * inv * uvPerPatchZ;

                            byte value = SampleBilinear(pixels, format, srcW, srcH, u, v);

                            // 只替换目标通道，其他通道保留原值
                            var src = existing[z * heightmapSize + x];
                            switch (channelIndex)
                            {
                                case 0: src.R = value; break;
                                case 1: src.G = value; break;
                                case 2: src.B = value; break;
                                case 3: src.A = value; break;
                            }
                            newData[z * heightmapSize + x] = src;
                        }
                    }

                    fixed (Color32* ptr = newData)
                    {
                        var offset = Int2.Zero;
                        var size = new Int2(heightmapSize, heightmapSize);   // 509
                        if (TerrainTools.ModifySplatMap(terrain, ref patchCoord, splatmapIndex, ptr, ref offset, ref size))
                            return $"Failed to write patch ({patchCoord.X},{patchCoord.Y})";
                    }
                }
            }

            return null;   // 成功
        }

        private static bool IsFormatSupported(PixelFormat format)
        {
            switch (format)
            {
                case PixelFormat.R8_UNorm:
                case PixelFormat.R16_UNorm:
                case PixelFormat.R8G8B8A8_UNorm:
                case PixelFormat.R8G8B8A8_UNorm_sRGB:
                    return true;
                default:
                    return false;
            }
        }

        private static void LogError(string filePath, string message)
        {
            Debug.LogError($"[SplatmapImport] Failed: '{System.IO.Path.GetFileName(filePath)}' - {message}");
        }

        // 双线性采样，uv ∈ [0,1]
        private static byte SampleBilinear(byte[] pixels, PixelFormat format, int w, int h, float u, float v)
        {
            // 夹紧防止越界
            u = Mathf.Clamp(u, 0f, 1f);
            v = Mathf.Clamp(v, 0f, 1f);

            float fx = u * (w - 1);
            float fy = v * (h - 1);
            int x0 = (int)Math.Floor(fx);
            int y0 = (int)Math.Floor(fy);
            int x1 = Math.Min(x0 + 1, w - 1);
            int y1 = Math.Min(y0 + 1, h - 1);
            float dx = fx - x0;
            float dy = fy - y0;

            byte p00 = SampleSingleChannel(pixels, format, w, x0, y0);
            byte p10 = SampleSingleChannel(pixels, format, w, x1, y0);
            byte p01 = SampleSingleChannel(pixels, format, w, x0, y1);
            byte p11 = SampleSingleChannel(pixels, format, w, x1, y1);

            float a = Mathf.Lerp(p00, p10, dx);
            float b = Mathf.Lerp(p01, p11, dx);
            return (byte)Mathf.Clamp(Mathf.Lerp(a, b, dy), 0, 255);
        }

        // 取单通道值（按 format 解析）
        private static byte SampleSingleChannel(byte[] pixels, PixelFormat format, int width, int x, int y)
        {
            int idx = y * width + x;
            switch (format)
            {
                case PixelFormat.R8_UNorm:
                    return pixels[idx];
                case PixelFormat.R16_UNorm:
                    {
                        int i = idx * 2;
                        ushort v16 = (ushort)(pixels[i] | (pixels[i + 1] << 8));
                        return (byte)(v16 >> 8);   // 16 位降到 8 位
                    }
                case PixelFormat.R8G8B8A8_UNorm:
                case PixelFormat.R8G8B8A8_UNorm_sRGB:
                    return pixels[idx * 4];        // 取 R
                default:
                    return 0;
            }
        }

        private static Terrain GetSelectedTerrain()
        {
            var sceneEditing = Editor.Instance.SceneEditing;
            if (sceneEditing.SelectionCount != 1)
                return null;

            var node = sceneEditing.Selection[0] as FlaxEditor.SceneGraph.Actors.TerrainNode;
            if (node == null)
                return null;

            return (Terrain)node.Actor;
        }
    }
}
