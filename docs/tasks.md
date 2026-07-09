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
