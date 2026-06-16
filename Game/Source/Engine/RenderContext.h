#pragma once

#include "Engine/Core.h"
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>
#include <vma/vk_mem_alloc.h>
#include <unordered_map>
#include <filesystem>
#include <string>
#include <set>
#include <map>
#include <functional>
#include <glm/glm.hpp>

#include "Resource.h"

namespace engine { class Engine; class DObject; class DEntity; }
namespace engine::render {
#define VK_PROC_ADDRESS(name) PFN_##name name = nullptr;
	enum class ImageHandle : uint32_t { Invalid = UINT32_MAX };
	enum class BufferHandle : uint32_t { Invalid = UINT32_MAX };
	struct Vertex { glm::vec2 Position; glm::vec2 UVs; };
	struct EntityPcData { glm::mat4 t; glm::vec2 s; glm::vec2 o; uint32_t id; };
	[[nodiscard]] inline uint32_t padSizeToMinAlignment(uint32_t originalSize, uint32_t minAlignment) { return (originalSize + minAlignment - 1) & ~(minAlignment - 1); }

	class RenderContext;
	class RenderObject {
	public:
		RenderObject();
		virtual ~RenderObject() = default;
	protected:
		RenderContext* _context;
	};

	class Image final : public RenderObject {
	public:
		struct Specs {
			uint32_t Width = 1;
			uint32_t Height = 1;
			VkFormat Format = VK_FORMAT_UNDEFINED;
			VkImageUsageFlags Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			VkImageTiling Tilling = VK_IMAGE_TILING_LINEAR;
		};
	public:
		Image(const Specs& specs);
		virtual ~Image();
		Image(const Image&) = delete;
		Image& operator=(const Image&) = delete;

		[[nodiscard]] const uint32_t GetWidth() const noexcept { return _imageSpecs.Width; }
		[[nodiscard]] const uint32_t GetHeight() const noexcept { return _imageSpecs.Height; }
		[[nodiscard]] VkFormat GetFormat() const noexcept { return _imageSpecs.Format; }
		[[nodiscard]] VkImage GetImage() const noexcept { return _image; }
		[[nodiscard]] VkImageView GetImageView();
		[[nodiscard]] VmaAllocation GetAllocation() const noexcept { return _allocation; }
	private:
		Specs _imageSpecs;
		VkImage _image = VK_NULL_HANDLE;
		VkImageView _imageView = VK_NULL_HANDLE;
		VmaAllocation _allocation;
	};

	class Texture final : public RenderObject, public Resource {
	public:
		Texture(uint32_t width, uint32_t height, VkFormat format, uint8_t* data);
		virtual ~Texture();
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		const ImageHandle GetBindlessIndex();
		const Ref<Image>& GetImage() const { return _image; }
		Ref<Image>& GetImage() { return _image; }

		virtual ResourceType GetResourceType() override { return ResourceType::Texture; }
	private:
		ImageHandle _bindlessIndex = ImageHandle::Invalid;
		Ref<Image> _image;
	};

	class Buffer final : public RenderObject {
	public:
		Buffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags);
		virtual ~Buffer();
		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;

		void Write(const void* data, size_t size = SIZE_MAX, size_t offset = SIZE_MAX);
		const void* GetMappedPtr() const { return _mappedPtr; }
		const size_t GetSize() const { return _bufferSize; }

		[[nodiscard]] VkDeviceAddress GetGPUAddress() const;
		[[nodiscard]] VkBuffer GetBuffer() const { return _buffer; }
		[[nodiscard]] VmaAllocation GetAllocation() const { return _bufferAllocation; }
		[[nodiscard]] VkBufferUsageFlags GetBufferUsage() const { return _bufferUsage; }
	private:
		BufferHandle _bindlessIndex = BufferHandle::Invalid;
		void* _mappedPtr = nullptr;
		size_t _bufferSize = SIZE_MAX;
		size_t _allocationSize = SIZE_MAX;
		VkBuffer _buffer = VK_NULL_HANDLE;
		VmaAllocation _bufferAllocation;
		VkBufferUsageFlags _bufferUsage;
		VkMemoryAllocateFlags _memoryUsage;
	};

	class RenderContext final {
	public:
		constexpr static uint32_t MaxFramesInFlight = 2;
		constexpr static uint32_t _uniformGlobalSet = 0;
		constexpr static uint32_t _bindlessGlobalSet = 1;
		constexpr static uint32_t _textureBinding = 0;
		constexpr static uint32_t _samplerBinding = 1;
		constexpr static uint32_t _bindlessSetMaxBinding = 2;
		constexpr static uint32_t _maxDescriptorCountOnGlobalLayout = 10;
		RenderContext(Engine& engine);
		~RenderContext();

		void SetupMemoryAllocator();
		void CreateInstance();
		void PickPhysicalDevice();
		void CreateLogicalDevice();
		void CreateSurface();
		void CreateSwapchain();
		void CreateCommandPool();
		void CreateRenderResources();
		void CreateGlobalSampler();
		void CreateDefaultVBOAndIBO();
		uint32_t LoadShader(
			const std::string& name, 
			const std::filesystem::path& vertPath, 
			const std::filesystem::path& fragPath, 
			const VkPushConstantRange* pc = {},
			const VkVertexInputBindingDescription* bdesc = {},
			const std::vector<VkVertexInputAttributeDescription>& desc = {}
		);

		bool begin_Frame();
		void begin_SingleTimeCommands();
		void end_SingleTimeCommands();
		void end_Frame();

		void command_DrawVertex(std::string_view pipelineName, const Ref<Buffer>& vertexBuffer, uint32_t vertexCount);
		void command_DrawIndex(std::string_view pipelineName, const Ref<Buffer>& vertexBuffer, const Ref<Buffer>& indexBuffer, uint32_t indexCount);
		void command_DrawSprite(std::string_view pipelineName, uint32_t textureID, const glm::mat4& transform, const glm::vec2& UVoffset = glm::vec2(0), const glm::vec2& UVsize = glm::vec2(0));
		void command_DrawDEntity(std::string_view pipelineName, DEntity& entity);
		void command_PushConstant(std::string_view pipelineName, VkShaderStageFlagBits stage, void* data, size_t size, size_t offset = 0);
		void command_BindPipeline(std::string_view pipelineName);

		ImageHandle bindless_RegisterImage(Image& image);
		void bindless_RemoveImage(ImageHandle id);
		void bindless_Bind(VkPipelineLayout layout);

		void globalUniform_RegisterUniform(const Buffer& buffer, uint32_t binding);
		void globalUniform_Bind(VkPipelineLayout layout);

		VkDescriptorSet imgui_AddTexture(const std::string& name, VkImageView view = VK_NULL_HANDLE, VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED);

		const uint32_t GetFrameIndex() const { return _frameValue; }
		const VmaAllocator GetVMA() const { return _vmaAllocator; }
		VkAllocationCallbacks& GetCallbackAllocator() { return _callbackAllocator; }
		[[nodiscard]] VkInstance GetInstance() const noexcept { return _instance; }
		[[nodiscard]] VkDevice GetDevice() const noexcept { return _device; }
		[[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept { return _physicalDevice; }
		[[nodiscard]] VkPhysicalDeviceProperties GetPhysicalDeviceProperties() const noexcept;
		[[nodiscard]] VkQueue GetGrapichsAndPresentQueue() const noexcept { return _graphicsQueue; }
		[[nodiscard]] VkPipelineCache GetPipelineCache() const noexcept { return _pipelineCache; }
		[[nodiscard]] VkImage GetSwapchainImage(uint32_t index = 0) const noexcept { return _swapchainImages[index]; }
		[[nodiscard]] VkImageView GetSwapchainImageView(uint32_t index = 0) const noexcept { return _swapchainImageViews[index]; }
		[[nodiscard]] uint32_t GetGrapichsAndPresentQueueFamilyIndex() const noexcept { return _graphicsPresentQueueFamilyIndex; }
		[[nodiscard]] uint32_t GetSwapchainImageCount() const noexcept { return static_cast<uint32_t>(_swapchainImages.size()); }
		[[nodiscard]] VkFormat GetSwapchainImageFormat() const noexcept { return _swapchainImageFormat; }
		[[nodiscard]] VkFormat GetSwapchainDepthFormat() const noexcept { return _swapchainDepthFormat; }
		[[nodiscard]] VkDescriptorPool GetBindlessDescriptorPool() const noexcept { return _bindlessDescriptorPool; }
		[[nodiscard]] VkPipeline GetPipeline(const std::string& name) const { return _pipelines.at(_hashedPipelineNames.at(name)); }
		[[nodiscard]] VkPipelineLayout GetPipelineLayout(const std::string& name) const { return _pipelineLayouts.at(_hashedPipelineNames.at(name)); }
	private:
		Engine& _engine;

		VkInstance _instance;
		VkPhysicalDevice _physicalDevice;
		VkDevice _device;
		VkQueue _graphicsQueue;
		uint32_t _graphicsPresentQueueFamilyIndex;
		VkSurfaceKHR _surface;

#pragma region swapchain
		VkSwapchainKHR _swapchain;
		VkFormat _swapchainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
		VkFormat _swapchainDepthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
		std::vector<VkImage> _swapchainImages;
		std::vector<VkImageView> _swapchainImageViews;
		VkImage _swapchainDepthImage;
		VmaAllocation _swapchainDepthImageAllocation;
		VkImageView _swapchainDepthImageView;
		std::vector<VkSemaphore> _renderCompleteSemaphores;
		bool _requireRecreateSwapchain = false;
		bool _vsync = true;
		uint32_t _swapchainExtent[2];
#pragma endregion

#pragma region Vulkan memory && sync management
		VkAllocationCallbacks _callbackAllocator;
		VmaAllocator _vmaAllocator;

		VkSemaphore _timelineSemaphore = VK_NULL_HANDLE;
		struct Frame {
			VkCommandPool CommandPool = VK_NULL_HANDLE;
			VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
			VkSemaphore ImageAcquiredSemaphore = VK_NULL_HANDLE;

			std::vector<Ref<RenderObject>> RenderObjects;
			std::vector<Ref<Buffer>> StagingBuffers;
		} _frameResources[MaxFramesInFlight];

		struct PendingTextureUpload { Ref<Texture> dstTexture; Ref<Buffer> srcStagingBuffer; };
		std::vector<PendingTextureUpload> _pendingTextureUploads;

		bool _frameRecording = false;
		uint32_t _imageIndex = 0;
		uint64_t _currentFrame = 0;
		uint64_t _frameValue = 0;
		uint64_t _nextSignalValue = MaxFramesInFlight;
#pragma endregion

#pragma region Pipeline
		VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
		std::string _pipelineCacheData;
		const char* _pipelineCacheSavePath = "pipeline_cache.bin";
		std::map<std::string, uint64_t> _hashedPipelineNames;
		std::unordered_map<uint64_t, VkPipeline> _pipelines;
		std::unordered_map<uint64_t, VkPipelineLayout> _pipelineLayouts;
#pragma endregion

		VkDescriptorSetLayout _globalLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout _bindlessLayout = VK_NULL_HANDLE;

		VkDescriptorPool _uniformPool = VK_NULL_HANDLE;
		VkDescriptorPool _imguiDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorPool _bindlessDescriptorPool = VK_NULL_HANDLE;

		VkDescriptorSet _bindlessGlobalDescriptorSet = VK_NULL_HANDLE;
		VkDescriptorSet _globalDescriptorSet = VK_NULL_HANDLE;

		VkSampler _globalSampler = VK_NULL_HANDLE;
		std::unordered_map<ImageHandle, VkImageView> _images;

		std::unordered_map<std::string, VkDescriptorSet> _imguiTextureCache;
		std::vector<VkDescriptorSet> _imguiTextureDeletionQueue;

		struct SpriteData {
			const std::vector<Vertex> Vertices = {
				{ { -0.5f, -0.5f },  { 0.0f, 0.0f } },
				{ {  0.5f, -0.5f },  { 1.0f, 0.0f } },
				{ {  0.5f,  0.5f },  { 1.0f, 1.0f } },
				{ { -0.5f,  0.5f },  { 0.0f, 1.0f } }
			};
			const std::vector<uint32_t> Indices = { 0, 1, 2, 2, 3, 0 };
			Ref<Buffer> SpriteVBO = nullptr;
			Ref<Buffer> SpriteIBO = nullptr;
		} _spriteData;
	private:
		void submit_Frame();
		void destroySwapchain();

		std::vector<const char*> getRequiredExtensions();
		VkPhysicalDeviceLimits type_GetPhysicalDeviceLimits() const;
		const uint32_t type_GetGraphicsAndPresentQueueFamilyIndex(VkPhysicalDevice physicalDevice) const;
		const uint32_t type_GetGraphicsAndPresentQueueFamilyIndexFromOwningDevice() const;
		const uint32_t type_GetMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
		std::vector<const char*> _extensions;
		std::vector<const char*> _validationLayers;

		void pipelineCache_Read();
		void pipelineCache_Write();
		void pipelineCache_Destroy();

		void bindless_CreateDescriptorPool();
		void bindless_CreateSetLayout();
		void bindless_CreateDescriptorSet();
		void bindless_WriteGlobalSampler();

		void globalUniform_CreateDescriptorPool();
		void globalUniform_CreateSetLayout();
		void globalUniform_AllocDescriptorSets();

		void imgui_CreateContext();
		void imgui_CreateDescriptorPool();
		void imgui_NewFrame();
		void imgui_Draw(VkCommandBuffer cmd);
	public:
		void Wait();
		void loadDataToTexture(const uint8_t* data, size_t dataSize, const Ref<Texture>& texture);
		void loadDataToBuffer(const void* data, size_t dataSize, const Ref<Buffer>& buffer);
		void copy_Buffer(VkCommandBuffer cmd, size_t size, VkBuffer srcBuffer, VkBuffer dstBuffer);
		void copy_BufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, VkImageLayout imageLayout, uint32_t width, uint32_t height);
		void transition_ImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

		void bindDescriptors(VkPipelineLayout layout);
		void trackObject(Ref<RenderObject> object) { _frameResources[GetFrameIndex()].RenderObjects.push_back(object); }
		void uploadTextureToGPUAtRuntime(const Ref<Texture>& texture, const Ref<Buffer>& buffer) { _pendingTextureUploads.push_back({ texture, buffer }); }

		[[nodiscard]] Frame& GetCurrentFrame();
		friend class RenderObject;
	private:
#if _DEBUG
		bool _enableValidationLayers = true;
#else 
		bool _enableValidationLayers = false;
#endif
	};

	namespace compiler {
		class FileIncluder : public shaderc::CompileOptions::IncluderInterface {
		public:
			shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t include_depth) override;
			void ReleaseInclude(shaderc_include_result* data) override {
				auto extra_data = static_cast<std::pair<std::string*, std::string*>*>(data->user_data);
				delete extra_data->first;
				delete extra_data->second;
				delete extra_data;
				delete data;
			}
		};
	}
}