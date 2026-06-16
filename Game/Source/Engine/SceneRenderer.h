#pragma once
#include "Engine/Object.h"
#include "Engine/RenderContext.h"

namespace engine {
	class Scene;
	class Camera2D;
	class SceneRenderer final {
	public:
		SceneRenderer();

		void DrawEntities(Scene* activeScene, Camera2D* camera);
	private:
		render::RenderContext& _renderContext;

		struct SceneData {
			uint64_t CameraBDA;
		} _sceneData;
		Ref<render::Buffer> _sceneBuffer;

		struct CameraData {
			glm::mat4 View;
			glm::mat4 Projection;
			glm::vec2 Position;
		} _cameraData;
		Ref<render::Buffer> _cameraBuffer;

		uint32_t _mainPipelineId = 0;
		std::string _mainPipelineName = "main_pipeline_NAME";
	};
}