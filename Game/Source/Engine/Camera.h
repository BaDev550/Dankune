#pragma once
#include "Engine/MathInclude.h"

namespace engine {
    class Camera2D final {
    public:
        Camera2D();

        void SetProjection(float zoom);

        const glm::vec2& GetPosition() const { return _Position; }
        void SetPosition(const glm::vec2& position) { _Position = position; RecalculateViewMatrix(); }

        float GetRotation() const { return _Rotation; }
        void SetRotation(float rotation) { _Rotation = rotation; RecalculateViewMatrix(); }

        void Resized(uint32_t width, uint32_t height);
        const glm::mat4& GetProjectionMatrix() const { return _ProjectionMatrix; }
        const glm::mat4& GetViewMatrix() const { return _ViewMatrix; }
        const glm::mat4& GetViewProjectionMatrix() const { return _ViewProjectionMatrix; }
    private:
        void RecalculateViewMatrix();

        glm::mat4 _ProjectionMatrix;
        glm::mat4 _ViewMatrix;
        glm::mat4 _ViewProjectionMatrix;

        glm::vec2 _Position = { 0.0f, 0.0f };
        float _Rotation = 0.0f;
        float _Zoom = 1.0f;
        float _aspectRatio;
    };
}