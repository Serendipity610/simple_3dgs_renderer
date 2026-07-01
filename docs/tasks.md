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