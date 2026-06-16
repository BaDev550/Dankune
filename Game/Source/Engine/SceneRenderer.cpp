#include "Engine/SceneRenderer.h"
#include "Engine/Scene.h"
#include "Engine/Camera.h"

namespace engine {
	SceneRenderer::SceneRenderer() : _renderContext(*Engine::Get()->GetRenderContext()) {
		std::initializer_list<VkVertexInputAttributeDescription> desc{ { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32_SFLOAT, 8 } };
		VkVertexInputBindingDescription bdesc{};
		bdesc.binding = 0;
		bdesc.stride = sizeof(engine::render::Vertex);

		VkPushConstantRange pcRange{};
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pcRange.offset = 0;
		pcRange.size = sizeof(render::EntityPcData);

		_mainPipelineId = Engine::Get()->GetResourceManager()->LoadShader(_mainPipelineName, "shader.vert", "shader.frag", &bdesc, desc, &pcRange);

		{
			_cameraBuffer = Allocator::AllocRef<engine::render::Buffer>(
				sizeof(CameraData),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VMA_MEMORY_USAGE_CPU_TO_GPU,
				VMA_ALLOCATION_CREATE_MAPPED_BIT
			);
			_cameraBuffer->Write(&_cameraData);
		}

		{
			_sceneData.CameraBDA = _cameraBuffer->GetGPUAddress();
			_sceneBuffer = Allocator::AllocRef<engine::render::Buffer>(
				sizeof(SceneData),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VMA_MEMORY_USAGE_CPU_TO_GPU,
				VMA_ALLOCATION_CREATE_MAPPED_BIT
			);
			_sceneBuffer->Write(&_sceneData);
			_renderContext.globalUniform_RegisterUniform(*_sceneBuffer, 0);
		}
	}

	void SceneRenderer::DrawEntities(Scene* activeScene, Camera2D* camera) {
		auto entities = activeScene->GetEntities();

		{
			_cameraData.View = camera->GetViewMatrix();
			_cameraData.Projection = camera->GetProjectionMatrix();
			_cameraData.Position = camera->GetPosition();
			_cameraBuffer->Write(&_cameraData);
		}

		for (auto [id, entity] : entities) {
			if (entity->GetSpriteComponent().IsValid()) {
				Engine::Get()->GetRenderContext()->command_DrawDEntity(_mainPipelineName, *entity);
			}
		}
	}
}