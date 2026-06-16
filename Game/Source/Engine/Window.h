#pragma once
#include <iostream>

struct GLFWwindow;
namespace engine {
	struct WindowSpecs {
		std::string Title;
		uint32_t Width;
		uint32_t Height;
		float ScrollX = 0.0f;
		float ScrollY = 0.0f;
		bool Resized = false;
	};

	class Window final {
	public:
		Window(const WindowSpecs& specs);
		~Window();

		bool ShoudClose() const;
		void PollEvents() const;
		void ResetResizedFlag();
		float GetTime() const;
		const WindowSpecs& GetSpecs() const { return _specs; }
		WindowSpecs& GetSpecs() { return _specs; }
		GLFWwindow* GetHandle() const { return _handle; }
	private:
		WindowSpecs _specs;
		GLFWwindow* _handle = nullptr;

		static void glfw_resizeCallback(GLFWwindow* handle, int width, int height);
		static void glfw_scrollCallback(GLFWwindow* handle, double x, double y);
	};
}