#include "Engine/Core.h"
#include "Engine/Window.h"
#include <GLFW/glfw3.h>

namespace engine {
	Window::Window(const WindowSpecs& specs) : _specs(specs) {
		static bool glfwInitialized = false;
		if (!glfwInitialized) {
			int success = glfwInit();
			if (!success)
				return;

			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		}
		_handle = glfwCreateWindow(_specs.Width, _specs.Height, _specs.Title.c_str(), nullptr, nullptr);
		glfwMakeContextCurrent(_handle);
		glfwSetWindowUserPointer(_handle, &_specs);
		glfwSetFramebufferSizeCallback(_handle, glfw_resizeCallback);
		glfwSetScrollCallback(_handle, glfw_scrollCallback);
		LOG("Window created!");
	}

	Window::~Window() {
		glfwDestroyWindow(_handle);
		glfwTerminate();
	}

	bool Window::ShoudClose() const { return glfwWindowShouldClose(_handle); }
	void Window::PollEvents() const { glfwPollEvents(); }
	void Window::ResetResizedFlag() { _specs.Resized = false; }
	float Window::GetTime() const { return glfwGetTime(); }

	void Window::glfw_resizeCallback(GLFWwindow* handle, int width, int height) {
		WindowSpecs* specs = static_cast<WindowSpecs*>(glfwGetWindowUserPointer(handle));
		if (specs) {
			specs->Width = (uint32_t)width;
			specs->Height = (uint32_t)height;
			specs->Resized = true;
		}
	}

	void Window::glfw_scrollCallback(GLFWwindow* handle, double x, double y) {
		WindowSpecs* specs = static_cast<WindowSpecs*>(glfwGetWindowUserPointer(handle));
		if (specs) {
			specs->ScrollX = x;
			specs->ScrollY = y;
		}
	}
}