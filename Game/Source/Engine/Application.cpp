#include "Engine/Application.h"
#include "Engine/Engine.h"

namespace engine {
	bool Application::Running() const noexcept { 
		if (_window)
			return (!_window->ShoudClose() && !_forceClose); 
		return !_forceClose;
	}

	void Application::CreateWindow(const WindowSpecs& specs) { _window = Allocator::AllocScope<Window>(specs); }
	void Application::UpdateWindow() const noexcept { if (_window) _window->PollEvents(); }
}