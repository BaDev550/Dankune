#pragma once

#include "Engine/Object.h"
#include "Engine/Scene.h"
#include "Engine/Core.h"
#include "Engine/RenderContext.h"

namespace engine {
	enum class EditorMode : uint8_t {
		TilePaint = 0,
		Collision,
		EntitySpawn
	};

	enum class StorageLayer : uint8_t {
		Background = 0,
		Foreground = 1,
		Count
	};

	struct Tile {
		glm::vec3 Position{ 0.0f };
		glm::vec3 Rotation{ 0.0f };
		glm::vec2 UVOffset{ 0.0f };
		glm::vec2 UVScale{ 1.0f };
		StorageLayer Layer = StorageLayer::Background;
		render::ImageHandle TextureID = render::ImageHandle::Invalid;

		bool EmptyTile() const { return (TextureID == render::ImageHandle::Invalid); }
	};

	struct TileMapChunk {
		static constexpr int CHUNK_SIZE = 16;

		Tile Tiles[(uint8_t)StorageLayer::Count][CHUNK_SIZE * CHUNK_SIZE];
		Ref<render::Buffer> VBO = nullptr;
		Ref<render::Buffer> IBO = nullptr;
		uint32_t IndexCount = 0;
		bool IsDirty = true;

		void ReconstructMesh(float tileSize, int orginX, int orginY);
	};

	class TileMapConstructor final {
	public:
		void Initialize();
		void SetTile(const Tile& tile);
		Tile* GetTile(int x, int y, StorageLayer layer);
		void Render(render::RenderContext* context);
		void RenderDebugUI();

		bool SaveMap(const std::string& path);
		bool LoadMap(const std::string& path);

		uint32_t GetTextureID() const { return _tilesetTextureID; }
	private:
		int _mapChunkWidth = 32;
		int _mapChunkHeight = 32;
		float _tileSize = 1.0f;
		uint32_t _tilesetTextureID = 0;

		std::vector<TileMapChunk> _chunks;
	};

	class LevelEditor final {
	public:
		constexpr static float _tileSizeInWorld = 1.0f;

		void Initialize(Ref<Scene> targetScene, const std::string& defaultTilesetPath);
		void DrawEditorUI(const Ref<Camera2D>& camera);
		void SetDebugPanelVisibility(bool visible) { _showDebugPanel = visible; }
		bool IsDebugPanelVisible() const { return _showDebugPanel; }

		void SetVisible(bool visible) { _drawLevelEditor = visible; }
		bool IsVisible() const { return _drawLevelEditor; }
	private:
		void DrawGridOverlay(const Ref<Camera2D>& camera);
		void DrawPalette(int tilePixelSize);
		void DrawCollisions() {}
		void DrawEntitySpawner();
		void HandleDrawAndInput(const glm::mat4& viewProj);
	private:
		Ref<Scene> _scene = nullptr;
		Ref<render::Texture> _tileset = nullptr;

		EditorMode _mode = EditorMode::TilePaint;
		StorageLayer _activeLayer = StorageLayer::Background;

		uint32_t _brushTileX = 0;
		uint32_t _brushTileY = 0;
		uint32_t _selectedTileID = 0;

		bool _fastPlace = false;
		bool _drawLevelEditor = false;
		bool _showDebugPanel = false;
		Tile _targetTile;
		TileMapConstructor _tileMapContructor;
	};
}