#pragma once

#include "Engine/Core.h"
#include "Engine/RenderContext.h"
#include "Engine/ResourceManager.h"
#include "Engine/InputManager.h"

#include <filesystem>

namespace engine {
	class Application;
	struct EngineSpecs {
		uint32_t VersionMajor;
		uint32_t VersionMinor;
		Application* App = nullptr;
	};

	class Engine final {
	public:
		Engine(const EngineSpecs& specs);
		~Engine();

		static Engine* Get() { return s_instance; }

		void Run();
		void Destroy();

		void WriteTextFile(const std::string& path, std::vector<char>* data);
		std::string ReadTextFile(const std::string& path);

		float GetDeltaTime() const { return _deltaTime; }
		EngineSpecs& GetSpecs() { return _specs; }
		GarbageCollector& GetGC() { return _garbageCollector; }
		Application* GetApplication() const { return _specs.App; }
		ResourceManager* GetResourceManager() { return _resourceManager.get(); }
		InputManager* GetInput() { return _inputManager.get(); }
		render::RenderContext* GetRenderContext() const { return _renderContext.get(); }
	private:
		EngineSpecs _specs;
		const double _fixedDeltaTime = 1.0 / 60.0;
		float _deltaTime = 0.0f;
		int _gc_collectTimer = static_cast<int>((60.0 * 1.0) / _fixedDeltaTime);
		
		GarbageCollector _garbageCollector;
		Scope<ResourceManager> _resourceManager;
		Scope<render::RenderContext> _renderContext;
		Scope<InputManager> _inputManager;
		static Engine* s_instance;
	};
}