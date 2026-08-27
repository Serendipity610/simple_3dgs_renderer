# Tasks

## T1: 搭建项目结构
- 初始化当前git仓
- 创建 CMake 项目
- 初始化窗口系统
- 初始化 Vulkan context
- 渲染一个空背景

## T2: 实现 PLY 解析
- 读取 ASCII/Binary PLY
- 解析 position、scale、rotation、opacity、color
- 添加单元测试

## T3: 上传 Gaussian 数据到 GPU
- 创建 GPU buffer
- 完成 CPU 到 GPU 数据传输
- 确认 buffer 生命周期正确

## T4: 实现基础 splat 渲染
- 编写 vertex shader
- 编写 fragment shader
- 完成 pipeline 配置

## T5: 实现相机控制
- 鼠标旋转
- 滚轮缩放
- WASD 平移

## T6: 实现 Gaussian 动态深度排序
- 按当前相机位置和视线方向计算每个 Gaussian 的相机深度
- 使用从远到近的顺序绘制透明 splat，减少 alpha blending 深度错误
- 避免每帧重传完整 Gaussian 数据，优先只更新排序后的索引数据

## T7: 改进 splat 裁剪与近距离稳定性
- 避免仅按中心点裁剪导致靠近相机或进入模型内部时错误丢失 splat
- 限制异常大的屏幕空间 footprint，防止近裁附近的高斯椭圆炸成全屏条纹
- 保持当前最小 viewer 架构，不引入复杂 tile-based culling

## T8: 识别 sort-free PLY 模型
- 解析 `f_do_0`、`f_ro_0..14` 和 `info`
- 校验背景权重、sigma 和最小 vertex 数
- 为缺失或非法字段提供可诊断的不支持状态
- 添加 ASCII/Binary 和错误输入单元测试

## T9: 上传 view-dependent opacity SH
- 保持现有 GpuGaussian 布局不变
- 为 sort-free 模型创建独立 storage buffer
- 验证上传内容、移动语义和释放生命周期

## T10: 实现 LC-WSR Vulkan 渲染路径
- 增加 `--sort-free`，默认保留 sorted-alpha
- 使用原始 Gaussian 顺序绘制，不构建或更新排序索引
- 用浮点 attachment 加法累积颜色分子与权重分母
- 用第二个 subpass 归一化并输出到 swapchain
- 在模型或设备不支持时警告并回退
- 覆盖 swapchain 重建、同步和资源释放

## T11: Sort-free 验收
- 增加最小 sort-free PLY 测试资产
- 添加默认路径、sort-free 路径及不兼容回退 smoke tests
- 运行 build/test 并记录不可用的格式化或静态检查工具
