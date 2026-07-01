# Spec: 3DGS PLY Viewer

## 目标
实现一个最小可运行的 3D Gaussian Splatting PLY 文件渲染器。

## 功能
- 加载 PLY 文件
- 解析 Gaussian 属性：position、scale、rotation、opacity、color/SH
- 将数据上传到 GPU buffer
- 根据相机视角进行排序
- 在屏幕上渲染 splat
- 支持相机旋转、平移、缩放

## 非目标
- 暂不支持训练
- 暂不支持编辑 Gaussian
- 暂不支持移动端优化
- 暂不支持复杂场景管理

## 验收标准
- 可以加载一个标准 PLY 文件
- 屏幕上能看到点云/高斯结果
- 相机移动时画面不崩溃
- 资源释放无明显泄漏