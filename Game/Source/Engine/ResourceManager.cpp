#include "ResourceManager.h"
#include "Engine/Engine.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <shaderc/shaderc.hpp>
#include <fstream>
#include <sstream>

namespace engine {
	uint8_t* ResourceManager::LoadRawPixelData(const std::string& path, render::Image::Specs& specs, size_t& dataSize) {
		stbi_set_flip_vertically_on_load(false);

		int width, height, channels;
		uint8_t* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);

		if (!pixels) {
			LOG("Failed to load image file: %s", path.c_str());
			return nullptr;
		}
		specs.Width = width;
		specs.Height = height;
		specs.Format = VK_FORMAT_R8G8B8A8_UNORM;
		dataSize = (width * height * 4);
		return pixels;
	}

	Ref<render::Texture> ResourceManager::LoadTexture(const std::string& path) {
		if (HasResource(path))
			return GetTexture(path);

		render::Image::Specs specs{};
		size_t dataSize{ 0 };
		uint8_t* pixels = LoadRawPixelData(path, specs, dataSize);

		if (pixels) {
			ResourceUUID id = HashString(path);
			Ref<render::Texture> texture = Allocator::AllocRef<render::Texture>(
				specs.Width,
				specs.Height,
				specs.Format,
				pixels
			);
			RegisterResource<render::Texture>(texture, id, path);

			if (texture) {
				Engine::Get()->GetRenderContext()->loadDataToTexture(pixels, dataSize, texture);
			}
			stbi_image_free(pixels);
			return texture;
		}
		return nullptr;
	}

	const std::vector<uint32_t> ResourceManager::CompileShaderToBinary(const std::filesystem::path& filePath) {
		shaderc_shader_kind kind = (filePath.extension() == ".frag") ? shaderc_fragment_shader : (filePath.extension() == ".vert") ? shaderc_vertex_shader : shaderc_geometry_shader;
		const std::string shaderPath = (std::filesystem::current_path() / ("resources/shaders" / filePath)).string();
		const std::string src = Engine::Get()->ReadTextFile(shaderPath);
		if (src.empty()) {
			LOG("Failed to open shader file: %s", shaderPath.c_str());
			return {};
		}

		LOG("Compiling shader: %s", shaderPath.c_str());
		shaderc::Compiler compiler;
		shaderc::CompileOptions options;
		options.SetIncluder(std::make_unique<render::compiler::FileIncluder>());
		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
		options.SetTargetSpirv(shaderc_spirv_version_1_5);
		options.SetOptimizationLevel(shaderc_optimization_level_performance);
		shaderc::CompilationResult result = compiler.CompileGlslToSpv(src, kind, filePath.string().c_str(), options);
		if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
			LOG("Failed to compile shader: %s", result.GetErrorMessage().c_str());
			return {};
		}
		return std::vector<uint32_t>(result.cbegin(), result.cend());
	}

	uint32_t ResourceManager::LoadShader(const std::string& name, const std::filesystem::path& vertPath, const std::filesystem::path& fragPath, const VkVertexInputBindingDescription* bdesc, const std::vector<VkVertexInputAttributeDescription>& desc, const VkPushConstantRange* pc) {
		if (Engine::Get()) {
			return Engine::Get()->GetRenderContext()->LoadShader(name, vertPath, fragPath, pc, bdesc, desc);
		}
	}

	Ref<render::Texture> ResourceManager::GetTexture(ResourceUUID id) { return GetResource<render::Texture>(id); }
	Ref<render::Texture> ResourceManager::GetTexture(const std::string& path) { return GetResource<render::Texture>(HashString(path)); }
	bool ResourceManager::HasResource(const std::string& path) const { return _resources.contains(HashString(path)); }
}