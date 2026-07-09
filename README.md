# Simple 3DGS Renderer

一个使用 C++17 和 Vulkan 编写的最小 3D Gaussian Splatting PLY 查看器。目前提供
Win32 窗口后端，可加载 Gaussian PLY 数据、上传到 GPU，并以实例化 splat 进行渲染。

## 当前功能

- 读取 ASCII、binary little-endian 和 binary big-endian PLY 1.0 文件
- 解析 position、scale、rotation、opacity、RGB 和 0–3 阶球谐颜色
- 使用 staging buffer 将 Gaussian 数据上传到 GPU storage buffer
- 将三维 covariance 透视投影为屏幕空间各向异性椭圆 splat
- 使用 Vulkan swapchain 和 alpha blending 渲染屏幕空间 splat
- 支持窗口缩放及 swapchain 重建
- 支持鼠标和键盘相机控制
- 包含 PLY、相机、GPU buffer 和渲染冒烟测试

## 环境要求

- Windows 10/11
- 支持 Vulkan 的显卡和驱动
- CMake 3.20 或更高版本
- Visual Studio 2022 C++ 工具链
- Vulkan SDK（需要 Vulkan headers、loader 和 `glslc`）
- 可选：Git Bash、WSL 或其他 Bash 环境，用于执行仓库中的 shell 脚本

## 构建

在 PowerShell 中执行：

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
```

如果环境中提供 Bash，也可以执行：

```bash
./build.sh
```

构建后的程序位于：

```text
Visual Studio / multi-config: build/Debug/simple_3dgs_engine.exe
Ninja / Makefiles / single-config: build/simple_3dgs_engine.exe
```

## 运行

加载 PLY 文件：

```powershell
.\build\Debug\simple_3dgs_engine.exe <path-to-file.ply>
# single-config generator:
.\build\simple_3dgs_engine.exe <path-to-file.ply>
```

例如加载仓库中的测试数据：

```powershell
.\build\Debug\simple_3dgs_engine.exe .\tests\data\gaussians_ascii.ply
```

不指定 PLY 文件时，程序会渲染内置的三个示例 Gaussian：

```powershell
.\build\Debug\simple_3dgs_engine.exe
```

使用 `--smoke-test` 时，程序在成功呈现 3 帧后自动退出：

```powershell
.\build\Debug\simple_3dgs_engine.exe .\tests\data\gaussians_ascii.ply --smoke-test
```

## 相机控制

| 输入 | 操作 |
| --- | --- |
| 按住鼠标左键拖动 | 旋转相机 |
| 鼠标滚轮 | 缩放 |
| `W` / `S` | 前进 / 后退 |
| `A` / `D` | 左移 / 右移 |

## PLY 数据约定

每个 vertex 必须包含 `x`、`y` 和 `z`。以下属性为可选项，缺失时使用默认值：

| 数据 | 支持的属性名 |
| --- | --- |
| Scale | `scale_0..2` 或 `scale_x/y/z` |
| Rotation | `rot_0..3`、`rotation_w/x/y/z` 或 `rw/rx/ry/rz` |
| Opacity | `opacity` 或 `alpha` |
| RGB | `red/green/blue`、`r/g/b` 或 `color_0..2` |
| 球谐颜色 | `f_dc_0..2` 和 `f_rest_0..44`（最高 3 阶） |

整型 RGB/alpha 会归一化到 `[0, 1]`。当直接 RGB 缺失时，加载器会读取标准
3DGS 的 `f_dc_*`/`f_rest_*` 布局，并根据相机视线方向在 shader 中计算 SH 颜色。

## 测试

在 PowerShell 中执行：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

或者在 Bash 环境中执行：

```bash
./run_tests.sh
```

## 当前限制

- 仅提供 Win32 窗口后端
- 尚未根据相机视角对 Gaussian 进行深度排序
- 不支持训练、Gaussian 编辑或复杂场景管理
- 当前实现面向最小可运行验证，尚未针对大规模数据或移动平台优化

## 项目结构

```text
include/simple_3dgs/  公共 C++ 接口
src/                  PLY 加载、相机、GPU buffer 和 Vulkan 应用
shaders/              GLSL splat shaders
tests/                单元测试、冒烟测试数据和加载基准
docs/                 规格与任务列表
```
