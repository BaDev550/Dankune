#pragma once
#include <iostream>

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine::math {
	static glm::vec2 ScreenSpaceToWorldSpace(float mouseX, float mouseY, float width, float height, const glm::mat4& viewProj) {
		float ndcX = (2.0f * mouseX) / width - 1.0f;
		float ndcY = 1.0f - (2.0f * mouseY) / height;

		glm::mat4 invVP = glm::inverse(viewProj);
		glm::vec4 worldPos = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
		return glm::vec2(worldPos.x / worldPos.w, worldPos.y / worldPos.w);
	}
}