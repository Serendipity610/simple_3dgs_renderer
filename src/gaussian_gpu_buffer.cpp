#include "simple_3dgs/gaussian_gpu_buffer.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace simple_3dgs {
namespace {

void CheckVk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(result));
    }
}

[[nodiscard]] uint32_t FindMemoryType(VkPhysicalDevice physicalDevice,
                                      uint32_t allowedTypes,
                                      VkMemoryPropertyFlags requiredProperties)
{
    VkPhysicalDeviceMemoryProperties properties {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        const bool typeAllowed = (allowedTypes & (1U << index)) != 0;
        const bool propertiesMatch =
            (properties.memoryTypes[index].propertyFlags & requiredProperties) ==
            requiredProperties;
        if (typeAllowed && propertiesMatch) {
            return index;
        }
    }
    throw std::runtime_error("no compatible Vulkan memory type found");
}

struct TemporaryBuffer {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    TemporaryBuffer() = default;
    TemporaryBuffer(const TemporaryBuffer&) = delete;
    TemporaryBuffer& operator=(const TemporaryBuffer&) = delete;

    TemporaryBuffer(TemporaryBuffer&& other) noexcept
        : device(std::exchange(other.device, VK_NULL_HANDLE)),
          buffer(std::exchange(other.buffer, VK_NULL_HANDLE)),
          memory(std::exchange(other.memory, VK_NULL_HANDLE))
    {
    }

    ~TemporaryBuffer()
    {
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer, nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
        }
    }
};

[[nodiscard]] TemporaryBuffer CreateBuffer(VkPhysicalDevice physicalDevice,
                                            VkDevice device, VkDeviceSize size,
                                            VkBufferUsageFlags usage,
                                            VkMemoryPropertyFlags memoryProperties)
{
    TemporaryBuffer result;
    result.device = device;
    VkBufferCreateInfo bufferInfo {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateBuffer(device, &bufferInfo, nullptr, &result.buffer),
            "vkCreateBuffer");

    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(device, result.buffer, &requirements);
    VkMemoryAllocateInfo allocationInfo {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex =
        FindMemoryType(physicalDevice, requirements.memoryTypeBits, memoryProperties);
    CheckVk(vkAllocateMemory(device, &allocationInfo, nullptr, &result.memory),
            "vkAllocateMemory");
    CheckVk(vkBindBufferMemory(device, result.buffer, result.memory, 0),
            "vkBindBufferMemory");
    return result;
}

void CopyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                VkBuffer source, VkBuffer destination, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    try {
        VkCommandBufferAllocateInfo allocationInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocationInfo.commandPool = commandPool;
        allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocationInfo.commandBufferCount = 1;
        CheckVk(vkAllocateCommandBuffers(device, &allocationInfo, &commandBuffer),
                "vkAllocateCommandBuffers");

        VkCommandBufferBeginInfo beginInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
                "vkBeginCommandBuffer");
        const VkBufferCopy copyRegion {0, 0, size};
        vkCmdCopyBuffer(commandBuffer, source, destination, 1, &copyRegion);
        CheckVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

        VkFenceCreateInfo fenceInfo {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CheckVk(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
        VkSubmitInfo submitInfo {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        CheckVk(vkQueueSubmit(queue, 1, &submitInfo, fence), "vkQueueSubmit");
        CheckVk(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences");
    } catch (...) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (commandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        }
        throw;
    }
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

} // namespace

GpuGaussian ToGpuGaussian(const Gaussian& gaussian)
{
    GpuGaussian result;
    result.positionOpacity = {gaussian.position[0], gaussian.position[1],
                              gaussian.position[2], gaussian.opacity};
    result.scale = {gaussian.scale[0], gaussian.scale[1], gaussian.scale[2], 0.0F};
    result.rotation = gaussian.rotation;
    result.color = {gaussian.color[0], gaussian.color[1], gaussian.color[2],
                    static_cast<float>(gaussian.shDegree)};
    for (size_t index = 0; index < gaussian.shCoefficients.size(); ++index) {
        result.sphericalHarmonics[index / 4][index % 4] =
            gaussian.shCoefficients[index];
    }
    return result;
}

GaussianGpuBuffer::~GaussianGpuBuffer()
{
    Reset();
}

GaussianGpuBuffer::GaussianGpuBuffer(GaussianGpuBuffer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      memory_(std::exchange(other.memory_, VK_NULL_HANDLE)),
      sizeBytes_(std::exchange(other.sizeBytes_, 0)),
      count_(std::exchange(other.count_, 0))
{
}

GaussianGpuBuffer& GaussianGpuBuffer::operator=(GaussianGpuBuffer&& other) noexcept
{
    if (this != &other) {
        Reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        sizeBytes_ = std::exchange(other.sizeBytes_, 0);
        count_ = std::exchange(other.count_, 0);
    }
    return *this;
}

void GaussianGpuBuffer::Upload(VkPhysicalDevice physicalDevice, VkDevice device,
                               VkCommandPool commandPool, VkQueue transferQueue,
                               const std::vector<Gaussian>& gaussians)
{
    if (gaussians.empty()) {
        Reset();
        return;
    }

    const VkDeviceSize size = sizeof(GpuGaussian) * gaussians.size();
    TemporaryBuffer staging = CreateBuffer(
        physicalDevice, device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = nullptr;
    CheckVk(vkMapMemory(device, staging.memory, 0, size, 0, &mapped), "vkMapMemory");
    auto* destination = static_cast<GpuGaussian*>(mapped);
    for (size_t index = 0; index < gaussians.size(); ++index) {
        destination[index] = ToGpuGaussian(gaussians[index]);
    }
    vkUnmapMemory(device, staging.memory);

    TemporaryBuffer uploaded = CreateBuffer(
        physicalDevice, device, size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CopyBuffer(device, commandPool, transferQueue, staging.buffer, uploaded.buffer, size);

    Reset();
    device_ = device;
    buffer_ = std::exchange(uploaded.buffer, VK_NULL_HANDLE);
    memory_ = std::exchange(uploaded.memory, VK_NULL_HANDLE);
    sizeBytes_ = size;
    count_ = gaussians.size();
}

void GaussianGpuBuffer::Reset() noexcept
{
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer_, nullptr);
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    buffer_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    sizeBytes_ = 0;
    count_ = 0;
}

} // namespace simple_3dgs
