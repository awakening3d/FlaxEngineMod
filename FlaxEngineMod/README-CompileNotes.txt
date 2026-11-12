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


=============================================================================
Flax 引擎功能扩展记录：具名变量节点（Named Variable）
日期：2026-10-31
描述：实现材质图中的具名变量系统，用于替代长距离连线，支持一处赋值、多处引用，并在材质生成阶段自动短路求值。
-------------------------------------------------------------------------------------------------------
1. Source/Editor/Surface/Archetypes/NamedVariable.cs
2. Source/Editor/Surface/NodeFactory.cs
3. Source/Editor/Surface/ResizableSurfaceNode.cs
4. Source/Engine/Visject/ShaderGraph.h
5. Source/Engine/Visject/ShaderGraph.cpp

------------------------------------------------------------------------------------------------------
1. 功能概述
------------------------------------------------------------------------------------------------------
在材质图中新增两种节点：
- Set Named Variable：接收一个输入值，并赋予唯一名字。
- Get Named Variable：通过下拉框选择名字，输出对应 Set 节点的值。

用户在图中仅通过名字关联，不显示实际连线。材质生成时，C++ 层将 Get 节点的求值短路到同名 Set 节点的输入源，不产生额外的代码或连线，保证性能与图面整洁。

------------------------------------------------------------------------------------------------------
2. 修改文件清单
------------------------------------------------------------------------------------------------------

[2.1] Source/Editor/Surface/Archetypes/NamedVariable.cs
说明：C# 编辑器层节点定义与交互逻辑。
改动：
- 注册 Set（TypeID 1）与 Get（TypeID 2）两种 NodeArchetype，GroupID 为 20。
- Set 节点：文本框输入名字，EditEnd 时提交；重名校验；改名时广播给所有 Get 节点同步。
- Get 节点：继承 ResizableSurfaceNode，支持水平拉伸；下拉框动态刷新；类型跟随 Set 输入 Box 实时同步。
- Set 节点改名时禁用 Undo 内部记录，并在操作结束后压入 EmptyUndoAction，避免 Undo 栈污染。
- Get 节点在切换下拉框时同样禁用 Undo 记录。
- 粘贴 Set 节点时，若名字冲突，自动追加 _1、_2 等后缀直到唯一。
- 输出类型同步：Get 节点的 OutputBox 类型由 Set 节点输入 Box 的连接类型决定；Set 的 ConnectionTick 会通知所有 Get 节点刷新类型。

[2.2] Source/Editor/Surface/NodeFactory.cs
说明：节点工厂注册。
改动：
- 在 DefaultGroups 末尾新增 GroupArchetype，GroupID = 20，Name = "Named Variables"。
- Archetypes 指向 NamedVariable.Nodes。

[2.3] Source/Engine/Visject/ShaderGraph.h
说明：ShaderGenerator 声明。
改动：
- 在 ProcessGroup* 声明列表中新增 ProcessGroupNamedVariable。
- 将 _perGroupProcessCall 类型从 FixedAllocation<17> 改为 FixedAllocation<32>，为后续扩展保留容量。
- 注意：FixedAllocation 是固定容量内联数组，原容量 17 无法容纳 GroupID 20，Resize(21) 在 Release 下会被静默截断为 17，导致越界访问空函数指针而崩溃。

[2.4] Source/Engine/Visject/ShaderGraph.cpp
说明：ShaderGenerator 实现。
改动：
- 构造函数中 Resize(17) 改为 Resize(21)。
- 注册 _perGroupProcessCall[20] 到 ProcessGroupNamedVariable。
- 新增 ProcessGroupNamedVariable 实现：
  - Set 节点：返回 Value::Zero（无输出）。
  - Get 节点：读取 Values[0].ToString() 作为变量名，在当前图的 Nodes 中查找同名 Set 节点。
  - 找到后取出 Set 节点的输入 Box（ID 0），若已连接则调用 eatBox 对上游求值。
  - eatBox 自带 Box 缓存，多个 Get 节点绑定同一 Set 时上游只求值一次，不会产生代码重复。
  - 找不到 Set 时通过 OnError 报错并返回 Value::Zero，防止崩溃。

------------------------------------------------------------------------------------------------------
3. 关键设计说明
------------------------------------------------------------------------------------------------------

[3.1] 图级别短路求值
Get 节点不生成任何 HLSL 代码。材质编译时，ProcessGroupNamedVariable 将 Get 节点替换为对 Set 节点输入的求值结果，等价于把 Get 的输出直接连到 Set 的输入源。用户图中仍保持无连线状态，生成结果与直接连线完全一致。

[3.2] 求值缓存
ShaderGenerator::eatBox 会对每个 Box 的求值结果缓存到 box->Cache。多个 Get 节点引用同一个 Set 时，上游函数的 HLSL 代码只会生成一次，下游共享同一个临时变量。

[3.3] 类型同步
Get 节点的 OutputBox 类型由 Set 节点输入 Box 的当前类型决定。Set 的 ConnectionTick 触发时，会遍历所有 Get 节点调用 UpdateOutputType，保证类型校验和连线时不会因类型不一致或空类型导致崩溃。

[3.4] Undo 策略
具名变量的改名和内部切换属于联动操作，不纳入 Undo 栈。Set 改名时临时禁用 Undo 记录，操作完成后压入 EmptyUndoAction 占位，避免用户按 Ctrl+Z 时撤销到不一致的中间状态。

[3.5] 唯一性校验
Set 节点改名时检查是否存在同名 Set，冲突则拒绝修改并回退到旧名字，同时输出警告日志。粘贴时若名字冲突，自动追加数字后缀直到唯一。

------------------------------------------------------------------------------------------------------
4. 调试经验记录
------------------------------------------------------------------------------------------------------

[4.1] 崩溃 0xC0000005 访问冲突于 0x0
原因：FixedAllocation<17> 容量不足以容纳 GroupID 20，Resize(21) 在 Release 下被截断为 17，_perGroupProcessCall[20] 越界读到空函数指针并被调用。
修复：将 FixedAllocation<17> 改为 FixedAllocation<32>，并确保 Resize(21)。

[4.2] 材质编译时 GroupID 20 未知导致崩溃
原因：ShaderGenerator 的 _perGroupProcessCall 未注册 GroupID 20 的处理函数。
修复：在 ShaderGenerator 构造函数中注册 ProcessGroupNamedVariable。

[4.3] C2440 无法从 T 转换为 String
原因：Variant 没有 AsString 方法，且不能隐式转换为 String。
修复：使用 Variant::ToString() 获取字符串。

[4.4] 节点拉伸时下拉框宽度不跟随
原因：未在 SizeChanged 中同步 ComboBox 宽度。
修复：重写 OnSizeChanged，按节点宽度设置 _combobox.Width。

[4.5] 节点改名导致 Get 节点选中项丢失
原因：Set 改名广播顺序错误，Get 节点在旧名字失效时被置空。
修复：在 Set 的 OnValuesChanged 中先广播 OnSetRenamed(oldName, newName)，再刷新下拉框。

[4.6] Set 改名卡顿
原因：TextBox.TextChanged 每次按键触发全图刷新。
修复：改用 TextBox.EditEnd 事件，仅在失焦或回车时提交。

============================================================
5. 后续可扩展方向
============================================================
- 支持跨层（Material Function 内部）的具名变量查找。
- 支持具名变量的默认值（Set 未连接时输出默认值）。
- 支持在 Visual Script 图和其他 Visject 图中复用同一套节点。
- 将命名变量存储为材质参数，实现运行时动态修改。



===========================================================
Flax 引擎与编辑器改动总结
日期：2026-11-01
-----------------------------------------------------------------------------------------------------
1. Source/Editor/Surface/Archetypes/NamedVariable.cs

-----------------------------------------------------------------------------------------------------
1. 材质节点图具名变量系统（Named Variable）后续完善

[1.1] 修复删除 Set 节点后 Get 节点下拉框未刷新
文件：Source/Editor/Surface/Archetypes/NamedVariable.cs
说明：
- 在 NamedVariableSetNode 中重写 OnDeleted。
- 当 Set 节点被删除时，遍历图中的所有 Get 节点。
- 直接调用 Get 节点的 UpdateCombo(deletedName)，将即将消失的名字作为过滤参数传入，使 Get 节点立即感知名字失效并置空。

[1.2] 支持 Undo 恢复 Set 节点后 Get 节点自动重连
文件：Source/Editor/Surface/Archetypes/NamedVariable.cs
说明：
- 在 NamedVariableSetNode 中重写 OnLoaded 和 OnSpawned。
- 当 Set 节点从文件加载或被 Undo 恢复时，主动遍历所有 Get 节点并调用 UpdateCombo()。
- Get 节点重新扫描当前图，找到恢复后的 Set 节点名字，自动恢复选中状态，实现状态一致性闭环。

[1.3] GetAllSetNames 增加忽略参数
文件：Source/Editor/Surface/Archetypes/NamedVariable.cs
说明：
- 修改 NamedVariable.GetAllSetNames，增加可选参数 ignoredName。
- 遍历节点收集名字时，跳过与 ignoredName 匹配的 Set 节点。
- 用于在 Set 删除瞬间过滤“幽灵节点”，避免 Get 节点误认为名字仍然存在。

----------------------------------------------------------------------------------
NamedVariable 模块状态说明
----------------------------------------------------------------------------------

文件: Source/Editor/Surface/Archetypes/NamedVariable.cs
状态: 功能可用, 暂时冻结 (不轻易改动 Undo 逻辑, 避免引入新 bug)

一, 设计概要
------------
1. Set 节点是命名变量的唯一数据源.
2. Get 节点通过名字查找 Set 节点, 同步输出类型和下拉列表.
3. 数据优先: 节点 Values[] 是真相, UI (TextBox / ComboBox) 跟随数据.
4. 三条广播路径:
   (1) Set 改名 -> OnSetRenamed 通知所有 Get.
   (2) Set 删除 -> UpdateCombo(deletedName) 通知所有 Get (清空选择).
   (3) Set 输入连接变化 -> UpdateOutputType 通知所有 Get.

二, 当前行为说明
----------------
1. 删除 Set 会清空匹配的 Get 选择 (UpdateCombo 带 ignoredName).
2. 撤销该删除会同时还原 Set 节点和 Get 节点的 Values[0], 于是 Get 自动重新选回被恢复的 Set.
   这是"数据优先 + Undo 还原"的必然结果, 本身不算 bug, 但暴露了下面的 Undo 不统一问题.

三, 已知隐患 (按优先级)
-----------------------
[P0] 删除路径 Undo 不统一
  OnDeleted -> UpdateCombo(deletedName) -> VariableName = string.Empty -> SetValue,
  未包 undo.Enabled = false, 而 OnTextBoxEditEnd 和 OnSelectedChanged 都包了.
  因此"删除"会在 Undo 栈里多留一条 EditNodeValuesAction.
  若 Surface.AddBatchedUndoAction 未把它与节点删除合并成单步,
  撤销可能只还原一半状态 (Set 回来但 Get 空着, 或 Get 选回但 Set 没回来).

[P0] 重复名只在编辑框路径硬拒
  OnTextBoxEditEnd 检测重名会 revert.
  但 OnValuesChanged (Undo 恢复, 代码 SetValue, OnSetRenamed 广播可达) 只 LogError 仍提交
  _lastCommittedName = newName.
  同名 Set 会导致 FindSetNode 返回第一个匹配 (Get 类型可能绑错),
  ComboBox 出现重复项, GetAllSetNames / UpdateCombo 语义歧义.

[P1] UpdateCombo 隐式改数据
  UpdateCombo 名义上是刷新 UI, 但当 currentName 不在候选集时执行 VariableName = string.Empty,
  带 SetValue 副作用 (压 Undo, MarkAsEdited, UpdateSize).
  不同调用点 (删除 / 加载 / 生成) 因 Undo 状态不同而行为不同.

[P2] ConnectionTick 全图广播
  任意 Set 的任意连接变化都会遍历全部节点刷新所有 Get 的 UpdateOutputType, 大图下 O(N^2).
  未按名字过滤, 功能正确但浪费.

[P2] _combobox / _textBox 获取时机依赖
  OnLoaded 依赖 GetChild<ComboBoxElement>() / Children 中已有控件.
  若调用顺序变化, UpdateCombo 的 if (_combobox != null) 会静默跳过 UI 刷新,
  表现为"加载后下拉框空但数据有值".

[P3] OnSetRenamed 与 OnValuesChanged 的锁交互
  OnSetRenamed 里 _isUpdateLocked = true 期间 VariableName = newName 仍走
  SetValue -> OnValuesChanged.
  重入目前靠 _isUpdateLocked 和 _isDuringValuesEditing 挡住, 但仅因当前无嵌套 SetValue,
  属"靠没有嵌套"而非机制保证, 较脆弱.

[P3] OnPasted 的 _textBox 可能为 null
  VariableName = newName 触发 OnValuesChanged 同步 _textBox.Text (非 null 时),
  随后又手动设一次, 重复无害.
  若 OnPasted 早于 OnLoaded, _textBox 为 null; OnLoaded 之后从 VariableName 补齐,
  一致性能保证但依赖调用顺序.

[P3] EmptyUndoAction 语义怪异
  重命名时禁用内部 Undo 再手动压一个空动作.
  用户表现为"撤销重命名无可见变化, 再撤一次才回到重命名前",
  与"删除可撤销"不一致, 可能困惑.

四, 建议修复顺序 (将来动 Undo 时)
--------------------------------
1. P0: 统一删除路径的 Undo 处理 (广播包 undo.Enabled = false, 或确认 AddBatchedUndoAction 合并单步).
2. P0: 让 OnValuesChanged 真正 revert 重复名 (或拒绝提交), 而不只是打日志.
3. P1: 拆分 UpdateCombo 为"只刷新 UI"与"显式改数据", 数据修改走 Undo 控制.
4. P2: ConnectionTick 按名字过滤, 避免全图扫描.
5. P2: 确认 OnLoaded 在元素创建后调用, 避免静默跳过.



===========================================================
Flax 引擎与编辑器改动总结
日期：2026-11-03
-----------------------------------------------------------------------------------------------------
1. Source/Editor/Surface/Archetypes/NamedVariable.cs
2. Source/Editor/GUI/Dialogs/ColorSelector.cs

1. NamedVariable - 修复重新打开材质后 Get 节点变量名丢失
--------------------------------------------------------
文件: NamedVariable.cs

问题:
  材质保存后重新打开, 某个 Get 节点有时会丢失已选的变量名 (Values[0] 被清空),
  即使对应的 Set 节点存在也找不回来。只有部分 Get 节点受影响。

根因:
  加载时, NamedVariableGetNode.OnLoaded 会调用 UpdateCombo() 重建下拉列表。
  如果某个 Get 节点比它依赖的 Set 节点先加载 (Surface.Nodes 的顺序不保证),
  此时目标名字不在候选列表里。旧版 UpdateCombo 会无条件走 else 分支:
      _combobox.SelectedItem = null;
      VariableName = string.Empty;   // 加载期间破坏性写入
  在 Set 节点还没加载出来前, 就把 Get 节点的数据清空了。
  等 Set 节点加载完再广播刷新, 名字已经没了。

修复:
  给 UpdateCombo 增加 allowClear 参数, 只有调用方明确允许时才清空底层数据。

  签名:
      public void UpdateCombo(string ignoredName = null, bool allowClear = false)

  逻辑:
      if (!string.IsNullOrEmpty(currentName) && names.Contains(currentName))
          _combobox.SelectedItem = currentName;
      else
      {
          _combobox.SelectedItem = null;
          if (allowClear)
              VariableName = string.Empty;   // 仅在明确允许时清空
      }

  调用点:
      - NamedVariableSetNode.OnDeleted -> UpdateCombo(deletedName, true)
        (Set 已删除, 清空匹配的 Get 是预期行为)
      - NamedVariableGetNode.OnLoaded  -> UpdateCombo()
        (allowClear = false, 保留数据, 等待 Set 加载)
      - NamedVariableSetNode.OnLoaded / OnSpawned 广播 -> UpdateCombo()
        (allowClear = false)
      - NamedVariableGetNode.OnSetRenamed -> UpdateCombo()
        (allowClear = false)

结果:
  无论节点加载顺序如何, Get 节点都不会再丢失已选变量名。
  删除路径仍然按预期清空匹配的 Get 选择。


2. ColorSelector - 修复松手时光标跳到屏幕左上角
------------------------------------------------
文件: ColorSelector.cs

问题:
  在色轮或明度/透明度滑块上拖动并松开鼠标左键后, 光标会跳到屏幕左上角 (0, 0)。

根因:
  ColorSelector 和 ColorSelectorWithSliders 的 OnMouseUp 里强行设置了光标位置:
      Root.MousePosition = dragRect.Center + MouseCursorOffset;
  以及
      Root.MousePosition = new Float2(sliderCenter + MouseCursorOffset,
                                      Mathf.Clamp(...));
  Root.MousePosition 期望的是屏幕坐标, 但传入的是控件局部坐标。
  坐标系统不匹配导致光标被放到接近全局原点的位置。

修复:
  注释掉这两处 Root.MousePosition 赋值:
      // Root.MousePosition = dragRect.Center + MouseCursorOffset;
      // Root.MousePosition = new Float2(sliderCenter + MouseCursorOffset, ...);
  保留其余松手逻辑 (还原 Cursor 为 Default, 清除滑动标志, EndMouseCapture,
  EndSliding) 不变。

取舍:
  移除了原有的"松手时光标吸附回把手"行为。光标停留在用户松开鼠标的位置,
  符合大多数用户的预期。

受影响位置:
  - ColorSelector.OnMouseUp            (色轮松手)
  - ColorSelectorWithSliders.OnMouseUp (明度滑块 / 透明度滑块松手)


3. 备注
-------
  - 两处修复都是最小改动, 不触碰无关逻辑。
  - NamedVariable 的修复只放宽了加载路径, 删除路径行为保持不变。
  - ColorSelector 的修复只移除错误的光标重定位, 拖拽、绘制、数值更新逻辑均未改动。


===========================================================
Flax 编辑器视口 Debug 绘制修复
日期：2026-11-04

-----------------------------------------------------------------------------------------------------
1.  Source/Editor/Viewport/MainEditorGizmoViewport.cs


问题描述
--------
场景中放置了 PostFxVolume (后处理体积) 后, 即使该体积没有被选中,
编辑器视口也会一直绘制它的包围盒线框。该线框在调色、光照调试时严重干扰视觉。

在 MainEditorGizmoViewport.cs 的 DrawEditorPrimitives 中定位到根源:
       _debugDrawData.DrawActors(true);
   该调用传入 true, 会连同当前场景中所有加载的 Actor 一并交给底层
   DebugDraw 渲染, 导致未选中的 PostFxVolume 也会被底层强行画出包围盒。

修复内容
--------
文件: MainEditorGizmoViewport.cs
方法: DrawEditorPrimitives

_debugDrawData.DrawActors(true);
改为
_debugDrawData.DrawActors(false);

说明:
    将 DrawActors 的 drawScenes 参数由 true 改为 false。
    这样 DebugDraw 只渲染 _debugDrawData 中收集到的 Actor
    (即在 OnBegin 中从 TransformGizmo.SelectedParents 收集的选中对象),
    不再遍历整个场景中的全部 Actor。


修复效果
--------
- 未选中的 PostFxVolume 不再显示包围盒。
- 选中 PostFxVolume 时仍正常显示包围盒。
- 附带性能收益: 不再每帧遍历全场景 Actor 进行 Debug 绘制。



===========================================================
Flax 编辑器 FPSCamera 视角旋转修复
日期：2026-11-05

-----------------------------------------------------------------------------------------------------
1. Source/Editor/Viewport/Cameras/FPSCamera.cs
2. Source/Editor/Tools/Terrain/Paint/SingleLayerMode.cs
3. Source/Editor/Tools/Terrain/Paint/TerrainSplatmapImportEditor.cs
4. Source/Editor/Tools/Terrain/Paint/TerrainSplatmapImporter.cs
5. Source/Editor/Tools/Terrain/Brushes/Brush.cs

问题描述
--------
在编辑器视口中, 按住 Alt + 鼠标左键拖拽旋转视野时,
相机位置会瞬间跳到极远的地方, 完全失去控制。


根因分析
--------
FPSCamera 类中定义了全局目标点:
    public Vector3 TargetPoint = new Vector3(-200);

在没有任何选中物体或聚焦 (Focus) 操作的情况下, TargetPoint 会一直
保留这个旧值。当用户按 Alt 旋转时, 原逻辑以下面这段代码计算相机位置:

    float orbitRadius = Mathf.Max((float)Vector3.Distance(ref position, ref TargetPoint), 0.0001f);
    Vector3 localPosition = Viewport.ViewDirection * (-1 * orbitRadius);
    Viewport.ViewPosition = TargetPoint + localPosition;

因为 TargetPoint 可能距离相机数百甚至数千米, 导致轨道半径极大,
鼠标轻微旋转就会让相机瞬间甩到远处的球面上。

另外, 滚轮缩放时 TargetPoint 没有跟随相机移动, 会进一步拉大这个距离,
使得问题在飞行一段距离后更加严重。


修复内容1
----------
文件: Source/Editor/Viewport/Cameras/FPSCamera.cs
方法: UpdateView

修改前:
    if (input.IsOrbiting)
    {
        float orbitRadius = Mathf.Max((float)Vector3.Distance(ref position, ref TargetPoint), 0.0001f);
        Vector3 localPosition = Viewport.ViewDirection * (-1 * orbitRadius);
        Viewport.ViewPosition = TargetPoint + localPosition;
    }

修改后:
    if (input.IsOrbiting)
    {
        // 强制将旋转中心设为相机前方 9000 米处（仅局部生效, 不污染全局 TargetPoint）
        float orbitRadius = 9000.0f;
        Vector3 customPivot = position + forward * orbitRadius;
        Vector3 localPosition = Viewport.ViewDirection * (-1 * orbitRadius);
        Viewport.ViewPosition = customPivot + localPosition;
    }

说明:
    使用局部变量 customPivot 强制指定旋转中心为相机前方 9000 米处,
    不再依赖全局的 TargetPoint。9000 米半径足够大, 旋转时视觉上接近平移,
    避免了原来的瞬间飞天问题。

    刻意不修改全局 TargetPoint, 以避免连带副作用:
    - Relative Panning 模式下平移速度依赖 TargetPoint 距离, 污染后会导致平移飞快。
    - FocusSelection 等其他依赖 TargetPoint 的逻辑会受影响。


修复效果
--------
- 按住 Alt 旋转时相机稳定围绕前方远处的点旋转, 不再飞天。
- 平移 (Pan) 手感保持不变。
- 聚焦 (F 键)、缩放、Gizmo 操作等其他逻辑不受影响。
- 全局 TargetPoint 保持原状, 不影响其他依赖它的功能。

修改内容2
----------
在默认情况下, 使用地形绘制工具 (Paint Tool) 刷地形图层时,
代码会自动按比例削减其他图层的权重 (AffectOtherLayers = true)。
这导致用户只想单独刷某一个图层时, 不小心破坏了其他已经刷好的图层混合,
体验不佳, 且增加了撤销和修复的负担。
修改 AffectOtherLayers 初始值为 false

修改内容3
----------
splatmap import 增加 jpg格式

修改内容4
----------
TerrainSplatmapImporter 导入成功后标记场景修改 Scene.MarkSceneEdited

修改内容5
----------
修改默认brush size =1, 防止误刷

===========================================================
Flax 修改记录
日期：2026-11-06

-----------------------------------------------------------------------------------------------------
1. Source\Engine\Terrain\Terrain.cpp
2. Source\Editor\Editor.cs
3. Source/Engine/Content/Assets/Texture.h
4. Source/Engine/Content/Assets/Texture.cpp
5. Source/Editor/Windows/Assets/TextureWindow.cs

修改内容1
----------
RepairHeightmapEdge 改为不自动落盘
editor里调用后设置脏标记

修改内容2
-----------
在 Texture 资产窗口中新增导出功能，支持将纹理导出为可见图片格式（含压缩格式），并支持 HDR 浮点纹理导出为 .hdr。

涉及文件:
  - Source/Engine/Content/Assets/Texture.h
  - Source/Engine/Content/Assets/Texture.cpp
  - Source/Editor/Windows/Assets/TextureWindow.cs

1. 背景与问题
--------------
此前 Flax 编辑器没有直接导出纹理图片的入口。
底层虽有 TextureTool::ExportTexture（支持压缩格式自动解码为可见像素），
但该函数没有 API_FUNCTION 标记，C# 侧不可见。

TextureTool::ExportTexture 内部通过 DirectXTex 处理:
  - 压缩格式（BC1/BC3/BC5/BC7 等）自动 Decompress 为 R8G8B8A8_UNorm
  - 输出 PNG / BMP / TGA / JPG 等可见格式
  - .hdr 分支仅支持浮点输入格式
  - 像素数据原样保留，不做 gamma 修正


2. C++ 侧改动
-------------
文件: Texture.h

在 #if USE_EDITOR 区块内新增:

    API_FUNCTION() bool ExportToImage(const StringView& path);

说明:
  - 使用 API_FUNCTION() 标记，使绑定生成器将其暴露到 C# 的 Texture 类。
  - 选择在 Texture 上包一层，而不是直接给 TextureTool::ExportTexture 加 API_FUNCTION，
    原因是 TextureData 在 C# 侧的可见性不确定，避免依赖它。


文件: Texture.cpp

新增实现:
    bool Texture::ExportToImage(const StringView& path)

说明:
  - GetTextureData 是 TextureBase 的公开成员，读取所有 mip 和 array slice 的原始数据。
  - 压缩格式在此处不解压，由 TextureTool::ExportTexture 内部的 DirectXTex 处理。
  - 需要重新编译 FlaxEngine，C# 侧才会生成 ExportToImage 方法。


3. C# 侧改动
------------
文件: TextureWindow.cs

在构造函数中添加导出按钮:

    _toolstrip.AddSeparator();
    _toolstrip.AddButton("Export", ExportTexture).LinkTooltip("Export texture to file");

说明:
  - 使用 ToolStrip.AddButton(string, Action) 重载，自动生成纯文字按钮，无需图标。


新增 ExportTexture 方法:

    private void ExportTexture()

说明:
  - filter 末尾使用双 \0 结束，符合 Win32 保存对话框约定。
  - 支持导出格式: PNG / TGA / BMP / HDR。
  - .hdr 前置格式校验: 只有浮点格式（R16G16B16A16_Float / R32G32B32A32_Float / R32G32B32_Float）
    才能导出为 .hdr，否则提前报错并说明当前格式。
  - 压缩格式（BC1/BC3/BC5/BC7 等）会自动由 DirectXTex 解码为可见像素。
  - 若用户选择的扩展名不在支持范围内，底层 GetImageType 会失败，
    最终由 ExportToImage 返回 true，C# 侧打出 Failed to export 日志。


4. 支持与不支持的能力
---------------------
支持:
  - PNG / TGA / BMP 等可见格式导出（含压缩格式自动解码）
  - R16G16B16A16_Float 等浮点格式导出为 .hdr
  - 法线压缩（BC5）导出为可见图片（R/G 通道有数据，B 通道为 0）
  - 导出分辨率与原纹理一致
  - 导出像素为原始数据，不做 sRGB 编码

不支持:
  - 导出为 .exr（GetImageType 未实现，会返回 E_NOTIMPL）
  - LDR 纹理导出为 .hdr（无 HDR 数据，且 SaveToHDRFile 要求浮点输入）
  - 压缩纹理直接导出为 .hdr（未实现前置解压）


5. 已知注意事项
---------------
- 若纹理正在流送（IsLoaded 为 false），ExportToImage 会阻塞主线程等待加载完成。
  后续可在 ExportTexture 中加入 IsLoaded 检查，避免 UI 卡顿。


===========================================================
Flax 修改记录 Flax 引擎 Windows 平台手柄底层识别与过滤修复
日期：2026-11-07
目标: 解决 Flax 游戏在 Windows 平台上 "无线手柄可用、有线手柄无法识别" 的问题，确保 Input Mapping 能正确绑定物理设备。

-----------------------------------------------------------------------------------------------------
1. Source/Engine/Platform/Windows/WindowsInput.cpp


改动总结 - Flax 引擎 Windows 手柄底层修复
=========================================

日期: 2026-11-08
目标文件: Source/Engine/Platform/Windows/WindowsInput.cpp

一、 问题背景
-----------
Windows 平台在有线手柄接入时无法识别，但无线接收器可用。
经测试发现：
1. 无线接收器或遗留驱动经常占用 Slot 0，且处于“假死”状态（能通过系统 API 检测，但无物理输入信号）。
2. 有线手柄通常被系统分配到 Slot 1、Slot 2 等非 0 槽位。
3. Flax 的 Input Mapping 默认只监听 Gamepads[0]。
4. 无论是查 XInputGetCapabilities，还是单纯依靠 XInputGetState，都无法过滤掉那个“假死”的占用槽位，导致真正的物理手柄无法上位。

二、 最终方案（暴力独占模式）
---------------------------
为了保证手柄能立刻响应且不受系统槽位乱序的影响，采用“基于物理输入信号的动态夺取”策略。
核心逻辑：
1. 每 0.2 秒轮询所有 XInput 槽位（0~3）。
2. 不查任何 Capabilities，只看 XInputGetState。
3. 只要检测到某个槽位有按键或摇杆非零信号（即“有物理动作”），立刻锁定该槽位为当前唯一有效手柄。
4. 彻底清空 Input::Gamepads 列表，将当前锁定槽位强行注册为 Gamepads[0]。
5. 如果当前没有任何按键动作，则检查已注册设备的槽位是否断开，断开则清理。

三、 已知取舍与局限
-----------------
1. 当前实现为“单手柄独占”模式。当多只手柄同时按下时，系统会按扫描顺序（或最后动作）强制将其中一个顶为 Gamepads[0]，导致本地双人同屏功能暂不可用。
2. 若玩家插上手柄后长时间不做任何操作，手柄暂不会进入设备列表；一旦按下任意键或推动摇杆，会瞬间注册并响应。
3. 该方案是解决当前手柄识别死锁的最有效、最稳妥兜底方案。未来若需支持多手柄，需将清空列表逻辑替换为“基于用户索引的增量注册与冒泡排序”。

四、 修复效果
-----------
彻底解决了“先开无线，再插有线无反应”的恶性 Bug。
Input Mapping 现在能稳定绑定当前正在使用的手柄（因为物理动作会强制将其提升至 0 号位），使游戏控制完全恢复正常。


===========================================================
Flax SkyLight 半球近似光照模式
日期：2026-11-09

-----------------------------------------------------------------------------------------------------
涉及修改文件：

1.  Source/Engine/Level/Actors/SkyLight.h
2.  Source/Engine/Level/Actors/SkyLight.cpp
3.  Source/Engine/Renderer/Config.h
4.  Source/Engine/Renderer/RenderList.cpp
5.  Source/Shaders/LightingCommon.hlsl
6.  Source/Shaders/Lighting.hlsl


问题描述
--------
SkyLight 仅支持 CaptureScene 和 CustomTexture 两种模式，都需要一个有效
的 cubemap 纹理来采样天空光照。当项目不使用 cubemap（例如纯方向光 + 半球
近似风格化渲染）时，只能绑定一张纯色 cubemap 或留空，前者浪费显存和采样
带宽，后者在 CustomTexture 未指定贴图时直接对空纹理调用 SampleLevel，行为
未定义。

需求：为 SkyLight 增加一个不采样 cubemap、直接根据表面法线朝向计算亮度
的模式，且不新增 GPU 常量字段、不改变结构体大小。


修复内容
--------
文件: SkyLight.h
枚举: Modes

在 CaptureScene = 0、CustomTexture = 1 之后增加：

    HemisphereApproximate = 2,


文件: SkyLight.cpp
方法: Bake

1) Bake()
   非 CaptureScene 模式不参与烘焙：

    void SkyLight::Bake(float timeout)
    {
        if (Mode != Modes::CaptureScene)
            return;
        ProbesRenderer::Bake(this, timeout);
    }


说明:
    Draw()、Serialize()、Deserialize() 无需改动。Draw() 中
    data.Image = GetSource() 在近似模式下已自动为 nullptr；枚举值本身
    随 Mode 字段序列化，旧场景数据不受影响。


文件: Config.h
结构: ShaderLightData

将原本闲置的填充字段改为位标志字段，类型 float -> uint32，大小仍为
4 字节，结构体对齐不变：

    // 原: float Dummy0;
    uint32 Flags;


文件: RenderList.cpp
方法: RenderSkyLightData::SetShaderData

根据 Image 是否为空置位。CustomTexture 未指定贴图、CaptureScene 未烘焙
完成时 Image 也为空，均自动走近似模式：

    uint32 flags = 0;
    if (nullptr==Image) flags |= 1; //Hemisphere Approximate
    data.Flags = flags;

说明:
    采用常量在前的比较写法，避免误写为赋值。


文件: LightingCommon.hlsl
结构: LightData

对应 C++ 侧同步修改：

    // 原: float Dummy0;
    uint Flags;


文件: Lighting.hlsl
函数: GetSkyLightLighting

按 Flags 的 bit 0 判断是否走近似分支，近似分支将法线 Y 分量映射到
0~1 ，替代 cubemap 采样值：

	if ((lightData.Flags & 0x1) != 0) //Hemisphere Approximate
		diffuseLookup = lightData.Color.rgb * saturate(gBuffer.Normal.y * 0.7 + 0.3);
	else
		diffuseLookup = ibl.SampleLevel(SamplerLinearClamp, uvw, mip).rgb * lightData.Color.rgb;

说明:
    1) 亮度映射 saturate(n.y * 0.7 + 0.3) 朝上 1.0, 水平 0.3, 往下渐黑
    2) AdditiveColor 加法、distanceAlpha 距离淡出、gBuffer.AO、
       Luminance 返回值全部复用原逻辑，近似模式在远处同样正常淡出。
    3) Flags 为位域，后续可扩展 bit 1、bit 2 等新标志，无需新增字段。

===========================================================
Flax HDR output, 以及 sdr 模式的 fakeHDR
日期：2026-11-10

-----------------------------------------------------------------------------------------------------
1.  Source\Engine\GraphicsDevice\DirectX\DX11\GPUSwapChainDX11.h
2.  Source\Engine\GraphicsDevice\DirectX\DX11\GPUSwapChainDX11.cpp
3.  Source\Engine\Renderer\PostProcessingPass.cpp
4.  Source\Engine\Core\Config\GraphicsSettings.h
5.  Source\Shaders\GammaCorrectionCommon.hlsl
6.  Source\Shaders\PostProcessing.shader
7.  Source\Shaders\ColorGrading.shader

修改内容1
----------
底层交换链改造 (DX11)
------------------------------------------
* 目标：使 DX11 交换链支持 HDR10 输出。
* 修改：
  1. 格式切换：当开启 EnableHDROutput 时，交换链格式设置为 R10G10B10A2_UNorm。
  2. 色彩空间：通过 IDXGISwapChain3::SetColorSpace1 设置 DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 (PQ)。
  3. 修复 Bug：修正 ResizeBuffers 时未传入新格式导致 HDR 不生效的问题；非 HDR 模式强制回退。
  4. 编辑器保护：编辑器下强制 SDR，避免 UI 刺眼和驱动报错。

修改内容2
----------
后处理管线改造 (PostProcessingPass & Shader)
---------------------------------------------------
* 目标：区分 HDR / SDR 路径，并支持动态参数。
* C++ 侧：
  1. 新增输出枚举：OUTPUT_HDR = 2。
  2. 数据派发：根据 GraphicsSettings::Get()->EnableHDROutput 动态设置 OutputColorSpace。

* Shader 侧 (PS_Composite)：
  分出三条截然不同的色彩路径：
  1. OUTPUT_HDR：仅执行 SoftClip，直接交由系统硬件 PQ 解码。
  2. OUTPUT_LINEAR：执行 SoftClip + FakeHDR，产生最高对比度的暴力通透感。
  3. OUTPUT_SRGB：执行 FakeHDR + LinearToSrgb，产生保留暗部、对比平滑的柔和感。

修改内容3
----------
核心数学函数 (GammaCorrectionCommon.hlsl)
---------------------------------------------------
* 彻底抛弃了传统的 ACES/AgX 等 Tonemapping，手搓核心数学函数：

1. SoftClip(float3 x)：
   - 原理：平滑截断。x <= 0.5 保持不变，x > 0.5 通过有理函数平滑逼近 0.9。
   - 优势：零分支 (Branchless)、纯 SIMD 运算，完美保住暗部，不产生死白。

2. FakeHDR(float3 pqSignal, ...)：
   - 原理：模拟硬件 PQ 解码。将输入视作 PQ 信号，通过 ST2084 公式解码出物理亮度 (nits)。
   - 暗部增益：利用 pqKnee 和 pqGain 在解码前对暗部信号进行动态放大，硬生生把 PQ 曲线黑洞里的细节拽出来。
   - 硬截断：除以 maxDisplayNits (如 300/500) 后 saturate，实现暴力的 R8G8B8 截断效果。

---------------
最终效果总结
---------------
1. 解决了开 HDR 后雪地高光死白、刺眼的问题。
2. 找到了 HDR 渲染的最佳实践：关闭自动曝光和默认 Tonemap，依靠 SoftClip 守住高光上限，直出硬件。
3. 成功利用 PQ 曲线的数学特性，在 SDR 屏幕上模拟出了高对比度、高宽容度的“伪 HDR”画面。
4. 提供了一套可通过参数切换的三种画面风格（物理真实、暴力冲击、柔和平滑）。


================================================================================
Flax 引擎雾效色带修复总结
日期：2026-11-12

一、 涉及修改文件
--------------------------------------------------------------------------------------------------------------------------
1. Source\Shaders\ExponentialHeightFog.hlsl
2. Source\Shaders\Fog.shader
3. Source\Shaders\PostProcessing.shader
4. Content\Editor\MaterialTemplates\Features\ForwardShading.hlsl
================================================================================
二、 问题现象
================================================================================
在高对比度场景（如大面积雪地+暗色岩石）下，开启 HDR 渲染并使用自定义的
FakeHDR 后处理时，雾效区域出现明显的梯度分层（Color Banding/等高线）。

关闭雾效后，色带消失，确认问题由雾效的渲染管线引起。

================================================================================
三、 问题根因
================================================================================
1. 雾效混合机制：
   Flax 的 ExponentialHeightFog 在 LightBuffer（R11G11B10 精度）阶段进行
   Pre-Multiplied Alpha 混合（One / SrcAlpha）。
2. 精度放大：
   在混合过程中，雾浓度的微小渐变量被量化成了离散的浮点阶梯。
3. 后处理暴力拉伸：
   自定义的 FakeHDR（PQ 解码 + 暗部增益）极大地放大了高光和中灰的对比度，
   将原本不可见的量化阶梯拉伸成了肉眼可见的色带。

================================================================================
四、 解决方案
================================================================================
在雾效 Shader (GetExponentialHeightFog) 输出最终颜色前，注入高频微小噪声，以打碎量化的硬阶梯。

修改位置：
ExponentialHeightFog.hlsl -> GetExponentialHeightFog 函数末尾。

================================================================================
五、 原理分析
================================================================================
* 空间高频扰动：噪声在相邻像素间随机跳变，打破了原本平滑渐变的连续性。
* 量化过渡：通过极微小的扰动（0.01），让像素值在两个量化台阶之间随机分布。
* 视觉效果：人眼会自动平滑这些高频噪点，从而看到平滑的渐变，而非生硬的色带。

================================================================================
六、 注意事项与后续优化
================================================================================
1. 参数微调：
   当前系数 0.01 在 4K 分辨率下效果极佳（细腻且无噪点感）。
   若场景亮度变化或切换分辨率（如 1080p），可能需要适当调整该系数。
2. 分辨率无关性：
   当前噪声基于世界坐标，4K 下表现完美。若未来需要适配更广的分辨率，
   建议在 PS_Composite 末尾再叠加基于屏幕像素的最终 Dither（1/255 强度），
   以保证噪点频率在视觉上的分辨率无关性。
================================================================================


