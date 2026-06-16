#pragma once
#include "Engine/Core.h"
#include "Engine/Window.h"

#include <vulkan/vulkan.h>
#include <filesystem>

namespace engine {
	class Application {
	public:
		virtual ~Application() = default;

		virtual void OnCreate() = 0;
		virtual void OnUpdate() = 0;
		virtual void OnFixedUpdate() {};
		virtual void OnDestroy() = 0;

		bool Running() const noexcept;
		void CreateWindow(const WindowSpecs& specs);
		void UpdateWindow() const noexcept;

		Window* GetWindow() const { return _window.get(); }
	private:
		Scope<Window> _window;
		bool _forceClose = false;
	};
}