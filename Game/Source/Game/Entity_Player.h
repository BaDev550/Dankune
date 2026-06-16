#pragma once

#include "Engine/Core.h"
#include "Engine/Object.h"
#include "Engine/InputManager.h"

using namespace engine;

class Entity_Player final : public DEntity {
public:
	Entity_Player() : DEntity("Entity_player") {
		LoadSprite("resources/textures/sprites/animations/character/priest3_v1_1.png");
	}

	virtual void OnCreate() override {};

	virtual void OnUpdate(float deltaTime) override {
		//LOG("Hello %d", deltaTime);

		if (DEngine) {
			glm::vec3 pos = GetWorldPosition();
			glm::vec3 rot = GetWorldRotation();
			if (DEngine->GetInput()->IsKeyPressed(DE_KEY_W)) pos.y -= (_speed * deltaTime);
			if (DEngine->GetInput()->IsKeyPressed(DE_KEY_S)) pos.y += (_speed * deltaTime);
			if (DEngine->GetInput()->IsKeyPressed(DE_KEY_A)) {
				pos.x -= (_speed * deltaTime);
				_direction = -1.0f;
			}
			if (DEngine->GetInput()->IsKeyPressed(DE_KEY_D)) {
				pos.x += (_speed * deltaTime);
				_direction = 1.0f;
			}
			SetWorldPosition(pos);
			SetWorldRotation({ rot.x, (_direction < 0.0f) ? 180.0f : 0.0f, rot.z });
		}
	};

	virtual void OnFixedUpdate(float deltaTime) override {};
	virtual void OnDestroy() override {};
private:
	float _speed = 5.0f;
	float _direction = 0.0f;
};