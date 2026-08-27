# Plan: Sort-free LC-WSR Rendering

1. 扩展 PLY 加载结果，严格识别复现仓库字段并读取 opacity SH、背景权重与
   sigma，同时保持现有加载 API 兼容。
2. 保持主 Gaussian GPU buffer 布局不变，仅为支持模型创建独立 opacity SH
   storage buffer。
3. 增加 `--sort-free` 能力选择；默认继续 sorted-alpha，不兼容请求自动回退。
4. 为 LC-WSR 创建浮点 accumulation attachment、加法混合 splat subpass 和
   input-attachment normalization subpass。
5. 在 swapchain 重建和应用退出时按 Vulkan 依赖顺序释放相关 image、memory、
   descriptor、pipeline 与 render pass。
6. 通过 loader、GPU buffer 与 renderer smoke tests 验证识别、数据布局、回退、
   sort-free 呈现和资源生命周期。
