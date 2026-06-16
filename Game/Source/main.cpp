#include "Engine/Engine.h"
#include "Engine/Application.h"

#include <imgui.h>

#include "Engine/MathInclude.h"
#include "Engine/Object.h"
#include "Engine/Scene.h"
#include "Engine/Camera.h"
#include "Engine/InputManager.h"
#include "Engine/LevelEditor.h"

#include "Game/Entity_Player.h"

class Game : public engine::Application {
public:
	Game() {
		engine::WindowSpecs specs{ "Vulkan Game", 800, 800 };
		CreateWindow(specs);
	}

	virtual void OnCreate() override {
		LOG("Game created!");

		_mainScene = Allocator::AllocRef<engine::Scene>();
		_camera = Allocator::AllocRef<engine::Camera2D>();

		if (engine::Engine* DEngine = engine::Engine::Get()) {
			_levelEditor.Initialize(_mainScene, "resources/textures/sprites/texture_atlas/dungeon_atlas.png");

			_player = _mainScene->CreateEntity<Entity_Player>();
		}
	}

	virtual void OnUpdate() override {
		if (GetWindow()->GetSpecs().Resized) {
			_camera->Resized(GetWindow()->GetSpecs().Width, GetWindow()->GetSpecs().Height);
		}

		float scrollDelta = engine::Engine::Get()->GetInput()->GetMouseScrollDelta();
		if (scrollDelta != 0.0f) {
			_scroll -= (scrollDelta * 0.1f) * 5.0f;
			_camera->SetProjection(_scroll);
		}

		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::Button("Level Editor")) {
				_levelEditor.SetVisible(!_levelEditor.IsVisible());
			}

			if (_levelEditor.IsVisible()) {
				ImGui::Separator();

				bool showDebug = _levelEditor.IsDebugPanelVisible();
				if (ImGui::Checkbox("Diagnostics Panel", &showDebug)) {
					_levelEditor.SetDebugPanelVisibility(showDebug);
				}
			}

			ImGui::EndMainMenuBar();
		}

		ImGui::Begin("Scene Debug");

		for (auto& [id, entity] : _mainScene->GetEntities()) {
			if (ImGui::Selectable(entity->GetName().c_str()))
				_selectedEntity = entity;
		}

		if (_selectedEntity) {
			ImGui::Separator();
			ImGui::Text("Properties");

			glm::vec3 pos = _selectedEntity->GetWorldPosition(), rot = _selectedEntity->GetWorldRotation(), scale = _selectedEntity->GetWorldScale();
			if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.1f)) { _selectedEntity->SetWorldPosition(pos); }
			if (ImGui::DragFloat3("Rotation", glm::value_ptr(rot), 0.1f)) { _selectedEntity->SetWorldRotation(rot); }
			if (ImGui::DragFloat3("Scale", glm::value_ptr(scale), 0.1f)) { _selectedEntity->SetWorldScale(scale); }

			ImGui::Text("Sprite details");
			auto texture = engine::Engine::Get()->GetResourceManager()->GetTexture(_selectedEntity->GetSpriteResourceID());
			ImGui::Text("Resource ID: %d", texture->_resource_id);
			ImGui::Text("Texture ID: %d", texture->GetBindlessIndex());
			ImGui::Text("Path: %s", texture->_resource_path.c_str());
		}

		ImGui::End();

		_levelEditor.DrawEditorUI(_camera);
		_mainScene->EditorUpdate(engine::Engine::Get()->GetDeltaTime(), _camera.get());
	}

	virtual void OnDestroy() override {
		LOG("Game destroyed!");
	}
private:
	Ref<engine::Scene> _mainScene;
	Ref<engine::Camera2D> _camera;
	engine::LevelEditor _levelEditor;
	engine::DEntity* _selectedEntity = nullptr;

	Entity_Player* _player;

	float _scroll = 15.0f;
};

int main() {
	{
		Game* game = new Game();
		engine::Engine engine({ 1, 0, game });
		engine.Run();
	}
	LOG("Engine destroyed exiting...");
	return 0;
}