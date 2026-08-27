# Spec: 3DGS PLY Viewer

## 目标
实现一个最小可运行的 3D Gaussian Splatting PLY 文件渲染器。

## 功能
- 加载 PLY 文件
- 解析 Gaussian 属性：position、scale、rotation、opacity、color/SH
- 将数据上传到 GPU buffer
- 根据相机视角进行排序
- 根据 PLY header 识别 LiYukeee/sort-free-gs 模型能力
- 通过 `--sort-free` 选择 LC-WSR 无排序渲染路径
- 在屏幕上渲染 splat
- 支持相机旋转、平移、缩放

## 非目标
- 暂不支持训练
- 暂不支持编辑 Gaussian
- 暂不提供移动端窗口后端或训练流程
- 暂不支持复杂场景管理

## 验收标准
- 可以加载一个标准 PLY 文件
- 屏幕上能看到点云/高斯结果
- 相机移动时画面不崩溃
- 资源释放无明显泄漏
- 普通 PLY 默认保持 sorted-alpha 渲染
- sort-free PLY 默认仍使用 sorted-alpha，传入 `--sort-free` 后使用 LC-WSR
- 不兼容模型请求 sort-free 时警告并安全回退

## Sort-free PLY 约定

兼容模型的 vertex element 必须同时包含 `f_do_0`、`f_ro_0..14` 和
`info`。`f_do_0` 与 `f_ro_0..14` 组成 16 个 view-dependent opacity SH
系数；第 0、1 个 vertex 的 `info` 分别保存背景权重和 LC 深度尺度
`sigma`。模型至少包含两个 vertex，背景权重必须非负，`sigma` 必须为正。

## Sort-free 渲染

LC-WSR 使用 `max(0, 1 - depth / sigma) * Vi` 作为顺序无关权重。第一个
subpass 以加法混合累积颜色分子和权重分母，第二个 subpass 读取 input
attachment 并归一化。背景颜色沿用 viewer 的固定深色背景，背景权重来自
PLY。若模型或 GPU 不支持该路径，应用输出警告并回退 sorted-alpha。
