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


================================================================================
2026.10.18 Flax Engine 源码改动总结
================================================================================

一、改动文件列表
--------------------------------------------------------------------------------
1. Source/Editor/Viewport/EditorViewport.cs
2. Source/Editor/Options/InputOptions.cs


二、Source/Editor/Viewport/EditorViewport.cs
--------------------------------------------------------------------------------
目的：
  调整相机键盘移动（W/S/A/D/E/Q）的触发条件，实现：
  - 鼠标松开时：键盘移动生效
  - 右键按住时：键盘移动生效
  - 左键 / 中键 / Alt+LMB 按住时：键盘移动不生效

改动内容：

1) 新增私有方法 GetKeyboardMoveDelta(Window win, InputOptions options)
   统一处理 W/S/A/D/Up/Down 检测，并做：
   - Normalize（带 LengthSquared > Mathf.Epsilon 保护）
   - 乘以 _movementSpeed
   - Shift 加速 x4，Ctrl 减速 x0.3

   代码：

   private Vector3 GetKeyboardMoveDelta(Window win, InputOptions options)

2) if (_isControllingMouse) 分支（鼠标操控相机时）
   原来：无条件检测 W/S，导致按住鼠标时键盘也移动相机。
   现在：仅当右键（或虚拟右键）按住、且左键未按住时，才调用 GetKeyboardMoveDelta。

   代码：

   // Get input movement（仅右键按下时响应键盘移动）
   var moveDelta = Vector3.Zero;
   bool rbDownForMove = (_input.IsMouseRightDown || _isVirtualMouseRightDown)
                        && !win.GetMouseButton(MouseButton.Left);
   if (rbDownForMove)
       moveDelta = GetKeyboardMoveDelta(win.Window, options.Input);

3) else 分支（鼠标未控制相机时）
   替换原来重复的 W/S 检测为调用 GetKeyboardMoveDelta，并用左键状态屏蔽。

   代码：

   // 原方向键/手柄检测保留
   ...
   if (!win.GetMouseButton(MouseButton.Left))
       moveDelta += GetKeyboardMoveDelta(win.Window, options.Input);

   说明：
   - 使用 win.GetMouseButton(MouseButton.Left) 直接读窗口鼠标状态，
     避免 _input 在 else 分支被 Clear() 或 useMouse=false 时状态丢失。
   - 当前仅屏蔽左键；如需更严格，可扩展为 Left/Right/Middle 任一按下都屏蔽。


三、Source/Editor/Options/InputOptions.cs
--------------------------------------------------------------------------------
目的：
  避免 Ctrl+W 误关窗口（相机飞行时 Ctrl 减速 + W 前进会触发关闭），
  同时避免 Ctrl+D 与常用操作冲突。

改动内容：

1) CloseTab（Interface 分组，EditorOrder 3500）
   原：
   [DefaultValue(typeof(InputBinding), "Ctrl+W")]
   [EditorDisplay("Interface"), EditorOrder(3500)]
   public InputBinding CloseTab = new InputBinding(KeyboardKeys.W, KeyboardKeys.Control);

   改为：
   [DefaultValue(typeof(InputBinding), "Ctrl+Alt+W")]
   [EditorDisplay("Interface"), EditorOrder(3500)]
   public InputBinding CloseTab = new InputBinding(KeyboardKeys.W, KeyboardKeys.Control, KeyboardKeys.Alt);

2) Duplicate（Common 分组，EditorOrder 150）
   原：
   [DefaultValue(typeof(InputBinding), "Ctrl+D")]
   [EditorDisplay("Common"), EditorOrder(150)]
   public InputBinding Duplicate = new InputBinding(KeyboardKeys.D, KeyboardKeys.Control);

   改为：
   [DefaultValue(typeof(InputBinding), "Ctrl+Alt+D")]
   [EditorDisplay("Common"), EditorOrder(150)]
   public InputBinding Duplicate = new InputBinding(KeyboardKeys.D, KeyboardKeys.Alt, KeyboardKeys.Control);

说明：
  - 修改后，Ctrl+W 不再关闭当前标签页，需 Ctrl+Alt+W。
  - Ctrl+D 不再触发 Duplicate，需 Ctrl+Alt+D。
  - 如果项目里已保存过旧的输入配置（ProjectCache / Options），
    可能需要在编辑器 Options 里手动重置或重新保存，新默认值才会生效。


四、未改动但相关的文件（仅阅读分析）
--------------------------------------------------------------------------------
1. Source/Editor/Viewport/Cameras/FPSCamera.cs
   - UpdateView 中 Dolly 段：
     if (input.IsPanning || input.IsMoving || input.IsRotating)
     {
         Vector3.Transform(ref moveDelta, ref rotation, out Vector3 move);
         position += move;
     }
   - 决定 moveDelta 如何被应用到相机位置。

2. Source/Editor/Options/InputOptions.cs 中的其他绑定（未改）：
   - Forward = W
   - Backward = S
   - Left = A
   - Right = D
   - Up = E
   - Down = Q


五、最终行为总结
--------------------------------------------------------------------------------
相机键盘移动（EditorViewport.cs）：
  - 鼠标松开：W/S/A/D/E/Q 生效
  - 右键按住：W/S/A/D/E/Q 生效
  - 左键按住：不生效
  - 中键按住：不生效
  - Alt+LMB：不生效
  - 右键+左键同时：不生效（rbDownForMove 中 !Left 条件）
  - Shift 加速 x4，Ctrl 减速 x0.3

快捷键（InputOptions.cs）：
  - 关闭标签页：Ctrl+Alt+W（原 Ctrl+W）
  - 复制对象：Ctrl+Alt+D（原 Ctrl+D）


六、注意事项
--------------------------------------------------------------------------------
1. InputOptions 默认值修改后，若编辑器已缓存旧配置，可能需要在
   Options -> Input 中手动恢复默认或重新保存。
2. EditorViewport.cs 的 else 分支目前只屏蔽左键；若希望中键也屏蔽，
   可将条件扩展为：
   !win.GetMouseButton(MouseButton.Left) &&
   !win.GetMouseButton(MouseButton.Middle)
3. 若后续仍需在代码层屏蔽 Ctrl+W，可全局搜索 "CloseTab" 定位
   DockWindow.cs / DockPanel.cs 中的实际处理点。

================================================================================


================================================================================
2026.10.19 Flax Engine 源码改动总结
Terrain Patch 接缝修复改动说明
================================

一、修改文件清单
----------------
1. Terrain.h
2. Terrain.cpp
3. TerrainPatch.h
4. TerrainPatch.cpp

二、核心改动
------------

1. Terrain.h
   新增全局高度锁定相关成员:
   - bool _global_height_range
   - float _global_yOffset
   - float _global_yHeight
   - Dictionary<Int2, Array<float>> _global_heights
   新增函数声明:
   - RepairHeightmapEdgePixel()

2. Terrain.cpp
   - 新增 RepairHeightmapEdge():
     遍历所有 Patch 的绝对高度快照, 对相邻 Patch 的 X/Z 接缝和四 Patch 角点取平均,
     计算全局最小高度和最大高度, 锁定全局高度参数, 调用 ModifyHeightMap 写回,
     调用 RepairHeightmapEdgePixel 修补法线, 最后强制 SaveHeightData 落盘.
   - 新增 RepairHeightmapEdgePixel():
     从 CPU 端 _dataHeightmap 读取最新像素, 对相邻 Patch 的边界像素高度(RG)和法线(BA)
     做平均, 然后调用 OverwriteEncodedHeightmapFull 写回并上传到 GPU.

3. TerrainPatch.h
   新增声明:
   - bool GetEncodedHeightmapPixels(Array<Color32>& outPixels)

4. TerrainPatch.cpp
   - CalculateHeightmapRange():
     末尾增加判断, 若 _global_height_range 为 true, 则覆盖局部计算结果,
     强制使用全局 _global_yOffset 和 _global_yHeight.
   - 新增 GetEncodedHeightmapPixels():
     直接从 CPU 端 _dataHeightmap 拷贝最新像素, 避免 GPU 异步回读旧数据.

三、已知问题
------------
1. LOD > 0 时 Patch 边界法线可能仍有极细微不一致. FixMips 不处理跨 Patch 边界.
   视觉难以察觉, 暂不处理.
2. 手动保存有时不生效. 已通过在 RepairHeightmapEdge 末尾强制 SaveHeightData 缓解,
   根本原因待排查.

=================================================================================
Flax 引擎源码修改记录
日期: 2026-10-27
描述: 核心 GUI 事件处理修复

============================================================
1. Source/Engine/UI/GUI/ContainerControl.cs
============================================================
[修改内容] 重写了 OnMouseMove 以及鼠标/触摸/拖拽事件的处理逻辑, 强制执行严格的遮挡(命中测试阻断)。
[目的] 修复重叠的 Visject 节点(或任何 UI 控件)仍然会响应鼠标事件穿透到最上层控件的问题。
[细节]
- OnMouseMove: 从广播模式改为 "寻找最上层命中的控件, break, 并清理未命中控件的状态"。
- OnMouseDown, OnMouseUp, OnMouseWheel, OnMouseDoubleClick, OnTouchDown, OnTouchUp, OnTouchMove, OnDragMove, OnDragDrop: 在控件命中后立即添加 "return false" 或 "break" 以防止事件穿透。

============================================================
2. Source/Editor/Surface/VisjectSurface.Input.cs
============================================================
[修改内容 1] 禁用了 "自动将节点插入连线" 的功能。
[目的] 防止在拖拽节点靠近已有连线时, 节点自动吸附进去并推开其他节点。
[细节] 在 OnMouseUp 中注释掉了大段的 "if (_movingNodes.Count == 1 ...) { ... }" 代码块。

[修改内容 2] 实现了选中节点时 "置于顶层"。
[目的] 修复点击重叠节点时选中了底层节点的问题。
[细节] 在 OnMouseDown 中, 当节点被选中时, 将其从 _rootControl.Children 中移除并重新添加到末尾, 以提升其 Z 序。

============================================================
3. Source/Editor/Surface/SurfaceNode.cs
============================================================
[修改内容] 修改了 CanSelect 方法。
[目的] 让节点主体区域也能被点击选中, 而不再局限于标题栏。
[细节]
- 原版: return _headerRect.MakeOffsetted(Location).Contains(ref location);
- 改后: return Bounds.Contains(ref location);
[状态] 已验证, 配合 VisjectSurface.Input 的置顶逻辑工作正常。

============================================================
4. Source/Editor/GUI/WindowDecorations.cs
============================================================
[修改内容] 重写了 IntersectsContent 方法。
[目的] 修复顶部 MainMenu 因事件被截断而无响应的问题。
[细节]
- 仅当鼠标命中图标、标题或窗口控制按钮(最小化/最大化/关闭)时返回 true。
- 对于透明的菜单区域和空白区域返回 false, 让事件穿透给 MainMenu。
- 使用 override 关键字覆盖基类方法。
- 使用 Bounds.Contains(locationParent) 进行父容器坐标空间的边界判定, 使用 location 进行子控件的局部坐标判定。
[状态] 已验证, 菜单响应正常, 事件不再穿透。

============================================================
已知状态
============================================================
- 所有修改的功能均已验证通过, 运行正常。
- ContainerControl 中 OnMouseEnter(广播) 与 OnMouseMove(截断) 之间存在语义不一致, 目前未暴露 Bug, 后续开发中如遇到相关问题再统一梳理。

=================================================================================
Flax 引擎源码修改记录
日期: 2026-10-28
描述: 增加splatmap导入格式支持

B8G8R8A8_UNorm:
B8G8R8X8_UNorm:
B8G8R8A8_UNorm_sRGB:
B8G8R8X8_UNorm_sRGB:

-------------------------------------------------------------------------------------------------------
1. Source\Editor\Tools\Terrain\Paint\TerrainSplatmapImporter.cs
-------------------------------------------------------------------------------------------------------

=================================================================================
Flax 引擎源码修改记录
日期: 2026-10-29
描述: 增加splatmap导出功能
-------------------------------------------------------------------------------------------------------
1. Source\Editor\Tools\Terrain\Paint\TerrainSplatmapImportEditor.cs
2. Source\Editor\Tools\Terrain\Paint\TerrainSplatmapImporter.cs
-------------------------------------------------------------------------------------------------------
=================================================================================
Flax 引擎源码修改记录
日期: 2026-10-30
描述: splatmap导出数据一致性修改，导出再导入像素位置一致，精度稍微有差异
-------------------------------------------------------------------------------------------------------
1. Source\Editor\Tools\Terrain\Paint\TerrainSplatmapImportEditor.cs
2. Source\Editor\Tools\Terrain\Paint\TerrainSplatmapImporter.cs
-------------------------------------------------------------------------------------------------------




