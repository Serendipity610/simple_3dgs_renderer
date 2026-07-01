#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <vulkan/vulkan.h>

#include "simple_3dgs/gaussian_gpu_buffer.hpp"
#include "simple_3dgs/camera.hpp"
#include "simple_3dgs/ply_loader.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kInitialWidth = 1280;
constexpr uint32_t kInitialHeight = 720;
constexpr size_t kFramesInFlight = 2;

void CheckVk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(result));
    }
}

struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    [[nodiscard]] bool Complete() const
    {
        return graphics.has_value() && present.has_value();
    }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities {};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct CameraPushConstants {
    std::array<float, 16> viewProjection {};
    std::array<float, 4> viewportSizeAndFocalLength {};
    std::array<float, 4> cameraPositionAndPadding {};
    std::array<float, 4> cameraTargetAndPadding {};
};

static_assert(sizeof(CameraPushConstants) == 112);

class VulkanApplication {
public:
    void Run(HINSTANCE instance, int showCommand)
    {
        LoadInputGaussians();
        CreateWindowHandle(instance, showCommand);
        try {
            InitializeVulkan(instance);
            MainLoop();
        } catch (...) {
            Cleanup();
            DestroyWindowHandle(instance);
            throw;
        }
        Cleanup();
        DestroyWindowHandle(instance);
    }

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam,
                                             LPARAM lParam)
    {
        VulkanApplication* app = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            app = static_cast<VulkanApplication*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<VulkanApplication*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (app != nullptr && message == WM_SIZE) {
            app->framebufferResized_ = true;
        }
        if (app != nullptr && message == WM_LBUTTONDOWN) {
            app->mouseDragging_ = true;
            app->lastMouseX_ = GET_X_LPARAM(lParam);
            app->lastMouseY_ = GET_Y_LPARAM(lParam);
            SetCapture(window);
            return 0;
        }
        if (app != nullptr && message == WM_LBUTTONUP) {
            app->mouseDragging_ = false;
            ReleaseCapture();
            return 0;
        }
        if (app != nullptr && message == WM_MOUSEMOVE && app->mouseDragging_) {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            app->camera_.Rotate(static_cast<float>(x - app->lastMouseX_),
                                static_cast<float>(y - app->lastMouseY_));
            app->lastMouseX_ = x;
            app->lastMouseY_ = y;
            return 0;
        }
        if (app != nullptr && message == WM_MOUSEWHEEL) {
            app->camera_.Zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                              static_cast<float>(WHEEL_DELTA));
            return 0;
        }
        if (app != nullptr && (message == WM_KEYDOWN || message == WM_KEYUP)) {
            if (wParam < app->keyDown_.size()) {
                app->keyDown_[wParam] = message == WM_KEYDOWN;
            }
            return 0;
        }
        if (app != nullptr && message == WM_KILLFOCUS) {
            app->keyDown_.fill(false);
            app->mouseDragging_ = false;
        }
        if (message == WM_CLOSE) {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void CreateWindowHandle(HINSTANCE instance, int showCommand)
    {
        WNDCLASSEXW windowClass {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kWindowClassName;

        if (RegisterClassExW(&windowClass) == 0) {
            throw std::runtime_error("failed to register Win32 window class");
        }
        windowClassRegistered_ = true;

        RECT rectangle {0, 0, static_cast<LONG>(kInitialWidth),
                        static_cast<LONG>(kInitialHeight)};
        AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
        window_ = CreateWindowExW(0, kWindowClassName, L"Simple 3DGS Engine",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  rectangle.right - rectangle.left,
                                  rectangle.bottom - rectangle.top, nullptr, nullptr,
                                  instance, this);
        if (window_ == nullptr) {
            throw std::runtime_error("failed to create Win32 window");
        }

        ShowWindow(window_, showCommand);
        UpdateWindow(window_);
    }

    void DestroyWindowHandle(HINSTANCE instance)
    {
        if (window_ != nullptr) {
            DestroyWindow(window_);
            window_ = nullptr;
        }
        if (windowClassRegistered_) {
            UnregisterClassW(kWindowClassName, instance);
            windowClassRegistered_ = false;
        }
    }

    void InitializeVulkan(HINSTANCE applicationInstance)
    {
        CreateInstance();
        CreateSurface(applicationInstance);
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateSwapchain();
        CreateImageViews();
        CreateRenderPass();
        CreateCommandPool();
        gaussianBuffer_.Upload(physicalDevice_, device_, commandPool_, graphicsQueue_,
                               gaussians_);
        CreateDescriptorResources();
        CreateGraphicsPipeline();
        CreateFramebuffers();
        CreateCommandBuffers();
        CreateSyncObjects();
    }

    void LoadInputGaussians()
    {
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr) {
            throw std::runtime_error("failed to parse command line");
        }
        std::optional<std::filesystem::path> plyPath;
        for (int index = 1; index < argumentCount; ++index) {
            if (std::wstring(arguments[index]) == L"--smoke-test") {
                smokeTest_ = true;
            } else if (!plyPath.has_value()) {
                plyPath = std::filesystem::path(arguments[index]);
            } else {
                LocalFree(arguments);
                throw std::runtime_error("expected at most one PLY file path");
            }
        }
        LocalFree(arguments);

        if (plyPath.has_value()) {
            gaussians_ = simple_3dgs::PlyLoader::Load(*plyPath);
            if (gaussians_.empty()) {
                throw std::runtime_error("PLY file contains no vertices");
            }
            return;
        }
        simple_3dgs::Gaussian left;
        left.position = {-1.4F, -0.4F, 0.0F};
        left.scale = {-1.6F, -1.6F, -1.6F};
        left.opacity = 2.0F;
        left.color = {1.0F, 0.25F, 0.12F};
        simple_3dgs::Gaussian center;
        center.position = {0.0F, 0.8F, 0.0F};
        center.scale = {-1.25F, -1.25F, -1.25F};
        center.opacity = 2.5F;
        center.color = {0.15F, 0.85F, 1.0F};
        simple_3dgs::Gaussian right;
        right.position = {1.4F, -0.4F, 0.0F};
        right.scale = {-1.45F, -1.45F, -1.45F};
        right.opacity = 2.0F;
        right.color = {0.65F, 0.3F, 1.0F};
        gaussians_ = {left, center, right};
    }

    void CreateInstance()
    {
        VkApplicationInfo applicationInfo {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        applicationInfo.pApplicationName = "Simple 3DGS Engine";
        applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        applicationInfo.pEngineName = "Simple 3DGS Engine";
        applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_0;

        constexpr std::array<const char*, 2> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        };

        VkInstanceCreateInfo createInfo {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        CheckVk(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
    }

    void CreateSurface(HINSTANCE applicationInstance)
    {
        VkWin32SurfaceCreateInfoKHR createInfo {
            VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        createInfo.hinstance = applicationInstance;
        createInfo.hwnd = window_;
        CheckVk(vkCreateWin32SurfaceKHR(instance_, &createInfo, nullptr, &surface_),
                "vkCreateWin32SurfaceKHR");
    }

    [[nodiscard]] QueueFamilies FindQueueFamilies(VkPhysicalDevice device) const
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

        QueueFamilies families;
        for (uint32_t index = 0; index < count; ++index) {
            if ((properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                families.graphics = index;
            }
            VkBool32 presentSupported = VK_FALSE;
            CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_,
                                                         &presentSupported),
                    "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (presentSupported == VK_TRUE) {
                families.present = index;
            }
            if (families.Complete()) {
                break;
            }
        }
        return families;
    }

    [[nodiscard]] bool SupportsSwapchainExtension(VkPhysicalDevice device) const
    {
        uint32_t count = 0;
        CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
                "vkEnumerateDeviceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(count);
        CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                                      extensions.data()),
                "vkEnumerateDeviceExtensionProperties");
        return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
            return std::string(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        });
    }

    [[nodiscard]] SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device) const
    {
        SwapchainSupport support;
        CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_,
                                                          &support.capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        uint32_t formatCount = 0;
        CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount,
                                                     nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR");
        support.formats.resize(formatCount);
        if (formatCount > 0) {
            CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                        device, surface_, &formatCount, support.formats.data()),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR");
        }

        uint32_t presentModeCount = 0;
        CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                    device, surface_, &presentModeCount, nullptr),
                "vkGetPhysicalDeviceSurfacePresentModesKHR");
        support.presentModes.resize(presentModeCount);
        if (presentModeCount > 0) {
            CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                        device, surface_, &presentModeCount, support.presentModes.data()),
                    "vkGetPhysicalDeviceSurfacePresentModesKHR");
        }
        return support;
    }

    void PickPhysicalDevice()
    {
        uint32_t count = 0;
        CheckVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
                "vkEnumeratePhysicalDevices");
        if (count == 0) {
            throw std::runtime_error("no Vulkan-capable physical device found");
        }

        std::vector<VkPhysicalDevice> devices(count);
        CheckVk(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
                "vkEnumeratePhysicalDevices");
        for (VkPhysicalDevice device : devices) {
            const QueueFamilies families = FindQueueFamilies(device);
            if (!families.Complete() || !SupportsSwapchainExtension(device)) {
                continue;
            }
            const SwapchainSupport support = QuerySwapchainSupport(device);
            if (!support.formats.empty() && !support.presentModes.empty()) {
                physicalDevice_ = device;
                queueFamilies_ = families;
                return;
            }
        }
        throw std::runtime_error("no Vulkan device supports graphics and presentation");
    }

    void CreateLogicalDevice()
    {
        const float priority = 1.0F;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        std::vector<uint32_t> uniqueFamilies = {*queueFamilies_.graphics};
        if (queueFamilies_.present != queueFamilies_.graphics) {
            uniqueFamilies.push_back(*queueFamilies_.present);
        }
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueInfo {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            queueInfos.push_back(queueInfo);
        }

        constexpr std::array<const char*, 1> extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkPhysicalDeviceFeatures features {};
        VkDeviceCreateInfo createInfo {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        CheckVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
                "vkCreateDevice");
        vkGetDeviceQueue(device_, *queueFamilies_.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, *queueFamilies_.present, 0, &presentQueue_);
    }

    [[nodiscard]] VkSurfaceFormatKHR ChooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) const
    {
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
        return formats.front();
    }

    [[nodiscard]] VkPresentModeKHR ChoosePresentMode(
        const std::vector<VkPresentModeKHR>& modes) const
    {
        const auto mailbox = std::find(modes.begin(), modes.end(),
                                       VK_PRESENT_MODE_MAILBOX_KHR);
        return mailbox != modes.end() ? *mailbox : VK_PRESENT_MODE_FIFO_KHR;
    }

    [[nodiscard]] VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        RECT client {};
        GetClientRect(window_, &client);
        VkExtent2D extent {static_cast<uint32_t>(client.right - client.left),
                           static_cast<uint32_t>(client.bottom - client.top)};
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
        return extent;
    }

    void CreateSwapchain()
    {
        const SwapchainSupport support = QuerySwapchainSupport(physicalDevice_);
        const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.formats);
        const VkPresentModeKHR presentMode = ChoosePresentMode(support.presentModes);
        const VkExtent2D extent = ChooseExtent(support.capabilities);

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0) {
            imageCount = std::min(imageCount, support.capabilities.maxImageCount);
        }

        VkSwapchainCreateInfoKHR createInfo {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const std::array<uint32_t, 2> indices = {*queueFamilies_.graphics,
                                                  *queueFamilies_.present};
        if (indices[0] != indices[1]) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = static_cast<uint32_t>(indices.size());
            createInfo.pQueueFamilyIndices = indices.data();
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        createInfo.preTransform = support.capabilities.currentTransform;
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> compositeAlphaModes = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        const auto compositeAlpha = std::find_if(
            compositeAlphaModes.begin(), compositeAlphaModes.end(),
            [&support](VkCompositeAlphaFlagBitsKHR mode) {
                return (support.capabilities.supportedCompositeAlpha & mode) != 0;
            });
        if (compositeAlpha == compositeAlphaModes.end()) {
            throw std::runtime_error("surface exposes no supported composite alpha mode");
        }
        createInfo.compositeAlpha = *compositeAlpha;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        CheckVk(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
                "vkCreateSwapchainKHR");

        CheckVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr),
                "vkGetSwapchainImagesKHR");
        swapchainImages_.resize(imageCount);
        CheckVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount,
                                        swapchainImages_.data()),
                "vkGetSwapchainImagesKHR");
        swapchainFormat_ = surfaceFormat.format;
        swapchainExtent_ = extent;
    }

    void CreateImageViews()
    {
        swapchainImageViews_.resize(swapchainImages_.size());
        for (size_t index = 0; index < swapchainImages_.size(); ++index) {
            VkImageViewCreateInfo createInfo {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            createInfo.image = swapchainImages_[index];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = swapchainFormat_;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;
            CheckVk(vkCreateImageView(device_, &createInfo, nullptr,
                                      &swapchainImageViews_[index]),
                    "vkCreateImageView");
        }
    }

    void CreateRenderPass()
    {
        VkAttachmentDescription colorAttachment {};
        colorAttachment.format = swapchainFormat_;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorReference {};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkSubpassDependency dependency {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo createInfo {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &colorAttachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;
        CheckVk(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_),
                "vkCreateRenderPass");
    }

    [[nodiscard]] std::vector<uint32_t> ReadShaderCode(const wchar_t* fileName) const
    {
        std::array<wchar_t, 32768> executablePath {};
        const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
                                                static_cast<DWORD>(executablePath.size()));
        if (length == 0 || length == executablePath.size()) {
            throw std::runtime_error("failed to locate executable directory");
        }
        const std::filesystem::path path =
            std::filesystem::path(executablePath.data()).parent_path() / L"shaders" /
            fileName;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            throw std::runtime_error("failed to open shader: " + path.string());
        }
        const std::streamoff byteCount = input.tellg();
        if (byteCount <= 0 || byteCount % 4 != 0) {
            throw std::runtime_error("shader has invalid SPIR-V size: " + path.string());
        }
        std::vector<uint32_t> code(static_cast<size_t>(byteCount) / sizeof(uint32_t));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(code.data()), byteCount);
        if (!input) {
            throw std::runtime_error("failed to read shader: " + path.string());
        }
        return code;
    }

    [[nodiscard]] VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code) const
    {
        VkShaderModuleCreateInfo createInfo {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();
        VkShaderModule shader = VK_NULL_HANDLE;
        CheckVk(vkCreateShaderModule(device_, &createInfo, nullptr, &shader),
                "vkCreateShaderModule");
        return shader;
    }

    void CreateDescriptorResources()
    {
        VkDescriptorSetLayoutBinding bufferBinding {};
        bufferBinding.binding = 0;
        bufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferBinding.descriptorCount = 1;
        bufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &bufferBinding;
        CheckVk(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr,
                                            &descriptorSetLayout_),
                "vkCreateDescriptorSetLayout");

        VkPipelineLayoutCreateInfo pipelineLayoutInfo {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
        VkPushConstantRange cameraRange {};
        cameraRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        cameraRange.offset = 0;
        cameraRange.size = sizeof(CameraPushConstants);
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &cameraRange;
        CheckVk(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr,
                                       &pipelineLayout_),
                "vkCreatePipelineLayout");

        VkDescriptorPoolSize poolSize {};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo poolInfo {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        CheckVk(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_),
                "vkCreateDescriptorPool");

        VkDescriptorSetAllocateInfo allocationInfo {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocationInfo.descriptorPool = descriptorPool_;
        allocationInfo.descriptorSetCount = 1;
        allocationInfo.pSetLayouts = &descriptorSetLayout_;
        CheckVk(vkAllocateDescriptorSets(device_, &allocationInfo, &descriptorSet_),
                "vkAllocateDescriptorSets");
        VkDescriptorBufferInfo bufferInfo {};
        bufferInfo.buffer = gaussianBuffer_.Buffer();
        bufferInfo.offset = 0;
        bufferInfo.range = gaussianBuffer_.SizeBytes();
        VkWriteDescriptorSet write {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    void CreateGraphicsPipeline()
    {
        const std::vector<uint32_t> vertexCode = ReadShaderCode(L"splat.vert.spv");
        const std::vector<uint32_t> fragmentCode = ReadShaderCode(L"splat.frag.spv");
        const VkShaderModule vertexShader = CreateShaderModule(vertexCode);
        VkShaderModule fragmentShader = VK_NULL_HANDLE;
        try {
            fragmentShader = CreateShaderModule(fragmentCode);
        } catch (...) {
            vkDestroyShaderModule(device_, vertexShader, nullptr);
            throw;
        }

        VkPipelineShaderStageCreateInfo vertexStage {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexStage.module = vertexShader;
        vertexStage.pName = "main";
        VkPipelineShaderStageCreateInfo fragmentStage {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentStage.module = fragmentShader;
        fragmentStage.pName = "main";
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {
            vertexStage, fragmentStage};

        VkPipelineVertexInputStateCreateInfo vertexInput {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport {0.0F, 0.0F, static_cast<float>(swapchainExtent_.width),
                             static_cast<float>(swapchainExtent_.height), 0.0F, 1.0F};
        VkRect2D scissor {{0, 0}, swapchainExtent_};
        VkPipelineViewportStateCreateInfo viewportState {
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo rasterization {
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo multisampling {
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blendAttachment {};
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                         VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT |
                                         VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blending {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1;
        blending.pAttachments = &blendAttachment;

        VkGraphicsPipelineCreateInfo pipelineInfo {
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &blending;
        pipelineInfo.layout = pipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;
        const VkResult result = vkCreateGraphicsPipelines(
            device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline_);
        vkDestroyShaderModule(device_, fragmentShader, nullptr);
        vkDestroyShaderModule(device_, vertexShader, nullptr);
        CheckVk(result, "vkCreateGraphicsPipelines");
    }

    void CreateFramebuffers()
    {
        framebuffers_.resize(swapchainImageViews_.size());
        for (size_t index = 0; index < swapchainImageViews_.size(); ++index) {
            const VkImageView attachment = swapchainImageViews_[index];
            VkFramebufferCreateInfo createInfo {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            createInfo.renderPass = renderPass_;
            createInfo.attachmentCount = 1;
            createInfo.pAttachments = &attachment;
            createInfo.width = swapchainExtent_.width;
            createInfo.height = swapchainExtent_.height;
            createInfo.layers = 1;
            CheckVk(vkCreateFramebuffer(device_, &createInfo, nullptr,
                                        &framebuffers_[index]),
                    "vkCreateFramebuffer");
        }
    }

    void CreateCommandPool()
    {
        VkCommandPoolCreateInfo createInfo {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = *queueFamilies_.graphics;
        CheckVk(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_),
                "vkCreateCommandPool");
    }

    void CreateCommandBuffers()
    {
        commandBuffers_.resize(framebuffers_.size());
        VkCommandBufferAllocateInfo allocateInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = commandPool_;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
        CheckVk(vkAllocateCommandBuffers(device_, &allocateInfo, commandBuffers_.data()),
                "vkAllocateCommandBuffers");
    }

    void RecordCommandBuffer(uint32_t imageIndex)
    {
        VkCommandBuffer commandBuffer = commandBuffers_[imageIndex];
        CheckVk(vkResetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo beginInfo {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
        constexpr VkClearValue clearColor = {{{0.025F, 0.035F, 0.055F, 1.0F}}};
        VkRenderPassBeginInfo renderPassInfo {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = framebuffers_[imageIndex];
        renderPassInfo.renderArea.extent = swapchainExtent_;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          graphicsPipeline_);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
        CameraPushConstants cameraState;
        cameraState.viewProjection = camera_.ViewProjection(
            static_cast<float>(swapchainExtent_.width) /
            static_cast<float>(swapchainExtent_.height));
        const float viewportWidth = static_cast<float>(swapchainExtent_.width);
        const float viewportHeight = static_cast<float>(swapchainExtent_.height);
        const auto focalLength =
            camera_.FocalLengthPixels(viewportWidth, viewportHeight);
        cameraState.viewportSizeAndFocalLength = {
            viewportWidth, viewportHeight, focalLength[0], focalLength[1]};
        const auto cameraPosition = camera_.Position();
        cameraState.cameraPositionAndPadding = {cameraPosition[0], cameraPosition[1],
                                                cameraPosition[2], 0.0F};
        const auto& cameraTarget = camera_.Target();
        cameraState.cameraTargetAndPadding = {cameraTarget[0], cameraTarget[1],
                                              cameraTarget[2], 0.0F};
        vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(cameraState), &cameraState);
        vkCmdDraw(commandBuffer, 6, static_cast<uint32_t>(gaussianBuffer_.Count()), 0, 0);
        vkCmdEndRenderPass(commandBuffer);
        CheckVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    }

    void CreateSyncObjects()
    {
        VkSemaphoreCreateInfo semaphoreInfo {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (size_t index = 0; index < kFramesInFlight; ++index) {
            CheckVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                                      &imageAvailable_[index]),
                    "vkCreateSemaphore");
            CheckVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                                      &renderFinished_[index]),
                    "vkCreateSemaphore");
            CheckVk(vkCreateFence(device_, &fenceInfo, nullptr, &inFlight_[index]),
                    "vkCreateFence");
        }
        imagesInFlight_.assign(swapchainImages_.size(), VK_NULL_HANDLE);
    }

    void MainLoop()
    {
        MSG message {};
        bool running = true;
        auto previousTime = std::chrono::steady_clock::now();
        while (running) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
                if (message.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (running && !IsIconic(window_)) {
                const auto now = std::chrono::steady_clock::now();
                const float elapsed = std::min(
                    std::chrono::duration<float>(now - previousTime).count(), 0.1F);
                previousTime = now;
                UpdateCamera(elapsed);
                DrawFrame();
            } else if (running) {
                previousTime = std::chrono::steady_clock::now();
                WaitMessage();
            }
        }
        CheckVk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }

    void UpdateCamera(float elapsedSeconds)
    {
        const float forward = (keyDown_['W'] ? 1.0F : 0.0F) -
                              (keyDown_['S'] ? 1.0F : 0.0F);
        const float right = (keyDown_['D'] ? 1.0F : 0.0F) -
                            (keyDown_['A'] ? 1.0F : 0.0F);
        constexpr float kMovementSpeed = 2.5F;
        camera_.Move(forward * kMovementSpeed * elapsedSeconds,
                     right * kMovementSpeed * elapsedSeconds);
    }

    void DrawFrame()
    {
        CheckVk(vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE,
                                UINT64_MAX),
                "vkWaitForFences");

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                                imageAvailable_[currentFrame_],
                                                VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            RecreateSwapchain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            CheckVk(result, "vkAcquireNextImageKHR");
        }
        if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE) {
            CheckVk(vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex], VK_TRUE,
                                    UINT64_MAX),
                    "vkWaitForFences");
        }
        imagesInFlight_[imageIndex] = inFlight_[currentFrame_];

        RecordCommandBuffer(imageIndex);

        CheckVk(vkResetFences(device_, 1, &inFlight_[currentFrame_]),
                "vkResetFences");
        const VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable_[currentFrame_];
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished_[currentFrame_];
        CheckVk(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlight_[currentFrame_]),
                "vkQueueSubmit");

        VkPresentInfoKHR presentInfo {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished_[currentFrame_];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain_;
        presentInfo.pImageIndices = &imageIndex;
        result = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            framebufferResized_) {
            framebufferResized_ = false;
            RecreateSwapchain();
        } else if (result != VK_SUCCESS) {
            CheckVk(result, "vkQueuePresentKHR");
        }
        if (smokeTest_ && ++presentedFrames_ >= 3) {
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }
        currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
    }

    void RecreateSwapchain()
    {
        RECT client {};
        do {
            GetClientRect(window_, &client);
            if (client.right == client.left || client.bottom == client.top) {
                WaitMessage();
            }
        } while (client.right == client.left || client.bottom == client.top);

        CheckVk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
        CleanupSwapchain();
        CreateSwapchain();
        CreateImageViews();
        CreateRenderPass();
        CreateGraphicsPipeline();
        CreateFramebuffers();
        CreateCommandBuffers();
        imagesInFlight_.assign(swapchainImages_.size(), VK_NULL_HANDLE);
    }

    void CleanupSwapchain()
    {
        if (!commandBuffers_.empty()) {
            vkFreeCommandBuffers(device_, commandPool_,
                                 static_cast<uint32_t>(commandBuffers_.size()),
                                 commandBuffers_.data());
            commandBuffers_.clear();
        }
        for (VkFramebuffer framebuffer : framebuffers_) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        framebuffers_.clear();
        if (graphicsPipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
            graphicsPipeline_ = VK_NULL_HANDLE;
        }
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
        for (VkImageView imageView : swapchainImageViews_) {
            vkDestroyImageView(device_, imageView, nullptr);
        }
        swapchainImageViews_.clear();
        swapchainImages_.clear();
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void Cleanup()
    {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
            CleanupSwapchain();
            for (size_t index = 0; index < kFramesInFlight; ++index) {
                if (imageAvailable_[index] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, imageAvailable_[index], nullptr);
                }
                if (renderFinished_[index] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, renderFinished_[index], nullptr);
                }
                if (inFlight_[index] != VK_NULL_HANDLE) {
                    vkDestroyFence(device_, inFlight_[index], nullptr);
                }
            }
            if (descriptorPool_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
            }
            if (pipelineLayout_ != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            }
            if (descriptorSetLayout_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
            }
            gaussianBuffer_.Reset();
            if (commandPool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, commandPool_, nullptr);
            }
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    static constexpr const wchar_t* kWindowClassName = L"Simple3dgsEngineWindow";
    HWND window_ = nullptr;
    bool windowClassRegistered_ = false;
    bool framebufferResized_ = false;
    bool smokeTest_ = false;
    uint32_t presentedFrames_ = 0;
    bool mouseDragging_ = false;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    std::array<bool, 256> keyDown_ {};
    simple_3dgs::Camera camera_;
    std::vector<simple_3dgs::Gaussian> gaussians_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    QueueFamilies queueFamilies_;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_ {};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    simple_3dgs::GaussianGpuBuffer gaussianBuffer_;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    std::array<VkSemaphore, kFramesInFlight> imageAvailable_ {};
    std::array<VkSemaphore, kFramesInFlight> renderFinished_ {};
    std::array<VkFence, kFramesInFlight> inFlight_ {};
    std::vector<VkFence> imagesInFlight_;
    size_t currentFrame_ = 0;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    try {
        VulkanApplication application;
        application.Run(instance, showCommand);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Simple 3DGS Engine", MB_OK | MB_ICONERROR);
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
