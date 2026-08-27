#include "simple_3dgs/gaussian_gpu_buffer.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void CheckVk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 std::to_string(result));
    }
}

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t allowed,
                        VkMemoryPropertyFlags required)
{
    VkPhysicalDeviceMemoryProperties properties {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((allowed & (1U << index)) != 0 &&
            (properties.memoryTypes[index].propertyFlags & required) == required) {
            return index;
        }
    }
    throw std::runtime_error("test could not find host-visible memory");
}

struct VulkanTestContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    VulkanTestContext()
    {
        VkApplicationInfo applicationInfo {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        applicationInfo.pApplicationName = "Gaussian GPU buffer tests";
        applicationInfo.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo instanceInfo {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &applicationInfo;
        CheckVk(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

        uint32_t deviceCount = 0;
        CheckVk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr),
                "vkEnumeratePhysicalDevices");
        Require(deviceCount > 0, "test requires a Vulkan physical device");
        std::vector<VkPhysicalDevice> devices(deviceCount);
        CheckVk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()),
                "vkEnumeratePhysicalDevices");
        for (VkPhysicalDevice candidate : devices) {
            uint32_t queueCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
            for (uint32_t index = 0; index < queueCount; ++index) {
                if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                    physicalDevice = candidate;
                    queueFamily = index;
                    break;
                }
            }
            if (physicalDevice != VK_NULL_HANDLE) {
                break;
            }
        }
        Require(physicalDevice != VK_NULL_HANDLE, "test requires a graphics queue");

        constexpr float priority = 1.0F;
        VkDeviceQueueCreateInfo queueInfo {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        CheckVk(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device),
                "vkCreateDevice");
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
        VkCommandPoolCreateInfo poolInfo {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = queueFamily;
        CheckVk(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
                "vkCreateCommandPool");
    }

    ~VulkanTestContext()
    {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            if (commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, commandPool, nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }
};

template<typename T>
std::vector<T> ReadBack(
    const VulkanTestContext& context, VkBuffer source, size_t count)
{
    const VkDeviceSize size = count * sizeof(T);
    VkBuffer destination = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateBuffer(context.device, &bufferInfo, nullptr, &destination),
            "vkCreateBuffer(readback)");
    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(context.device, destination, &requirements);
    VkMemoryAllocateInfo allocationInfo {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = FindMemoryType(
        context.physicalDevice, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CheckVk(vkAllocateMemory(context.device, &allocationInfo, nullptr, &memory),
            "vkAllocateMemory(readback)");
    CheckVk(vkBindBufferMemory(context.device, destination, memory, 0),
            "vkBindBufferMemory(readback)");

    VkCommandBufferAllocateInfo commandAllocation {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandAllocation.commandPool = context.commandPool;
    commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocation.commandBufferCount = 1;
    CheckVk(vkAllocateCommandBuffers(context.device, &commandAllocation, &commandBuffer),
            "vkAllocateCommandBuffers(readback)");
    VkCommandBufferBeginInfo beginInfo {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer(readback)");
    const VkBufferCopy copy {0, 0, size};
    vkCmdCopyBuffer(commandBuffer, source, destination, 1, &copy);
    CheckVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(readback)");
    VkFenceCreateInfo fenceInfo {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CheckVk(vkCreateFence(context.device, &fenceInfo, nullptr, &fence),
            "vkCreateFence(readback)");
    VkSubmitInfo submitInfo {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    CheckVk(vkQueueSubmit(context.queue, 1, &submitInfo, fence),
            "vkQueueSubmit(readback)");
    CheckVk(vkWaitForFences(context.device, 1, &fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences(readback)");

    void* mapped = nullptr;
    CheckVk(vkMapMemory(context.device, memory, 0, size, 0, &mapped),
            "vkMapMemory(readback)");
    std::vector<T> result(count);
    std::memcpy(result.data(), mapped, static_cast<size_t>(size));
    vkUnmapMemory(context.device, memory);
    vkDestroyFence(context.device, fence, nullptr);
    vkFreeCommandBuffers(context.device, context.commandPool, 1, &commandBuffer);
    vkDestroyBuffer(context.device, destination, nullptr);
    vkFreeMemory(context.device, memory, nullptr);
    return result;
}

void TestUploadAndLifecycle()
{
    VulkanTestContext context;
    simple_3dgs::Gaussian first;
    first.position = {1.0F, 2.0F, 3.0F};
    first.scale = {-1.0F, -2.0F, -3.0F};
    first.opacity = 0.25F;
    first.color = {0.1F, 0.2F, 0.3F};
    first.shDegree = 3;
    for (size_t index = 0; index < first.shCoefficients.size(); ++index) {
        first.shCoefficients[index] = static_cast<float>(index) * 0.125F;
    }
    simple_3dgs::Gaussian second;
    second.position = {-4.0F, 5.0F, 6.0F};
    second.rotation = {0.5F, 0.5F, 0.5F, 0.5F};

    simple_3dgs::GaussianGpuBuffer buffer;
    buffer.Upload(context.physicalDevice, context.device, context.commandPool,
                  context.queue, {first, second});
    Require(buffer.Buffer() != VK_NULL_HANDLE, "upload must create a buffer");
    Require(buffer.Count() == 2, "upload count");
    Require(buffer.SizeBytes() == 2 * sizeof(simple_3dgs::GpuGaussian), "upload size");

    const auto uploaded = ReadBack<simple_3dgs::GpuGaussian>(
        context, buffer.Buffer(), buffer.Count());
    Require(std::abs(uploaded[0].positionOpacity[2] - 3.0F) < 1.0e-6F,
            "uploaded position");
    Require(std::abs(uploaded[0].positionOpacity[3] - 0.25F) < 1.0e-6F,
            "uploaded opacity");
    Require(std::abs(uploaded[0].color[1] - 0.2F) < 1.0e-6F, "uploaded color");
    Require(std::abs(uploaded[0].color[3] - 3.0F) < 1.0e-6F,
            "uploaded SH degree");
    Require(std::abs(uploaded[0].sphericalHarmonics[0][3] - 0.375F) < 1.0e-6F,
            "uploaded first SH block");
    Require(std::abs(uploaded[0].sphericalHarmonics[11][3] - 5.875F) < 1.0e-6F,
            "uploaded last SH block");
    Require(std::abs(uploaded[1].rotation[3] - 0.5F) < 1.0e-6F,
            "uploaded rotation");

    simple_3dgs::GaussianGpuBuffer moved = std::move(buffer);
    Require(buffer.Buffer() == VK_NULL_HANDLE && moved.Count() == 2,
            "move transfers ownership");
    moved.Reset();
    Require(moved.Buffer() == VK_NULL_HANDLE && moved.SizeBytes() == 0,
            "reset releases ownership");
}

void TestOpacityShUploadAndLifecycle()
{
    VulkanTestContext context;
    simple_3dgs::Gaussian first;
    simple_3dgs::Gaussian second;
    for (size_t index = 0; index < first.opacityShCoefficients.size(); ++index) {
        first.opacityShCoefficients[index] = static_cast<float>(index) * 0.25F;
        second.opacityShCoefficients[index] = -static_cast<float>(index);
    }
    simple_3dgs::OpacityShGpuBuffer buffer;
    buffer.Upload(context.physicalDevice, context.device, context.commandPool,
                  context.queue, {first, second});
    Require(buffer.Count() == 2, "opacity SH upload count");
    Require(buffer.SizeBytes() == 2 * sizeof(simple_3dgs::GpuOpacitySh),
            "opacity SH upload size");
    const auto uploaded = ReadBack<simple_3dgs::GpuOpacitySh>(
        context, buffer.Buffer(), buffer.Count());
    Require(std::abs(uploaded[0].coefficients[3][3] - 3.75F) < 1.0e-6F,
            "opacity SH last coefficient");
    Require(std::abs(uploaded[1].coefficients[1][2] + 6.0F) < 1.0e-6F,
            "opacity SH second record");

    simple_3dgs::OpacityShGpuBuffer moved = std::move(buffer);
    Require(buffer.Buffer() == VK_NULL_HANDLE && moved.Count() == 2,
            "opacity SH move transfers ownership");
    moved.Reset();
    Require(moved.Buffer() == VK_NULL_HANDLE && moved.SizeBytes() == 0,
            "opacity SH reset releases ownership");
}

} // namespace

int main()
{
    try {
        TestUploadAndLifecycle();
        TestOpacityShUploadAndLifecycle();
        std::cout << "Gaussian GPU buffer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Gaussian GPU buffer test failure: " << error.what() << '\n';
        return 1;
    }
}
