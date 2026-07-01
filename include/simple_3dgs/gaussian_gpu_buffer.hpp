#pragma once

#include "simple_3dgs/gaussian.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <vector>

namespace simple_3dgs {

struct alignas(16) GpuGaussian {
    std::array<float, 4> positionOpacity {};
    std::array<float, 4> scale {};
    std::array<float, 4> rotation {};
    std::array<float, 4> color {};
    std::array<std::array<float, 4>, kShCoefficientCount / 4> sphericalHarmonics {};
};

[[nodiscard]] GpuGaussian ToGpuGaussian(const Gaussian& gaussian);

// The VkDevice and VkPhysicalDevice passed to Upload must outlive this object.
class GaussianGpuBuffer {
public:
    GaussianGpuBuffer() = default;
    ~GaussianGpuBuffer();

    GaussianGpuBuffer(const GaussianGpuBuffer&) = delete;
    GaussianGpuBuffer& operator=(const GaussianGpuBuffer&) = delete;
    GaussianGpuBuffer(GaussianGpuBuffer&& other) noexcept;
    GaussianGpuBuffer& operator=(GaussianGpuBuffer&& other) noexcept;

    void Upload(VkPhysicalDevice physicalDevice, VkDevice device,
                VkCommandPool commandPool, VkQueue transferQueue,
                const std::vector<Gaussian>& gaussians);
    void Reset() noexcept;

    [[nodiscard]] VkBuffer Buffer() const noexcept { return buffer_; }
    [[nodiscard]] VkDeviceSize SizeBytes() const noexcept { return sizeBytes_; }
    [[nodiscard]] size_t Count() const noexcept { return count_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize sizeBytes_ = 0;
    size_t count_ = 0;
};

static_assert(sizeof(GpuGaussian) == 256);

} // namespace simple_3dgs
