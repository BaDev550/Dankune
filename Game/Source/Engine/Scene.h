#pragma once

#include "Engine/Object.h"
#include "Engine/SceneRenderer.h"
#include <unordered_map>

namespace engine {
	class Camera2D;
	class Scene final {
	public:
		Scene();
		~Scene();

		template<class T, typename... Args>
		T* CreateEntity(Args&&... args) {
			T* entity = new T(std::forward<Args>(args)...);
			std::string eFallbackName = ("_fallback_entity_" + std::to_string(_entities.size()));
			std::string eName = (!entity->GetName().empty()) ? entity->GetName() : eFallbackName;
			DObjectUUID eId = (entity->_objectID == 0) ? HashString(eName) : entity->_objectID;
			entity->_owningScene = this;
			entity->SetName(eName);
			entity->OnCreate();
			_entities[eId] = entity;
			_hashedEntityNames[eName] = eId;
			return entity;
		}

		DEntity* GetEntity(const std::string& name);
		void DestroyEntity(const std::string& name);

		//static Scene* Get();

		void EditorUpdate(float deltaTime, Camera2D* editorCam);
		void EditorFixedUpdate(float deltaTime);
		std::unordered_map<DObjectUUID, DEntity*>& GetEntities() { return _entities; }
	private:
		std::unordered_map<std::string, DObjectUUID> _hashedEntityNames;
		std::unordered_map<DObjectUUID, DEntity*> _entities;
		Ref<SceneRenderer> _renderer;
	};
}