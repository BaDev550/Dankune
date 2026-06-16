#include "Engine/Engine.h"
#include "Engine/Application.h"

#include <fstream>
#include <sstream>
#include <thread>

namespace engine {
	Engine* Engine::s_instance = nullptr;
	Engine::Engine(const EngineSpecs& specs) : _specs(specs) {
		ASSERT(s_instance == nullptr && "Engine already exists!");
		s_instance = this;
	}

	Engine::~Engine() { Destroy(); }

	void Engine::Run() {
		if (_specs.App == nullptr) {
			LOG("No application provided to engine!!");
			Destroy();
			return;
		}
		_resourceManager = Allocator::AllocScope<ResourceManager>();
		_renderContext = Allocator::AllocScope<render::RenderContext>(*this);
		_inputManager = Allocator::AllocScope<InputManager>(_specs.App->GetWindow()->GetHandle());
		_specs.App->OnCreate();

		float lastFrame = 0.0f;
		double accumulator = 0.0f;
		int tick_counter = 0;

		while (_specs.App->Running()) {
			float currentFrame = static_cast<float>(_specs.App->GetWindow()->GetTime());
			_deltaTime = currentFrame - lastFrame;
			lastFrame = currentFrame;

			if (_deltaTime > 0.25) _deltaTime = 0.25;
			accumulator += _deltaTime;

			if (_renderContext->begin_Frame()) {
				_specs.App->OnUpdate();

				_renderContext->end_Frame();
			}

			_inputManager->Update();
			_specs.App->UpdateWindow();

			while (accumulator >= _fixedDeltaTime) {
				_specs.App->OnFixedUpdate();
				tick_counter++;

				if (tick_counter >= _gc_collectTimer) {
					_garbageCollector.collect();
					tick_counter = 0;
				}
				accumulator -= _fixedDeltaTime;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	void Engine::Destroy() {
		_renderContext->Wait();

		if (_specs.App) {
			_specs.App->OnDestroy();
			delete _specs.App;
			_specs.App = nullptr;
		}
		s_instance = nullptr;
		_garbageCollector.collect();
		_resourceManager = nullptr;
		_renderContext = nullptr;
	}

	void Engine::WriteTextFile(const std::string& path, std::vector<char>* data) {
		std::ofstream stream(path);
		if (stream.is_open()) {
			stream.write(data->data(), data->size());
			stream.close();
			return;
		}
		LOG("Failed to open file: %s", path.c_str());
		return;
	}

	std::string Engine::ReadTextFile(const std::string& path)
	{
		std::ifstream stream(path);
		if (stream.is_open()) {
			std::stringstream buff;
			buff << stream.rdbuf();
			const std::string out = buff.str();
			stream.close();
			return out;
		}
		return std::string();
	}
}