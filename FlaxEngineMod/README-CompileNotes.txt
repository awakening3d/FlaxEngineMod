Flax Engine 个人定制修改完整记录（编译问题 + DX11修复 + 地形LOD/接缝全套改动）
一、初期：工程生成失败问题（无代码修改，环境层面）
问题现象
运行 GenerateProjectFiles.bat 一闪而过、无法生成sln，Flax.Build 构建中断。
根本原因
- 系统未安装独立 .NET 8 SDK，仅 VS 内置 SDK（未写入系统 PATH，Flax 无法调用）
- Git LFS 大文件未拉取，第三方依赖都是占位文本，构建校验失败
- 旧 Cache 缓存冲突 + 权限不足
最终解决步骤
1. 安装 .NET 8 SDK 并加入系统环境变量
2. 执行 git lfs install && git lfs pull 补全引擎二进制依赖
3. 删除引擎根目录 Cache 文件夹
4. 管理员权限运行 GenerateProjectFiles.bat 成功生成工程

---
二、第一轮引擎源码修改：DX11 编辑器绿屏修复（amd专属驱动Bug）
问题现象
DX11 模式下编辑器视口随机绿屏、画面崩坏、缓冲区垃圾数据输出。
修改文件清单（仅1个文件，无h文件改动）
- Source/Engine/GraphicsDevice/DirectX/DX11/GPUSwapChainDX11.cpp
具体改动点
- 重写 CreateSwapChain 交换链参数：关闭 AllowTearing、调整后备缓冲区数量、修改 SwapEffect
- 修改 Present() 函数 DXGI 提交 Flag，规避 AMD 驱动异常
- 修复 Resize 窗口缩放重建交换链逻辑，防止缩放后再次绿屏
备注：.h 文件完全未改动，无新增变量、无函数签名变更。

---
三、第二轮引擎源码修改：地形系统全套修复（LOD跳变 + Patch接缝）
问题现象
- 相机移动地形 Chunk LOD 硬切换、明显跳动
- Patch 与 Patch 之间出现台阶、黑线接缝
C++ 修改文件清单（h + cpp）
- Source/Engine/Terrain/TerrainChunk.h（新增LOD平滑成员变量）
- Source/Engine/Terrain/TerrainChunk.cpp（重写CalculateLOD、帧间平滑插值、边界LOD锁定）
- Source/Engine/Terrain/TerrainPatch.h（新增边界高度缓存成员）
- Source/Engine/Terrain/TerrainPatch.cpp（邻接Patch高度采样、缝合接缝）
- Source/Engine/Terrain/Terrain.h（新增全局LOD平滑参数API）
- Source/Engine/Terrain/Terrain.cpp（参数暴露、全局控制逻辑）
C# 编辑器源码修改文件清单（引擎Editor模块）
...
改动效果
- 地形LOD 无跳变、平滑过渡
- 彻底消除 Patch 接缝、台阶黑线
---

//-------------------------------------------------------------
四、第三轮引擎源码修改：

本轮围绕地形 Splatmap 导入功能展开，涉及引擎 C++ 侧、编辑器 C# 侧和编辑器 UI 三部分。核心目标是：让外部工具制作的单通道灰度图，能按指定图层导入到地形 Splatmap 中，同时支持编辑器内刷/擦图层。

4.1 引擎 C++ 侧修改

文件：Source/Editor/Managed/ManagedEditor.h
在 ManagedEditor 类中新增静态方法声明：

API_FUNCTION() static bool DecodeTextureFileToPixels(
const String& inputPath,
API_PARAM(Out) Array<byte>& pixels,
API_PARAM(Out) PixelFormat& format,
API_PARAM(Out) int32& width,
API_PARAM(Out) int32& height);

说明：暴露给 C# 的纹理解码接口，返回源文件原生格式的像素数据、格式和尺寸。返回值遵循 Flax 惯例，true 表示失败，false 表示成功。

文件：Source/Editor/Managed/ManagedEditor.cpp
新增头文件引用：

#include "Engine/Graphics/Textures/TextureData.h"
#include "Engine/Graphics/PixelFormatExtensions.h"

实现 DecodeTextureFileToPixels：
bool ManagedEditor::DecodeTextureFileToPixels(const String& inputPath, Array<byte>& pixels, PixelFormat& format, int32& width, int32& height);


说明：

调用 TextureTool::ImportTexture 无 Options 重载，保留源文件原生格式（8 位 PNG 为 R8_UNorm，16 位 PNG 为 R16_UNorm，RGB/RGBA PNG 为 R8G8B8A8_UNorm）。

拒绝压缩格式，因为无法按 width * pixelStride 计算每行字节。

将 mip0 数据从 RowPitch 带 padding 的布局，重新按紧密排列（tightRowBytes）拷贝到输出数组，便于 C# 侧解析。

4.2 编辑器 C# 侧修改

文件：Source/Editor/Tools/Terrain/Paint/TerrainSplatmapImporter.cs（新增）

命名冲突处理：
由于所在命名空间 FlaxEditor.Tools.Terrain.Paint 与类型 FlaxEngine.Terrain 重名，在 namespace 内部使用别名：

using Terrain = FlaxEngine.Terrain;

核心方法 Import：

关键点：

源图尺寸不做校验，采用 UV 归一化采样，与引擎 TerrainTools::GenerateTerrain 中的采样公式完全一致。

uvStart 为该 patch 在源图 UV 空间的左上角，uvPerPatch 为单 patch 覆盖的 UV 区间。

每个 patch 采样出 heightmapSize × heightmapSize（509×509）数据，只替换目标图层对应的通道，其他通道保留原值。

通过 TerrainTools.ModifySplatMap 写回，尺寸参数传 509。引擎内部会自动完成 509 到 512 的展开。

辅助方法：

IsFormatSupported：前置检查格式支持，不支持直接报错。

SampleBilinear：UV 归一化双线性采样，边缘夹紧。

SampleSingleChannel：按 PixelFormat 取单通道值，支持 R8_UNorm、R16_UNorm、R8G8B8A8_UNorm、R8G8B8A8_UNorm_sRGB。

GetSelectedTerrain：从编辑器选中项取 TerrainNode 并转换为 FlaxEngine.Terrain。

LogError：统一格式的失败日志输出。

4.3 编辑器 UI 修改

文件：Source/Editor/Tools/Terrain/Paint/TerrainSplatmapImportEditor.cs（新增）

自定义编辑器类，挂接到 SingleLayerMode.SplatmapImport 字段上，UI 显示为一行标签加一个 import 按钮：

说明：

CustomEditor 基类无 Owner 属性，通过 ParentEditor 向上遍历，找到 Values[0] 为 SingleLayerMode 的编辑器，即被编辑对象。

文件路径保存在 SingleLayerMode.SplatmapImport 字段中，下次打开对话框时作为初始目录。

失败时弹出 MessageBox 提示，成功时静默（日志由 TerrainSplatmapImporter 输出）。

文件：Source/Editor/Tools/Terrain/Paint/SingleLayerMode.cs（修改）

新增字段：

[EditorOrder(15), Tooltip("import single-channel splatmap to current layer"), CustomEditorAlias("FlaxEditor.Tools.Terrain.Paint.TerrainSplatmapImportEditor")]
public string SplatmapImport = "";

Apply 方法中新增擦除模式分支，新增 IsEraseMode 参数由 Mode.Apply 传入：

文件：Source/Editor/Tools/Terrain/Paint/Mode.cs（修改）

ApplyParams 结构新增字段：

public bool IsEraseMode;

Apply 方法中新增：

if (Input.GetKey(KeyboardKeys.Shift))
p.IsEraseMode = true;

说明：按住 Shift 时，画笔进入擦除模式。

4.4 关键技术结论

4.4.1 Splatmap 尺寸

单 patch 的逻辑数据尺寸为 chunkSize * 4 + 1 = 509，GPU 纹理尺寸为 (chunkSize + 1) * 4 = 512。两者相差 3，对应 4 个 chunk 之间的 3 个共享边界。509 布局中相邻 chunk 共享 1 列顶点，512 布局中每 chunk 独立占 128 列，共享边界的顶点在纹理里被复制一份。此展开由 TerrainPatch::UpdateSplatMap 内部完成，C# 侧只需按 509 操作。

4.4.2 源图尺寸无关

引擎 TerrainTools::GenerateTerrain 使用 UV 归一化采样，源图尺寸任意。导入逻辑照抄该公式，无需校验源图尺寸。

4.4.3 Flax 返回值语义

引擎 C++ 侧 API_FUNCTION 返回 bool 时，惯例为 true 表示失败，false 表示成功。C# 绑定保持一致。

4.4.4 单通道格式

无 Options 的 TextureTool::ImportTexture 重载保留源文件原生格式：8 位灰度 PNG 为 R8_UNorm，16 位灰度 PNG 为 R16_UNorm，RGB/RGBA PNG 为 R8G8B8A8_UNorm。导入工具按格式解析单通道值，16 位降为 8 位使用。

4.5 涉及文件汇总

C++ 侧：

Source/Editor/Managed/ManagedEditor.h

Source/Editor/Managed/ManagedEditor.cpp

C# 编辑器侧新增：

Source/Editor/Tools/Terrain/Paint/TerrainSplatmapImporter.cs

Source/Editor/Tools/Terrain/Paint/TerrainSplatmapImportEditor.cs

C# 编辑器侧修改：

Source/Editor/Tools/Terrain/Paint/SingleLayerMode.cs

Source/Editor/Tools/Terrain/Paint/Mode.cs

//-------------------------------------------------------------