#include "InputManager.h"
#include "Engine.h"
#include "Application.h"
#include <GLFW/glfw3.h>

namespace engine {
	InputManager::InputManager(GLFWwindow* window) : _windowHandle(window) {}

	void InputManager::Update() {
		for (int key = 0; key < 256; key++)
			_keyStates[key] = (glfwGetKey(_windowHandle, key) == GLFW_PRESS);

		for (int button = 0; button < 256; button++)
			_mouseButtonStates[button] = (glfwGetMouseButton(_windowHandle, button) == GLFW_PRESS);

		double x, y;
		glfwGetCursorPos(_windowHandle, &x, &y);
		_cachedMousePos = { x, y };

		auto* window = Engine::Get()->GetApplication()->GetWindow();
		WindowSpecs& specs = window->GetSpecs();

		_mouseScroll = specs.ScrollY;

		specs.ScrollY = 0.0;
		specs.ScrollX = 0.0;
	}

	float InputManager::GetMouseScrollDelta() { return _mouseScroll; }
	bool InputManager::IsKeyPressed(int keyCode) const { return _keyStates[keyCode]; }
	bool InputManager::IsKeyReleased(int keyCode) const { return !_keyStates[keyCode]; }
	bool InputManager::IsMouseButtonPressed(int buttonCode) const { return _mouseButtonStates[buttonCode]; }
	bool InputManager::IsMouseButtonReleased(int buttonCode) const { return !_mouseButtonStates[buttonCode]; }
}