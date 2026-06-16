#pragma once

#include "Engine/Core.h"
#include "Engine/Keys.h"
#include <glm/glm.hpp>

struct GLFWwindow;
namespace engine {
	class InputManager final {
	public:
		InputManager(GLFWwindow* window);
		void Update();

		[[nodiscard]] bool IsKeyPressed(int keyCode) const;
		[[nodiscard]] bool IsKeyReleased(int keyCode) const;

		[[nodiscard]] bool IsMouseButtonPressed(int buttonCode) const;
		[[nodiscard]] bool IsMouseButtonReleased(int buttonCode) const;

		glm::vec2 GetMousePos() const { return _cachedMousePos; }
		float GetMouseScrollDelta();
	private:
		GLFWwindow* _windowHandle;
		bool _keyStates[256] = { false };
		bool _mouseButtonStates[256] = { false };
		float _mouseScroll = 0.0f;
		glm::vec2 _cachedMousePos;
	};
}