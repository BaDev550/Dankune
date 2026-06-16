#include "Scene.h"

namespace engine {
	Scene::Scene() {
		_renderer = Allocator::AllocScope<SceneRenderer>();
	}
	Scene::~Scene() {
		for (auto& [id, entity] : _entities) { entity->Destroy(); }
	}

	DEntity* Scene::GetEntity(const std::string& name) {
		if (_hashedEntityNames.contains(name))
			return _entities[_hashedEntityNames.at(name)];
		return nullptr;
	}

	void Scene::DestroyEntity(const std::string& name) {
		auto nameIt = _hashedEntityNames.find(name);
		if (nameIt != _hashedEntityNames.end()) {
			auto entityIt = _entities.find(nameIt->second);
			if (entityIt != _entities.end()) {
				entityIt->second->Destroy();
				_entities.erase(entityIt);
			}
			_hashedEntityNames.erase(nameIt);
		}
	}

	void Scene::EditorUpdate(float deltaTime, Camera2D* editorCam) {
		for (auto& [id, entity] : _entities) {
			entity->OnUpdate(deltaTime);
		}

		_renderer->DrawEntities(this, editorCam);
	}

	void Scene::EditorFixedUpdate(float deltaTime) {
		for (auto& [id, entity] : _entities) { entity->OnFixedUpdate(deltaTime); }
	}
}