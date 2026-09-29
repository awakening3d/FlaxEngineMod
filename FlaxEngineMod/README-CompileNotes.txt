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
