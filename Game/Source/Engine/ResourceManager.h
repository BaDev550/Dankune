#pragma once
#include "Engine/Core.h"
#include "Engine/Resource.h"
#include "Engine/RenderContext.h"
#include <map>

namespace engine {
	class ResourceManager final {
	public:
		uint8_t* LoadRawPixelData(const std::string& path, render::Image::Specs& specs, size_t& dataSize);
		Ref<render::Texture> LoadTexture(const std::string& path);
		Ref<render::Texture> LoadTextureAtlas(const std::string& path) {} // TODO implement

		[[nodiscard]] Ref<render::Texture> GetTexture(ResourceUUID id);
		[[nodiscard]] Ref<render::Texture> GetTexture(const std::string& path);

		[[nodiscard]] bool HasResource(const std::string& path) const;
		[[nodiscard]] bool HasResource(ResourceUUID id) const { return _resources.contains(id); }

		[[nodiscard]] const std::vector<uint32_t> CompileShaderToBinary(const std::filesystem::path& filePath);
		[[nodiscard]] uint32_t LoadShader(
			const std::string& name,
			const std::filesystem::path& vertPath,
			const std::filesystem::path& fragPath,
			const VkVertexInputBindingDescription* bdesc = {},
			const std::vector<VkVertexInputAttributeDescription>& desc = {},
			const VkPushConstantRange* pc = {}
		);
		[[nodiscard]] uint32_t GetShader(const std::string& name);

		template<class T>
		void RegisterResource(Ref<T>& resource, ResourceUUID id, const std::string& path = "") {
			_resources[id] = resource;
			_resources[id]->_resource_path = path;
			_resources[id]->_resource_id = id;
		}
		void RemoveResource(ResourceUUID id) { _resources.erase(id); }

		template<class T>
		Ref<T> GetResource(ResourceUUID id) {
			static_assert(std::is_base_of_v<Resource, T>, "T needs to be inherited from Resource");
			auto it = _resources.find(id);
			ASSERT((it != _resources.end()) && "Resource ID is not registered to resource manager!");
			Ref<T> rR = std::static_pointer_cast<T>(it->second);
			ASSERT(rR && "Failed to cast Resource to T");
			return rR;
		}
	private:
		std::map<ResourceUUID, Ref<Resource>> _resources;
	};
}