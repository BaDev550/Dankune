#include "Engine/Camera.h"
#include "Engine/Engine.h"
#include "Engine/Application.h"

namespace engine {
    Camera2D::Camera2D() {
        _ViewMatrix = glm::mat4(1.0f);
        auto window = Engine::Get()->GetApplication()->GetWindow();
        float width = static_cast<float>(window->GetSpecs().Width);
        float height = static_cast<float>(window->GetSpecs().Height);
        _aspectRatio = width / height;
        SetProjection(16.0f);
    }

    void Camera2D::SetProjection(float zoom) {
        _ProjectionMatrix = glm::ortho(-_aspectRatio * zoom, _aspectRatio * zoom, -zoom, zoom, -1.0f, 1.0f);
        _ViewProjectionMatrix = _ProjectionMatrix * _ViewMatrix;
    }

    void Camera2D::Resized(uint32_t width, uint32_t height) {
        float fwidth = static_cast<float>(width);
        float fheight = static_cast<float>(height);
        _aspectRatio = fwidth / fheight;
        SetProjection(16.0f);
    }

    void Camera2D::RecalculateViewMatrix() {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(_Position, 0.0f)) * glm::rotate(glm::mat4(1.0f), glm::radians(_Rotation), glm::vec3(0.0f, 0.0f, 1.0f));
        _ViewMatrix = glm::inverse(transform);
        _ViewProjectionMatrix = _ProjectionMatrix * _ViewMatrix;
    }
}