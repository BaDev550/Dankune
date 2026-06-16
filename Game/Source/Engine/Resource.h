#pragma once
#include <string>

namespace engine {
	enum class ResourceType {
		None = 0,
		Scene,
		Audio,
		Texture
	};

	using ResourceUUID = uint64_t;
	class Resource {
	public:
		virtual ~Resource() = default;
		virtual ResourceType GetResourceType() = 0;

		ResourceUUID _resource_id = 0;
		ResourceType _resource_type = ResourceType::None;
		std::string _resource_path = "";
	};
}