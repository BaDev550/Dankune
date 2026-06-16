#define VMA_IMPLEMENTATION
#include "RenderContext.h"
#include "Engine/Core.h"
#include "Engine/Engine.h"
#include "Engine/Application.h"
#include "Engine/Object.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <array>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>

namespace engine::render {
	static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData) {

		LOG("[Vulkan Debug]: %s", pCallbackData->pMessage);
		return VK_FALSE;
	}

	RenderContext::RenderContext(Engine& engine) : _engine(engine) {
		_callbackAllocator.pUserData = nullptr;
		_callbackAllocator.pfnAllocation = [](void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope) -> void* { return Allocator::Allocate(size, alignment); };
		_callbackAllocator.pfnFree = [](void* pUserData, void* pMemory) { Allocator::Free(pMemory, 0); };
		_callbackAllocator.pfnReallocation = [](void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope) -> void* { return Allocator::Reallocate(pOriginal, size, alignment); };
		_callbackAllocator.pfnInternalFree = nullptr;
		_callbackAllocator.pfnInternalAllocation = nullptr;

		try {
			CreateInstance();
			CreateSurface();
			PickPhysicalDevice();
			CreateLogicalDevice();
			SetupMemoryAllocator();
			CreateSwapchain();
			CreateCommandPool();
			CreateRenderResources();
			CreateGlobalSampler();

			bindless_CreateDescriptorPool();
			bindless_CreateSetLayout();
			bindless_CreateDescriptorSet();

			globalUniform_CreateDescriptorPool();
			globalUniform_CreateSetLayout();
			globalUniform_AllocDescriptorSets();

			imgui_CreateDescriptorPool();
			imgui_CreateContext();
		}	
		catch (const std::exception& e) {
			LOG("Failed to create RenderContext: %s", e.what());
		}
	}

	RenderContext::~RenderContext() {
		if (_spriteData.SpriteVBO) _spriteData.SpriteVBO = nullptr;
		if (_spriteData.SpriteIBO) _spriteData.SpriteIBO = nullptr;

		if (_imguiDescriptorPool) {
			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			vkDestroyDescriptorPool(_device, _imguiDescriptorPool, &_callbackAllocator);
		}

		if (_globalDescriptorSet) vkFreeDescriptorSets(_device, _uniformPool, 1, &_globalDescriptorSet);
		if (_globalLayout) vkDestroyDescriptorSetLayout(_device, _globalLayout, &_callbackAllocator);
		if (_uniformPool) vkDestroyDescriptorPool(_device, _uniformPool, &_callbackAllocator);

		if (_bindlessGlobalDescriptorSet) vkFreeDescriptorSets(_device, _bindlessDescriptorPool, 1, &_bindlessGlobalDescriptorSet);
		if (_bindlessLayout) vkDestroyDescriptorSetLayout(_device, _bindlessLayout, &_callbackAllocator);
		if (_bindlessDescriptorPool) vkDestroyDescriptorPool(_device, _bindlessDescriptorPool, &_callbackAllocator);
		if (_globalSampler) vkDestroySampler(_device, _globalSampler, &_callbackAllocator);

		if (_timelineSemaphore) vkDestroySemaphore(_device, _timelineSemaphore, &_callbackAllocator);

		for (auto& frame : _frameResources) {
			vkDestroySemaphore(_device, frame.ImageAcquiredSemaphore, &_callbackAllocator);
			vkDestroyCommandPool(_device, frame.CommandPool, &_callbackAllocator);
		}

		pipelineCache_Destroy();
		for (auto& [hash, pipeline] : _pipelines) {
			vkDestroyPipelineLayout(_device, _pipelineLayouts[hash], &_callbackAllocator);
			vkDestroyPipeline(_device, _pipelines[hash], &_callbackAllocator);
		}

		destroySwapchain();
		if (_vmaAllocator)
			vmaDestroyAllocator(_vmaAllocator);
		if (_device != VK_NULL_HANDLE) vkDestroyDevice(_device, &_callbackAllocator);
		if (_surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(_instance, _surface, &_callbackAllocator);
		if (_instance != VK_NULL_HANDLE) vkDestroyInstance(_instance, &_callbackAllocator);
	}

	void RenderContext::SetupMemoryAllocator()
	{
		VmaVulkanFunctions vmaFuncs{};
		VmaAllocatorCreateInfo vmaAllocatorInfo{};
		vmaAllocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		vmaAllocatorInfo.physicalDevice = _physicalDevice;
		vmaAllocatorInfo.device = _device;
		vmaAllocatorInfo.instance = _instance;
		vmaAllocatorInfo.pVulkanFunctions = &vmaFuncs;
		vmaAllocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

		if (vmaCreateAllocator(&vmaAllocatorInfo, &_vmaAllocator) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create VMA");
		}
	}

	void RenderContext::CreateInstance() {
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pNext = nullptr;
		appInfo.pApplicationName = _engine.GetApplication()->GetWindow()->GetSpecs().Title.c_str();
		appInfo.engineVersion = VK_MAKE_VERSION(_engine.GetSpecs().VersionMajor, _engine.GetSpecs().VersionMinor, 0);
		appInfo.apiVersion = VK_API_VERSION_1_3;
		
		_extensions = getRequiredExtensions();
		if (_enableValidationLayers) {
			_validationLayers.push_back("VK_LAYER_KHRONOS_validation");
		}

		VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
		debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugInfo.pfnUserCallback = VulkanDebugCallback;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		if (_enableValidationLayers) { createInfo.pNext = &debugInfo;
		} else { createInfo.pNext = nullptr; }
		createInfo.enabledExtensionCount = static_cast<uint32_t>(_extensions.size());
		createInfo.ppEnabledExtensionNames = _extensions.data();
		createInfo.enabledLayerCount = static_cast<uint32_t>(_validationLayers.size());
		createInfo.ppEnabledLayerNames = _validationLayers.data();
		createInfo.pApplicationInfo = &appInfo;

		if (VkResult result = vkCreateInstance(&createInfo, &_callbackAllocator, &_instance); result != VK_SUCCESS) {
			throw std::runtime_error("Failed to create Vulkan instance!");
		}
		LOG("Vulkan instance created successfully!");
	}

	void RenderContext::PickPhysicalDevice() {
		uint32_t physicalDeviceCount = 0;
		vkEnumeratePhysicalDevices(_instance, &physicalDeviceCount, nullptr);
		std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
		vkEnumeratePhysicalDevices(_instance, &physicalDeviceCount, physicalDevices.data());

		if (physicalDeviceCount) {
			_physicalDevice = physicalDevices[0];
			for (auto& pd : physicalDevices) {
				VkPhysicalDeviceProperties props{};
				vkGetPhysicalDeviceProperties(pd, &props);
				if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
					_physicalDevice = pd;
					break;
				}
			}
		}

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, surfaceFormats.data());

		bool formatSupported = false;
		for (const VkSurfaceFormatKHR& sFormat : surfaceFormats) {
			if (sFormat.format == _swapchainImageFormat) {
				formatSupported = true;
				break;
			}
		}

		if (!formatSupported) {
			throw std::runtime_error("Selected GPU surface doesnt support the swapchain image format");
		}

		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(_physicalDevice, &deviceProperties);
		LOG("Selected GPU: %s", deviceProperties.deviceName);
	}

	void RenderContext::CreateLogicalDevice() {
		_graphicsPresentQueueFamilyIndex = type_GetGraphicsAndPresentQueueFamilyIndexFromOwningDevice();
		float queuePriority = 1.0f;

		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = _graphicsPresentQueueFamilyIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		{
			VkPhysicalDeviceVulkan13Features supportedFeatures13{};
			supportedFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
			supportedFeatures13.pNext = nullptr;
			VkPhysicalDeviceVulkan12Features supportedFeatures12{};
			supportedFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
			supportedFeatures12.pNext = &supportedFeatures13;

			VkPhysicalDeviceFeatures2 supportedFeatures{};
			supportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			supportedFeatures.pNext = &supportedFeatures12;
			vkGetPhysicalDeviceFeatures2(_physicalDevice, &supportedFeatures);

			if (!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 || !supportedFeatures12.timelineSemaphore) {
				throw std::runtime_error("Physical device doesnt meet the feature requirments");
			}
		}

		VkPhysicalDeviceVulkan13Features features13{};
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		features13.synchronization2 = VK_TRUE;
		features13.dynamicRendering = VK_TRUE;
		features13.pNext = nullptr;

		VkPhysicalDeviceVulkan12Features features12{};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		features12.timelineSemaphore = VK_TRUE;
		features12.descriptorIndexing = VK_TRUE;
		features12.bufferDeviceAddress = VK_TRUE;
		features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
		features12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
		features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
		features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
		features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
		features12.descriptorBindingPartiallyBound = VK_TRUE;
		features12.runtimeDescriptorArray = VK_TRUE;
		features12.pNext = &features13;
		
		VkPhysicalDeviceFeatures2 features{};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features.features.samplerAnisotropy = VK_TRUE;
		features.features.robustBufferAccess = VK_TRUE;
		features.pNext = &features12;

		const std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.flags = 0;
		createInfo.pNext = &features;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.queueCreateInfoCount = 1;
		createInfo.ppEnabledLayerNames = nullptr;
		createInfo.enabledLayerCount = 0;
		createInfo.ppEnabledExtensionNames = deviceExtensions.data();
		createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());

		if (VkResult result = vkCreateDevice(_physicalDevice, &createInfo, &_callbackAllocator, &_device); result != VK_SUCCESS) {
			throw std::runtime_error("Failed to create logical device!");
		}
		vkGetDeviceQueue(_device, _graphicsPresentQueueFamilyIndex, 0, &_graphicsQueue);
		LOG("Logical device created successfully!");
	}

	void RenderContext::CreateSurface() {
		if (VkResult result = glfwCreateWindowSurface(_instance, _engine.GetApplication()->GetWindow()->GetHandle(), &_callbackAllocator, &_surface); result != VK_SUCCESS) {
			throw std::runtime_error("Failed to create window surface!");
		}
	}

	void RenderContext::CreateSwapchain() {
		_swapchainExtent[0] = _engine.GetApplication()->GetWindow()->GetSpecs().Width;
		_swapchainExtent[1] = _engine.GetApplication()->GetWindow()->GetSpecs().Height;
		VkSurfaceCapabilitiesKHR surfaceCaps{};
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physicalDevice, _surface, &surfaceCaps) != VK_SUCCESS) {
			throw std::runtime_error("Failed to get surface capabilities");
		}
		uint32_t requestedImageCount = std::max(2u, surfaceCaps.minImageCount);
		if (surfaceCaps.maxImageCount > 0)
			requestedImageCount = std::min(requestedImageCount, surfaceCaps.maxImageCount);

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = _surface;
		createInfo.minImageCount = requestedImageCount;
		createInfo.imageFormat = _swapchainImageFormat;
		createInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
		createInfo.imageExtent = VkExtent2D(_swapchainExtent[0], _swapchainExtent[1]);
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.preTransform = surfaceCaps.currentTransform;
		createInfo.presentMode = _vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;

		if (vkCreateSwapchainKHR(_device, &createInfo, &_callbackAllocator, &_swapchain) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create swapchain");
		}

		uint32_t imageCount = 0;
		vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, nullptr);
		_swapchainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, _swapchainImages.data());
		_swapchainImageViews.resize(imageCount);

		for (size_t i = 0; i < _swapchainImages.size(); i++) {
			VkImageViewCreateInfo imgViewCreateInfo{};
			imgViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imgViewCreateInfo.image = _swapchainImages[i];
			imgViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imgViewCreateInfo.format = _swapchainImageFormat;
			imgViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imgViewCreateInfo.subresourceRange.levelCount = 1;
			imgViewCreateInfo.subresourceRange.baseMipLevel = 0;
			imgViewCreateInfo.subresourceRange.layerCount = 1;
			imgViewCreateInfo.subresourceRange.baseArrayLayer = 0;
			if (vkCreateImageView(_device, &imgViewCreateInfo, &_callbackAllocator, &_swapchainImageViews[i]) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create swapchain image view");
			}
		}

		_renderCompleteSemaphores.resize(_swapchainImages.size());
		for (VkSemaphore& s : _renderCompleteSemaphores) {
			VkSemaphoreCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			if (vkCreateSemaphore(_device, &createInfo, &_callbackAllocator, &s) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create binary semephore!");
			}
		}

		VkImageCreateInfo depthImageCreateInfo{};
		depthImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		depthImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		depthImageCreateInfo.format = _swapchainDepthFormat;
		depthImageCreateInfo.extent = VkExtent3D(_swapchainExtent[0], _swapchainExtent[1], 1.0f);
		depthImageCreateInfo.mipLevels = 1;
		depthImageCreateInfo.arrayLayers = 1;
		depthImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		depthImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		depthImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		depthImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		if (vmaCreateImage(_vmaAllocator, &depthImageCreateInfo, &allocInfo, &_swapchainDepthImage, &_swapchainDepthImageAllocation, nullptr) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create depth buffer on swapchainnnn");
		}

		VkImageViewCreateInfo depthImgViewCreateInfo{};
		depthImgViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		depthImgViewCreateInfo.image = _swapchainDepthImage;
		depthImgViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthImgViewCreateInfo.format = _swapchainDepthFormat;
		depthImgViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		depthImgViewCreateInfo.subresourceRange.levelCount = 1;
		depthImgViewCreateInfo.subresourceRange.baseMipLevel = 0;
		depthImgViewCreateInfo.subresourceRange.layerCount = 1;
		depthImgViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		if (vkCreateImageView(_device, &depthImgViewCreateInfo, &_callbackAllocator, &_swapchainDepthImageView) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create swapchain image view");
		}
	}

	void RenderContext::CreateCommandPool() {
		for (Frame& frame : _frameResources) {
			VkCommandPoolCreateInfo poolCreateInfo{};
			poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolCreateInfo.queueFamilyIndex = _graphicsPresentQueueFamilyIndex;
			if (vkCreateCommandPool(_device, &poolCreateInfo, &_callbackAllocator, &frame.CommandPool) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create command pool for frame!");
			}

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = frame.CommandPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;

			if (vkAllocateCommandBuffers(_device, &allocInfo, &frame.CommandBuffer) != VK_SUCCESS) {
				throw std::runtime_error("Failed to alloc command buffer for frame!");
			}
		}
	}

	void RenderContext::CreateRenderResources() {
		VkSemaphoreTypeCreateInfo semaphoreTypeInfo{};
		semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		semaphoreTypeInfo.initialValue = MaxFramesInFlight;

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		semaphoreInfo.pNext = &semaphoreTypeInfo;
		if (vkCreateSemaphore(_device, &semaphoreInfo, &_callbackAllocator, &_timelineSemaphore) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create timeline semaphore!");
		}

		for (Frame& frame : _frameResources) {
			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			if (vkCreateSemaphore(_device, &semaphoreInfo, &_callbackAllocator, &frame.ImageAcquiredSemaphore) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create frame resource!");
			}
		}

		pipelineCache_Read();
	}

	void RenderContext::CreateGlobalSampler() {
		float maxAnisotropy = (type_GetPhysicalDeviceLimits().maxSamplerAnisotropy >= 16.0f) ? 16.0f : type_GetPhysicalDeviceLimits().maxSamplerAnisotropy;
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = maxAnisotropy;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

		if (vkCreateSampler(_device, &samplerInfo, &_callbackAllocator, &_globalSampler) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create global sampler!");
		}
	}

	void RenderContext::CreateDefaultVBOAndIBO() {
		size_t vboSize = sizeof(Vertex) * _spriteData.Vertices.size();
		size_t iboSize = sizeof(uint32_t) * _spriteData.Indices.size();

		_spriteData.SpriteVBO = Allocator::AllocRef<Buffer>(vboSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);
		_spriteData.SpriteVBO->Write(_spriteData.Vertices.data(), vboSize);

		_spriteData.SpriteIBO = Allocator::AllocRef<Buffer>(iboSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);
		_spriteData.SpriteIBO->Write(_spriteData.Indices.data(), iboSize);
	}

	uint32_t RenderContext::LoadShader(
		const std::string& name, 
		const std::filesystem::path& vertPath, 
		const std::filesystem::path& fragPath, 
		const VkPushConstantRange* pc, 
		const VkVertexInputBindingDescription* bdesc, 
		const std::vector<VkVertexInputAttributeDescription>& desc
	) {
		auto createModule = [&](const std::filesystem::path& path) -> VkShaderModule {
			const std::string pathStr = path.string();
			const std::vector<uint32_t> spirv = _engine.GetResourceManager()->CompileShaderToBinary(pathStr);
			if (spirv.empty())
				return nullptr;

			const size_t shaderSize = spirv.size() * sizeof(uint32_t);

			VkShaderModuleCreateInfo moduleCreateInfo{};
			moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleCreateInfo.codeSize = shaderSize;
			moduleCreateInfo.pCode = spirv.data();

			VkShaderModule shaderModule;
			if (vkCreateShaderModule(_device, &moduleCreateInfo, &_callbackAllocator, &shaderModule) != VK_SUCCESS) {
				LOG("Failed to load shader shader module cannot be created!");
				return nullptr;
			}
			return shaderModule;
			};

		VkShaderModule vertModule = createModule(vertPath);
		VkShaderModule fragModule = createModule(fragPath);
		_hashedPipelineNames[name] = HashString(name);
		{
			std::vector<VkDescriptorSetLayout> layouts{ _globalLayout, _bindlessLayout };
			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.pSetLayouts = layouts.data();
			pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
			if (pc) {
				pipelineLayoutInfo.pPushConstantRanges = pc;
				pipelineLayoutInfo.pushConstantRangeCount = 1;
			}
			else {
				pipelineLayoutInfo.pPushConstantRanges = nullptr;
				pipelineLayoutInfo.pushConstantRangeCount = 0;
			}

			auto& layout = _pipelineLayouts[_hashedPipelineNames[name]];
			if (vkCreatePipelineLayout(_device, &pipelineLayoutInfo, &_callbackAllocator, &layout) != VK_SUCCESS) {
				LOG("Failed to create pipeline layout!");
				return 0;
			}

			const char* entryPoint = "main";
			std::vector<VkPipelineShaderStageCreateInfo> shaderStages(2);
			shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			shaderStages[0].module = vertModule;
			shaderStages[0].pName = entryPoint;

			shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			shaderStages[1].module = fragModule;
			shaderStages[1].pName = entryPoint;

			VkPipelineVertexInputStateCreateInfo vertInputInfo{};
			vertInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			if (!desc.empty() && bdesc) {
				vertInputInfo.pVertexAttributeDescriptions = desc.data();
				vertInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(desc.size());
				vertInputInfo.pVertexBindingDescriptions = bdesc;
				vertInputInfo.vertexBindingDescriptionCount = 1;
			}
			else {
				vertInputInfo.pVertexAttributeDescriptions = nullptr;
				vertInputInfo.pVertexBindingDescriptions = nullptr;
				vertInputInfo.vertexAttributeDescriptionCount = 0;
				vertInputInfo.vertexBindingDescriptionCount = 0;
			}

			VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
			inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			
			VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
			depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencilInfo.depthTestEnable = VK_TRUE;
			depthStencilInfo.depthWriteEnable = VK_FALSE;
			depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
			depthStencilInfo.stencilTestEnable = VK_FALSE;

			VkPipelineViewportStateCreateInfo viewportInfo{};
			viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportInfo.viewportCount = 1;
			viewportInfo.pViewports = nullptr;
			viewportInfo.scissorCount = 1;
			viewportInfo.pScissors = nullptr;

			VkPipelineRasterizationStateCreateInfo rasterInfo{};
			rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
			rasterInfo.cullMode = VK_CULL_MODE_NONE;
			rasterInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rasterInfo.lineWidth = 1.0f;

			VkPipelineMultisampleStateCreateInfo multiSampleInfo{};
			multiSampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multiSampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineColorBlendAttachmentState attachState{};
			attachState.blendEnable = VK_TRUE;
			attachState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			attachState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			attachState.colorBlendOp = VK_BLEND_OP_ADD;
			attachState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			attachState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			attachState.alphaBlendOp = VK_BLEND_OP_ADD;
			attachState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendStateCreateInfo blendInfo{};
			blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blendInfo.attachmentCount = 1;
			blendInfo.pAttachments = &attachState;

			std::vector<VkDynamicState> dynamicState{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
			dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicState.size());
			dynamicStateInfo.pDynamicStates = dynamicState.data();

			VkPipelineRenderingCreateInfo renderInfo{};
			renderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
			renderInfo.colorAttachmentCount = 1;
			renderInfo.pColorAttachmentFormats = &_swapchainImageFormat;
			renderInfo.depthAttachmentFormat = _swapchainDepthFormat;

			VkGraphicsPipelineCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			createInfo.pNext = &renderInfo;
			createInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
			createInfo.pStages = shaderStages.data();
			createInfo.pVertexInputState = &vertInputInfo;
			createInfo.pInputAssemblyState = &inputAssemblyInfo;
			createInfo.pViewportState = &viewportInfo;
			createInfo.pRasterizationState = &rasterInfo;
			createInfo.pMultisampleState = &multiSampleInfo;
			createInfo.pDepthStencilState = &depthStencilInfo;
			createInfo.pColorBlendState = &blendInfo;
			createInfo.pDynamicState = &dynamicStateInfo;
			createInfo.layout = layout;
			createInfo.renderPass = VK_NULL_HANDLE;

			
			auto& pipeline = _pipelines[_hashedPipelineNames[name]];
			if (vkCreateGraphicsPipelines(_device, _pipelineCache, 1, &createInfo, &_callbackAllocator, &pipeline) != VK_SUCCESS) {
				LOG("Failed to create pipeline!");
				return 0;
			}
			vkDestroyShaderModule(_device, vertModule, &_callbackAllocator);
			vkDestroyShaderModule(_device, fragModule, &_callbackAllocator);
		}
		return _hashedPipelineNames[name];
	}

	void RenderContext::Wait() { vkDeviceWaitIdle(_device); }

	const uint32_t RenderContext::type_GetGraphicsAndPresentQueueFamilyIndex(VkPhysicalDevice physicalDevice) const
	{
		uint32_t queueCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueCount, nullptr);
		std::vector<VkQueueFamilyProperties2> queueFamilies(queueCount, { .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
		vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueCount, queueFamilies.data());

		for (uint32_t i = 0; i < queueFamilies.size(); i++) {
			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, _surface, &presentSupport);

			const auto& qProps = queueFamilies[i];
			if ((qProps.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
				return i;
			}
		}
		return 0;
	}
	const uint32_t RenderContext::type_GetGraphicsAndPresentQueueFamilyIndexFromOwningDevice() const { return type_GetGraphicsAndPresentQueueFamilyIndex(_physicalDevice); }

	const uint32_t RenderContext::type_GetMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
	{
		return 0;
	}

	std::vector<const char*> RenderContext::getRequiredExtensions() {
		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (_enableValidationLayers) {
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		return extensions;
	}

	VkPhysicalDeviceLimits RenderContext::type_GetPhysicalDeviceLimits() const { return GetPhysicalDeviceProperties().limits; }

	void RenderContext::pipelineCache_Read() {
		if (_pipelineCache != VK_NULL_HANDLE)
			pipelineCache_Destroy();

		_pipelineCacheData = _engine.ReadTextFile(_pipelineCacheSavePath);

		VkPipelineCacheCreateInfo cacheInfo{};
		cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		if (!_pipelineCacheData.empty()) {
			cacheInfo.initialDataSize = static_cast<uint32_t>(_pipelineCacheData.size());
			cacheInfo.pInitialData = _pipelineCacheData.data();
			LOG("Pipeline cache loaded!");
		}

		if (vkCreatePipelineCache(_device, &cacheInfo, &_callbackAllocator, &_pipelineCache) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create pipeline cache!");
		}
	}

	void RenderContext::pipelineCache_Write() {
		size_t cacheSize = 0;
		vkGetPipelineCacheData(_device, _pipelineCache, &cacheSize, nullptr);
		if (cacheSize == 0) {
			LOG("Pipeline cache does not exists in the disk!");
			return;
		}

		std::vector<char> cacheData(cacheSize);
		vkGetPipelineCacheData(_device, _pipelineCache, &cacheSize, cacheData.data());

		if (cacheData.data()) {
			_engine.WriteTextFile(_pipelineCacheSavePath, &cacheData);
			LOG("Pipeline cache saved to disk!");
		}
	}

	void RenderContext::pipelineCache_Destroy() {
		pipelineCache_Write();
		vkDestroyPipelineCache(_device, _pipelineCache, &_callbackAllocator);
	}

	void RenderContext::bindless_CreateDescriptorPool() {
		std::vector<VkDescriptorPoolSize> sizes{
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1 }
		};
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
		poolInfo.pPoolSizes = sizes.data();
		if (vkCreateDescriptorPool(_device, &poolInfo, &_callbackAllocator, &_bindlessDescriptorPool) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create bindless pool!");
		}
	}

	void RenderContext::bindless_CreateSetLayout() {
		std::array<VkDescriptorSetLayoutBinding, _bindlessSetMaxBinding> bindings{};
		std::array<VkDescriptorBindingFlags, _bindlessSetMaxBinding> flags{};
		std::array<VkDescriptorType, _bindlessSetMaxBinding> types{
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			VK_DESCRIPTOR_TYPE_SAMPLER
		};

		for (uint32_t i = 0; i < _bindlessSetMaxBinding; i++) {
			bindings[i].binding = i;
			bindings[i].descriptorType = types[i];
			bindings[i].descriptorCount = (types[i] == VK_DESCRIPTOR_TYPE_SAMPLER) ? 1 : 1000;;
			bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
			flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		}

		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
		bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		bindingFlagsInfo.pBindingFlags = flags.data();
		bindingFlagsInfo.bindingCount = _bindlessSetMaxBinding;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		layoutInfo.pNext = &bindingFlagsInfo;
		layoutInfo.bindingCount = _bindlessSetMaxBinding;
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(_device, &layoutInfo, &_callbackAllocator, &_bindlessLayout) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create bindless descriptor set layout!");
		}
	}

	void RenderContext::bindless_CreateDescriptorSet() {
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = _bindlessDescriptorPool;
		allocInfo.pSetLayouts = &_bindlessLayout;
		allocInfo.descriptorSetCount = 1;
		if (vkAllocateDescriptorSets(_device, &allocInfo, &_bindlessGlobalDescriptorSet) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate global descriptor set for bindless!");
		}
		LOG("Global Bindless descriptor set allocated");
	}

	void RenderContext::bindless_WriteGlobalSampler() {
		VkDescriptorImageInfo samplerInfo{};
		samplerInfo.sampler = _globalSampler;
		samplerInfo.imageView = VK_NULL_HANDLE;
		samplerInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = _bindlessGlobalDescriptorSet;
		write.dstBinding = _samplerBinding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		write.pImageInfo = &samplerInfo;

		vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
		LOG("Global sampler bound to descriptor set slot %d successfully.", _bindlessGlobalDescriptorSet);
	}

	void RenderContext::globalUniform_CreateDescriptorPool() {
		std::vector<VkDescriptorPoolSize> poolSizes{ { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _maxDescriptorCountOnGlobalLayout } };

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 1;
		vkCreateDescriptorPool(_device, &poolInfo, &_callbackAllocator, &_uniformPool);
	}

	void RenderContext::globalUniform_CreateSetLayout() {
		std::vector<VkDescriptorSetLayoutBinding> bindings{};
		for (uint32_t i = 0; i < _maxDescriptorCountOnGlobalLayout; i++) {
			VkDescriptorSetLayoutBinding binding{};
			binding.binding = i;
			binding.descriptorCount = 1;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			binding.stageFlags = VK_SHADER_STAGE_ALL;
			bindings.push_back(binding);
		}
		
		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		vkCreateDescriptorSetLayout(_device, &layoutInfo, &_callbackAllocator, &_globalLayout);
	}

	void RenderContext::globalUniform_AllocDescriptorSets() {
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pSetLayouts = &_globalLayout;
		allocInfo.descriptorSetCount = 1;
		allocInfo.descriptorPool = _uniformPool;
		vkAllocateDescriptorSets(_device, &allocInfo, &_globalDescriptorSet);
	}

	void RenderContext::imgui_CreateContext() {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		ImGui_ImplGlfw_InitForVulkan(_engine.GetApplication()->GetWindow()->GetHandle(), true);
		ImGui_ImplVulkan_InitInfo vulkanInfo{};
		vulkanInfo.Instance = _instance;
		vulkanInfo.PhysicalDevice = _physicalDevice;
		vulkanInfo.Device = _device;
		vulkanInfo.QueueFamily = _graphicsPresentQueueFamilyIndex;
		vulkanInfo.Queue = _graphicsQueue;
		vulkanInfo.PipelineCache = _pipelineCache;
		vulkanInfo.DescriptorPool = _imguiDescriptorPool;
		vulkanInfo.MinImageCount = 2;
		vulkanInfo.ImageCount = GetSwapchainImageCount();
		vulkanInfo.UseDynamicRendering = true;

		VkFormat colorFormats[] = { _swapchainImageFormat };
		vulkanInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		vulkanInfo.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
		vulkanInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		vulkanInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		vulkanInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = colorFormats;
		vulkanInfo.PipelineInfoMain.PipelineRenderingCreateInfo.depthAttachmentFormat = _swapchainDepthFormat;
		vulkanInfo.PipelineInfoMain.PipelineRenderingCreateInfo.stencilAttachmentFormat = _swapchainDepthFormat;
		ImGui_ImplVulkan_Init(&vulkanInfo);
	}

	void RenderContext::imgui_CreateDescriptorPool() {
		std::vector<VkDescriptorPoolSize> sizes{
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 }
		};
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 1000;
		poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
		poolInfo.pPoolSizes = sizes.data();
		vkCreateDescriptorPool(_device, &poolInfo, &_callbackAllocator, &_imguiDescriptorPool);
	}

	void RenderContext::imgui_NewFrame() {
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void RenderContext::imgui_Draw(VkCommandBuffer cmd) {
		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	}

	void RenderContext::copy_Buffer(VkCommandBuffer cmd, size_t size, VkBuffer srcBuffer, VkBuffer dstBuffer) {
		VkBufferCopy region{};
		region.size = size;
		vkCmdCopyBuffer(cmd, srcBuffer, dstBuffer, 1, &region);
	}

	void RenderContext::copy_BufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, VkImageLayout imageLayout, uint32_t width, uint32_t height) {
		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageOffset = VkOffset3D(0.0f, 0.0f, 0.0f);
		region.imageExtent = VkExtent3D(width, height, 1.0f);
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.layerCount = 1;
		vkCmdCopyBufferToImage(cmd, buffer, image, imageLayout, 1, &region);
	}

	void RenderContext::loadDataToTexture(const uint8_t* data, size_t dataSize, const Ref<Texture>& texture) {
		auto& image = texture->GetImage();
		size_t bufferSize = (image->GetWidth() * image->GetHeight() * 4);
		
		Ref<Buffer> stagingBuffer = Allocator::AllocRef<Buffer>(
			bufferSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
		);
		stagingBuffer->Write(data);

		uploadTextureToGPUAtRuntime(texture, stagingBuffer);
		trackObject(texture);
	}

	void RenderContext::transition_ImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		VkPipelineStageFlags sourceStage;
		VkPipelineStageFlags dstStage;
		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else {
			LOG("Unsupported layout transition!");
			return;
		}

		vkCmdPipelineBarrier(
			cmd,
			sourceStage, dstStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}

	void RenderContext::bindDescriptors(VkPipelineLayout layout) {
		auto& frame = GetCurrentFrame();
		VkDescriptorSet sets[2] = { _globalDescriptorSet, _bindlessGlobalDescriptorSet };
		vkCmdBindDescriptorSets(frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2, sets, 0, nullptr);
	}

	bool RenderContext::begin_Frame() {
		if (_requireRecreateSwapchain) {
			Wait();
			destroySwapchain();
			CreateSwapchain();
			_engine.GetApplication()->GetWindow()->ResetResizedFlag();
			_requireRecreateSwapchain = false;
		}

		_frameValue = _currentFrame++ % MaxFramesInFlight;
		const uint64_t signalValue = _nextSignalValue++;
		const uint64_t waitValue = signalValue - MaxFramesInFlight;
		auto& frame = _frameResources[_frameValue];

		VkSemaphoreWaitInfo waitInfo{};
		waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
		waitInfo.semaphoreCount = 1;
		waitInfo.pSemaphores = &_timelineSemaphore;
		waitInfo.pValues = &waitValue;
		vkWaitSemaphores(_device, &waitInfo, UINT64_MAX);

		for (auto& id : _imguiTextureDeletionQueue) ImGui_ImplVulkan_RemoveTexture(id);
		_imguiTextureDeletionQueue.clear();
		frame.StagingBuffers.clear();
		frame.RenderObjects.clear();

		vkResetCommandPool(_device, frame.CommandPool, 0);

		VkSemaphore imageAcquireSemaphore = _frameResources[_frameValue].ImageAcquiredSemaphore;
		VkResult acquireResult = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX, imageAcquireSemaphore, VK_NULL_HANDLE, &_imageIndex);

		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
			_requireRecreateSwapchain = true;
			return false;
		}
		else if (acquireResult == VK_SUBOPTIMAL_KHR) {
			_requireRecreateSwapchain = true;
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(frame.CommandBuffer, &beginInfo);

		imgui_NewFrame();

		for (auto& textureToUpload : _pendingTextureUploads) {
			auto& image = textureToUpload.dstTexture->GetImage();
			transition_ImageLayout(frame.CommandBuffer, image->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			copy_BufferToImage(frame.CommandBuffer, textureToUpload.srcStagingBuffer->GetBuffer(), image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, image->GetWidth(), image->GetHeight());
			transition_ImageLayout(frame.CommandBuffer, image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			frame.StagingBuffers.push_back({ textureToUpload.srcStagingBuffer });
		}
		_pendingTextureUploads.clear();

		std::vector<VkImageMemoryBarrier2> layoutBarriers(2);
		layoutBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		layoutBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		layoutBarriers[0].srcAccessMask = 0;
		layoutBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		layoutBarriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		layoutBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		layoutBarriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		layoutBarriers[0].image = _swapchainImages[_imageIndex];
		layoutBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		layoutBarriers[0].subresourceRange.baseMipLevel = 0;
		layoutBarriers[0].subresourceRange.levelCount = 1;
		layoutBarriers[0].subresourceRange.baseArrayLayer = 0;
		layoutBarriers[0].subresourceRange.layerCount = 1;

		layoutBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		layoutBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
		layoutBarriers[1].srcAccessMask = 0;
		layoutBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		layoutBarriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		layoutBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		layoutBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		layoutBarriers[1].image = _swapchainDepthImage;
		layoutBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		layoutBarriers[1].subresourceRange.baseMipLevel = 0;
		layoutBarriers[1].subresourceRange.levelCount = 1;
		layoutBarriers[1].subresourceRange.baseArrayLayer = 0;
		layoutBarriers[1].subresourceRange.layerCount = 1;

		VkDependencyInfo depInfo{};
		depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(layoutBarriers.size());
		depInfo.pImageMemoryBarriers = layoutBarriers.data();
		vkCmdPipelineBarrier2(frame.CommandBuffer, &depInfo);

		VkRenderingAttachmentInfo colorAttachmentInfo{};
		colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachmentInfo.imageView = _swapchainImageViews[_imageIndex];
		colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachmentInfo.clearValue.color = { {0.01f, 0.01f, 0.01f, 1.0f} };

		VkRenderingAttachmentInfo depthAttachmentInfo{};
		depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAttachmentInfo.imageView = _swapchainDepthImageView;
		depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachmentInfo.clearValue.depthStencil = { 1.0f, 0 };

		VkRect2D renderArea{};
		renderArea.offset.x = 0;
		renderArea.offset.y = 0;
		renderArea.extent = VkExtent2D(_swapchainExtent[0], _swapchainExtent[1]);

		VkRenderingInfo renderInfo{};
		renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderInfo.renderArea = renderArea;
		renderInfo.layerCount = 1;
		renderInfo.colorAttachmentCount = 1;
		renderInfo.pColorAttachments = &colorAttachmentInfo;
		renderInfo.pDepthAttachment = &depthAttachmentInfo;

		vkCmdBeginRendering(frame.CommandBuffer, &renderInfo);
		{
			VkViewport viewport{};
			viewport.x = 0;
			viewport.y = 0;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			viewport.width = static_cast<float>(_swapchainExtent[0]);
			viewport.height = static_cast<float>(_swapchainExtent[1]);
			vkCmdSetViewport(frame.CommandBuffer, 0, 1, &viewport);

			VkRect2D scissor{};
			scissor.offset.x = 0;
			scissor.offset.y = 0;
			scissor.extent = VkExtent2D(_swapchainExtent[0], _swapchainExtent[1]);
			vkCmdSetScissor(frame.CommandBuffer, 0, 1, &scissor);
		}

		_frameRecording = true;
		return true;
	}

	void RenderContext::end_Frame() {
		auto& frame = GetCurrentFrame();

		imgui_Draw(frame.CommandBuffer);

		vkCmdEndRendering(frame.CommandBuffer);
		VkImageMemoryBarrier2 presentBarrier{};
		presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		presentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		presentBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		presentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
		presentBarrier.dstAccessMask = 0;
		presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		presentBarrier.image = _swapchainImages[_imageIndex];
		presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		presentBarrier.subresourceRange.baseMipLevel = 0;
		presentBarrier.subresourceRange.levelCount = 1;
		presentBarrier.subresourceRange.baseArrayLayer = 0;
		presentBarrier.subresourceRange.layerCount = 1;

		VkDependencyInfo presentDepInfo{};
		presentDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		presentDepInfo.imageMemoryBarrierCount = 1;
		presentDepInfo.pImageMemoryBarriers = &presentBarrier;
		vkCmdPipelineBarrier2(frame.CommandBuffer, &presentDepInfo);

		vkEndCommandBuffer(frame.CommandBuffer);
		submit_Frame();

		_frameRecording = false;
	}

	void RenderContext::command_DrawVertex(std::string_view pipelineName, const Ref<Buffer>& vertexBuffer, uint32_t vertexCount) {
		VkCommandBuffer cmd = GetCurrentFrame().CommandBuffer;
		VkPipeline pipeline = GetPipeline(pipelineName.data());
		VkPipelineLayout layout = GetPipelineLayout(pipelineName.data());
		VkBuffer vbo = vertexBuffer->GetBuffer();
		VkDeviceSize offsets[] = { 0 };

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		bindDescriptors(layout);
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, offsets);
		vkCmdDraw(cmd, vertexCount, 1, 0, 0);
	}

	void RenderContext::command_DrawIndex(std::string_view pipelineName, const Ref<Buffer>& vertexBuffer, const Ref<Buffer>& indexBuffer, uint32_t indexCount) {
		VkCommandBuffer cmd = GetCurrentFrame().CommandBuffer;
		VkPipeline pipeline = GetPipeline(pipelineName.data());
		VkPipelineLayout layout = GetPipelineLayout(pipelineName.data());

		VkBuffer vbo = vertexBuffer->GetBuffer();
		VkBuffer ibo = indexBuffer->GetBuffer();
		VkDeviceSize offsets[] = { 0 };

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		bindDescriptors(layout);
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, offsets);
		vkCmdBindIndexBuffer(cmd, ibo, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
	}

	void RenderContext::command_DrawSprite(std::string_view pipelineName, uint32_t textureID, const glm::mat4& transform, const glm::vec2& UVoffset, const glm::vec2& UVsize) {
		if (!(_spriteData.SpriteVBO && _spriteData.SpriteIBO)) {
			CreateDefaultVBOAndIBO();
			return;
		}

		EntityPcData pc;
		pc.t = transform;
		pc.s = UVsize;
		pc.o = UVoffset;
		pc.id = textureID;
		command_PushConstant(pipelineName, VK_SHADER_STAGE_VERTEX_BIT, &pc, sizeof(EntityPcData));
		command_DrawIndex(pipelineName, _spriteData.SpriteVBO, _spriteData.SpriteIBO, _spriteData.Indices.size());
	}

	void RenderContext::command_DrawDEntity(std::string_view pipelineName, DEntity& entity) {
		command_DrawSprite(pipelineName, static_cast<uint32_t>(entity.GetSpriteTextureID()), entity.GetMat4(), entity.GetSpriteComponent().UVOffset, entity.GetSpriteComponent().UVScale);
	}

	void RenderContext::command_PushConstant(std::string_view pipelineName, VkShaderStageFlagBits stage, void* data, size_t size, size_t offset) {
		VkCommandBuffer cmd = GetCurrentFrame().CommandBuffer;
		VkPipelineLayout layout = GetPipelineLayout(pipelineName.data());
		vkCmdPushConstants(cmd, layout, stage, offset, size, data);
	}

	void RenderContext::command_BindPipeline(std::string_view pipelineName) {
		VkCommandBuffer cmd = GetCurrentFrame().CommandBuffer;
		VkPipeline pipeline = GetPipeline(pipelineName.data());
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	}

	ImageHandle RenderContext::bindless_RegisterImage(Image& image) {
		ImageHandle handle = static_cast<ImageHandle>(_images.size());
		_images[handle] = image.GetImageView();

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = image.GetImageView();
		imageInfo.sampler = VK_NULL_HANDLE;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstBinding = _textureBinding;
		write.dstSet = _bindlessGlobalDescriptorSet;
		write.descriptorCount = 1;
		write.dstArrayElement = static_cast<uint32_t>(handle);
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
		return handle;
	}

	void RenderContext::bindless_RemoveImage(ImageHandle id) {
		if (auto it = _images.find(id); it != _images.end()) {
			_images.erase(it);
		}
		else { LOG("Image is not controlled by bindless manager!"); }
	}

	void RenderContext::bindless_Bind(VkPipelineLayout layout) {
		auto& frame = GetCurrentFrame();
		vkCmdBindDescriptorSets(frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, _bindlessGlobalSet, 1, &_bindlessGlobalDescriptorSet, 0, nullptr);
	}

	void RenderContext::globalUniform_RegisterUniform(const Buffer& buffer, uint32_t binding) {
		VkDescriptorBufferInfo info{};
		info.buffer = buffer.GetBuffer();
		info.offset = 0;
		info.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet writeSet{};
		writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeSet.dstSet = _globalDescriptorSet;
		writeSet.dstBinding = binding;
		writeSet.dstArrayElement = 0;
		writeSet.descriptorCount = 1;
		writeSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeSet.pBufferInfo = &info;
		vkUpdateDescriptorSets(_device, 1, &writeSet, 0, nullptr);
	}

	void RenderContext::globalUniform_Bind(VkPipelineLayout layout) {
		auto& frame = GetCurrentFrame();
		vkCmdBindDescriptorSets(frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, _uniformGlobalSet, 1, &_globalDescriptorSet, 0, nullptr);
	}

	VkDescriptorSet RenderContext::imgui_AddTexture(const std::string& name, VkImageView view, VkImageLayout layout) {
		if (auto it = _imguiTextureCache.find(name); it != _imguiTextureCache.end() && it->second != 0 && view != VK_NULL_HANDLE) {
			_imguiTextureDeletionQueue.push_back(it->second);
		}
		else if (view == VK_NULL_HANDLE) {
			return it->second;
		}

		_imguiTextureCache[name] = static_cast<VkDescriptorSet>(ImGui_ImplVulkan_AddTexture(_globalSampler, view, layout));
		return _imguiTextureCache[name];
	}

	VkPhysicalDeviceProperties RenderContext::GetPhysicalDeviceProperties() const noexcept
	{
		VkPhysicalDeviceProperties p;
		vkGetPhysicalDeviceProperties(_physicalDevice, &p);
		return p;
	}

	void RenderContext::begin_SingleTimeCommands() {}
	void RenderContext::end_SingleTimeCommands() {}

	void RenderContext::submit_Frame() {
		auto& frame = GetCurrentFrame();

		VkSemaphoreSubmitInfo imageAcquireWaitInfo{};
		imageAcquireWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		imageAcquireWaitInfo.semaphore = frame.ImageAcquiredSemaphore;
		imageAcquireWaitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

		std::vector<VkSemaphoreSubmitInfo> semaphoreSignals(2);
		semaphoreSignals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		semaphoreSignals[0].semaphore = _renderCompleteSemaphores[_imageIndex];
		semaphoreSignals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

		semaphoreSignals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		semaphoreSignals[1].semaphore = _timelineSemaphore;
		semaphoreSignals[1].value = _nextSignalValue++;
		semaphoreSignals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

		VkCommandBufferSubmitInfo cmdSubmitInfo{};
		cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		cmdSubmitInfo.commandBuffer = frame.CommandBuffer;

		VkSubmitInfo2 submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submitInfo.waitSemaphoreInfoCount = 1;
		submitInfo.pWaitSemaphoreInfos = &imageAcquireWaitInfo;
		submitInfo.commandBufferInfoCount = 1;
		submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
		submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignals.size());
		submitInfo.pSignalSemaphoreInfos = semaphoreSignals.data();

		vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &_renderCompleteSemaphores[_imageIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &_swapchain;
		presentInfo.pImageIndices = &_imageIndex;
		presentInfo.pResults = nullptr;

		vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	}

	RenderContext::Frame& RenderContext::GetCurrentFrame()
	{
		ASSERT(_frameRecording);
		return _frameResources[_frameValue];
	}

	void RenderContext::destroySwapchain() {
		for (auto& scImgView : _swapchainImageViews) {
			vkDestroyImageView(_device, scImgView, &_callbackAllocator);
		}
		_swapchainImageViews.clear();

		for (auto& semaphore : _renderCompleteSemaphores) {
			vkDestroySemaphore(_device, semaphore, &_callbackAllocator);
		}
		_renderCompleteSemaphores.clear();

		vkDestroySwapchainKHR(_device, _swapchain, &_callbackAllocator);
		_swapchain = VK_NULL_HANDLE;

		if (_swapchainDepthImage) {
			vkDestroyImageView(_device, _swapchainDepthImageView, &_callbackAllocator);
			vmaDestroyImage(_vmaAllocator, _swapchainDepthImage, _swapchainDepthImageAllocation);
			_swapchainDepthImage = VK_NULL_HANDLE;
		}
	}

	RenderObject::RenderObject() : _context(Engine::Get()->GetRenderContext()) {}

	Texture::Texture(uint32_t width, uint32_t height, VkFormat format, uint8_t* data) {
		Image::Specs imageSpecs{};
		imageSpecs.Width = width;
		imageSpecs.Height = height;
		imageSpecs.Format = format;
		imageSpecs.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		_image = Allocator::AllocRef<Image>(imageSpecs);
	}

	Texture::~Texture() {
		if (_bindlessIndex != ImageHandle::Invalid)
			_context->bindless_RemoveImage(_bindlessIndex);
	}

	const ImageHandle Texture::GetBindlessIndex() {
		if (_bindlessIndex == ImageHandle::Invalid) {
			_bindlessIndex = _context->bindless_RegisterImage(*_image);
		}
		return _bindlessIndex;
	}

	Buffer::Buffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags) {
		_bufferSize = size;
		_bufferUsage = usage;
		_memoryUsage = memoryUsage;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = memoryUsage;
		allocInfo.flags = allocFlags;
		VmaAllocationInfo info{};
		if (vmaCreateBuffer(_context->GetVMA(), &bufferInfo, &allocInfo, &_buffer, &_bufferAllocation, &info) != VK_SUCCESS) {
			LOG("Failed to create buffer!");
			return;
		}
		_mappedPtr = info.pMappedData;
		_allocationSize = info.size;
	}

	Buffer::~Buffer() {
		if (_buffer != VK_NULL_HANDLE)
			vmaDestroyBuffer(_context->GetVMA(), _buffer, _bufferAllocation);
	}

	void Buffer::Write(const void* data, size_t size, size_t offset) {
		if (size == SIZE_MAX) {
			std::memcpy(_mappedPtr, data, _bufferSize);
		}
		else {
			std::memcpy(_mappedPtr, data, size);
		}
	}

	VkDeviceAddress Buffer::GetGPUAddress() const {
		VkBufferDeviceAddressInfo info{};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		info.buffer = _buffer;
		return vkGetBufferDeviceAddress(_context->GetDevice(), &info);
	}

	Image::Image(const Specs& specs) : _imageSpecs(specs) {
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.extent = VkExtent3D(_imageSpecs.Width, _imageSpecs.Height, 1.0f);
		imageInfo.format = _imageSpecs.Format;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.usage = _imageSpecs.Usage;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		
		VmaAllocationInfo info{};
		if (vmaCreateImage(_context->GetVMA(), &imageInfo, &allocInfo, &_image, &_allocation, &info) != VK_SUCCESS) {
			LOG("Failed to create image!");
			return;
		}
	}

	Image::~Image() {
		if (_imageView)
			vkDestroyImageView(_context->GetDevice(), _imageView, &_context->GetCallbackAllocator());
		vmaDestroyImage(_context->GetVMA(), _image, _allocation);
	}

	VkImageView Image::GetImageView()
	{
		if (_imageView == VK_NULL_HANDLE) {
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = _image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = _imageSpecs.Format;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			if (vkCreateImageView(_context->GetDevice(), &viewInfo, &_context->GetCallbackAllocator(), &_imageView) != VK_SUCCESS) {
				LOG("Failed to create image view!");
				return VK_NULL_HANDLE;
			}
		}
		return _imageView;
	}

	namespace compiler {
		shaderc_include_result* FileIncluder::GetInclude(const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t include_depth) {
			auto result = new shaderc_include_result;

			std::filesystem::path includePath = (std::filesystem::current_path() / "resources/shaders" / requested_source);
			std::ifstream file(includePath);

			if (!file.is_open()) {
				result->source_name = "";
				result->content = "File not found";
				return result;
			}

			std::stringstream buffer;
			buffer << file.rdbuf();

			std::string* contentStr = new std::string(buffer.str());
			std::string* nameStr = new std::string(requested_source);

			result->source_name = nameStr->c_str();
			result->source_name_length = nameStr->length();
			result->content = contentStr->c_str();
			result->content_length = contentStr->length();

			result->user_data = new std::pair<std::string*, std::string*>(contentStr, nameStr);
			return result;
		}
	}
}