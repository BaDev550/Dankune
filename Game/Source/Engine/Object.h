#pragma once

#include <map>

#include "Engine/MathInclude.h"
#include "Engine/Core.h"
#include "Engine/Engine.h"

namespace engine {
	class Scene;
	struct TransformComponent {
		glm::vec3 Position{ 0.0f };
		glm::vec3 Rotation{ 0.0f };
		glm::vec3 Scale{ 1.0f };

		[[nodiscard]] glm::mat4 GetMat4() const {
			glm::mat4 m{ 1.0f };
			m = glm::translate(m, Position);
			m = glm::rotate(m, glm::radians(Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
			m = glm::rotate(m, glm::radians(Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
			m = glm::rotate(m, glm::radians(Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
			m = glm::scale(m, Scale);
			return m;
		}
	};

	struct SpriteComponent {
		ResourceUUID ResourceID = 0;
		render::ImageHandle TextureID = render::ImageHandle::Invalid;
		glm::vec2 UVScale = glm::vec2(1.0f);
		glm::vec2 UVOffset = glm::vec2(0.0f);

		bool IsValid() const { return ((ResourceID != 0) && (TextureID != render::ImageHandle::Invalid)); }
	};

	using DObjectUUID = uint64_t;
	class DObject : public GCObject {
	public:
		using Parent = GCObject;
		DObject() { DEngine->GetGC().register_ObjectToGC(this); }
		virtual ~DObject() = default;

		virtual void Destroy() override {
			Parent::Destroy();
			OnDestroy();
		}

		virtual void OnCreate() {};
		virtual void OnUpdate(float deltaTime) {};
		virtual void OnFixedUpdate(float deltaTime) {};
		virtual void OnDestroy() {};

		Scene* _owningScene = nullptr;
		DObjectUUID _objectID = 0;
	protected:
		Engine* DEngine = Engine::Get();
	};

	class DEntity : public DObject {
	public:
		using Parent = DObject;
		DEntity(const std::string& name = "") : _name(name) { if (!_name.empty()) _objectID = HashString(name); }

		void SetOwner(DEntity* entity) { _owner = entity; }
		void AddChild(DEntity* entity) {
			if (_childs.find(entity->_objectID) != _childs.end()) {
				LOG("Entity is already attached to owner!");
				return;
			}
			_childs[entity->_objectID] = entity;
			_childs[entity->_objectID]->SetOwner(this);
		}
		virtual DEntity* GetOwner() const { return _owner; }
		bool HasOwner() const { return _owner; }

		void SetRelativePosition(const glm::vec3& pos) { _transform.Position = pos; }
		void SetRelativeRotation(const glm::vec3& rot) { _transform.Rotation = rot; }
		void SetRelativeScale(const glm::vec3& scale) { _transform.Scale = scale; }
		[[nodiscard]] glm::vec3 GetRelativePosition() const { return _transform.Position; }
		[[nodiscard]] glm::vec3 GetRelativeRotation() const { return _transform.Rotation; }
		[[nodiscard]] glm::vec3 GetRelativeScale() const { return _transform.Scale; }

		void SetWorldPosition(const glm::vec3& pos) { HasOwner() ? (GetOwner()->SetWorldPosition(pos)) : SetRelativePosition(pos); }
		void SetWorldRotation(const glm::vec3& rot) { HasOwner() ? (GetOwner()->SetWorldRotation(rot)) : SetRelativeRotation(rot); }
		void SetWorldScale(const glm::vec3& scale) { HasOwner() ? (GetOwner()->SetWorldScale(scale)) : SetRelativeScale(scale); }
		[[nodiscard]] glm::vec3 GetWorldPosition() const { return HasOwner() ? (GetRelativePosition() + GetOwner()->GetWorldPosition()) : GetRelativePosition(); }
		[[nodiscard]] glm::vec3 GetWorldRotation() const { return HasOwner() ? (GetRelativeRotation() + GetOwner()->GetWorldRotation()) : GetRelativeRotation();; }
		[[nodiscard]] glm::vec3 GetWorldScale() const { return HasOwner() ? (GetRelativeScale() + GetOwner()->GetWorldScale()) : GetRelativeScale(); }
		[[nodiscard]] glm::mat4 GetMat4() const { return HasOwner() ? (_transform.GetMat4() + GetOwner()->GetMat4()) : _transform.GetMat4(); }

		SpriteComponent GetSpriteComponent() const { return _spriteComponent; }
		ResourceUUID GetSpriteResourceID() const { return _spriteComponent.ResourceID; }
		render::ImageHandle GetSpriteTextureID() const { return _spriteComponent.TextureID; }
		void SetSpriteWithCustomUV(const Ref<render::Texture>& texture, const glm::vec2& offset, const glm::vec2& size) {
			if (texture) {
				_spriteComponent.UVOffset = offset;
				_spriteComponent.UVScale = size;
				SetSprite(texture);
			}
		}

		void SetSprite(const Ref<render::Texture>& texture) {
			if (texture) {
				_spriteComponent.ResourceID = texture->_resource_id;
				_spriteComponent.TextureID = texture->GetBindlessIndex();
			}
		}

		void SetSpriteResourceID(ResourceUUID id) { _spriteComponent.ResourceID = id; }
		void LoadSprite(const std::string& path) {
			if (DEngine) {
				if (!_spriteComponent.IsValid()) {
					auto texture = DEngine->GetResourceManager()->LoadTexture(path);
					_spriteComponent.ResourceID = texture->_resource_id;
					_spriteComponent.TextureID = texture->GetBindlessIndex();
				}
			}
		}

		void SetName(const std::string& name) { _name = name; }
		const std::string& GetName() const { return _name; }
		std::string& GetName() { return _name; }
	private:
		std::string _name;
		TransformComponent _transform;
		SpriteComponent _spriteComponent;

		DEntity* _owner = nullptr;
		std::map<DObjectUUID, DEntity*> _childs;
	};
}