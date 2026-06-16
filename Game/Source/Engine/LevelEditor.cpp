#include "Engine/LevelEditor.h"
#include "Engine/Engine.h"
#include "Engine/Application.h"
#include "Engine/Camera.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <ostream>
#include <fstream>

namespace engine {
	void LevelEditor::Initialize(Ref<Scene> targetScene, const std::string& defaultTilesetPath) {
		_scene = targetScene;
		_tileset = engine::Engine::Get()->GetResourceManager()->LoadTexture(defaultTilesetPath);
        _tileset->GetBindlessIndex();
        _tileMapContructor.Initialize();
	}

	void LevelEditor::DrawEditorUI(const Ref<Camera2D>&camera) {
        if (auto renderContext = Engine::Get()->GetRenderContext()) {
            _tileMapContructor.Render(renderContext);
        }

        if (!_drawLevelEditor) return;

		ImGui::Begin("Editor Toolbar");

        static char mapFileName[128] = "resources/maps/world_01.map";
        ImGui::InputText("Map File Path", mapFileName, IM_ARRAYSIZE(mapFileName));

        if (ImGui::Button("Save Map File")) {
            if (_tileMapContructor.SaveMap(mapFileName)) {
                LOG("Successfully saved tilemap assets to destination disk layout!");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Map File")) {
            if (_tileMapContructor.LoadMap(mapFileName)) {
                LOG("Successfully read map structure arrays and queued pipeline VBO updates!");
            }
        }

		if (ImGui::RadioButton("Tile", _mode == EditorMode::TilePaint)) _mode = EditorMode::TilePaint;
		ImGui::SameLine();
		if (ImGui::RadioButton("Entity", _mode == EditorMode::EntitySpawn)) _mode = EditorMode::EntitySpawn;
        ImGui::Separator();
        ImGui::Text("Active Target Layer:");
        if (ImGui::RadioButton("Background (Floors)", _activeLayer == StorageLayer::Background)) _activeLayer = StorageLayer::Background;
        ImGui::SameLine();
        if (ImGui::RadioButton("Foreground (Objects)", _activeLayer == StorageLayer::Foreground)) _activeLayer = StorageLayer::Foreground;
        ImGui::Checkbox("Fast place", &_fastPlace);
        ImGui::End();

		if (_mode == EditorMode::TilePaint && _tileset) {
			DrawPalette(16);
		}
		else if (_mode == EditorMode::EntitySpawn) {
			DrawEntitySpawner();
		}

        DrawGridOverlay(camera);
		HandleDrawAndInput(camera->GetViewProjectionMatrix());

        if (auto renderContext = Engine::Get()->GetRenderContext()) {
            if (_showDebugPanel) {
                _tileMapContructor.RenderDebugUI();
            }
        }

        if (!_targetTile.EmptyTile()) {
            ImGui::Begin("Place Tile");
            ImGui::DragFloat3("Rotation", glm::value_ptr(_targetTile.Rotation));
            ImGui::Text("UV Offset: %f, UV Scale %f", _targetTile.UVOffset, _targetTile.UVScale);
            if (ImGui::Button("Place")) {
                _tileMapContructor.SetTile(_targetTile);
                _targetTile.TextureID = render::ImageHandle::Invalid;
            }
            ImGui::End();
        }
	}

    void LevelEditor::DrawGridOverlay(const Ref<Camera2D>& camera) {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();

        auto* window = Engine::Get()->GetApplication()->GetWindow();
        float width = (float)window->GetSpecs().Width;
        float height = (float)window->GetSpecs().Height;

        glm::vec2 topLeftWorld = math::ScreenSpaceToWorldSpace(0.0f, 0.0f, width, height, camera->GetViewProjectionMatrix());
        glm::vec2 bottomRightWorld = math::ScreenSpaceToWorldSpace(width, height, width, height, camera->GetViewProjectionMatrix());

        int startX = std::floor(topLeftWorld.x / _tileSizeInWorld);
        int endX = std::ceil(bottomRightWorld.x / _tileSizeInWorld);

        int startY = std::floor(bottomRightWorld.y / _tileSizeInWorld);
        int endY = std::ceil(topLeftWorld.y / _tileSizeInWorld);
        if (startY > endY) std::swap(startY, endY);

        glm::mat4 vp = camera->GetViewProjectionMatrix();

        for (int x = startX; x <= endX; ++x) {
            glm::vec4 worldPos = glm::vec4(x * _tileSizeInWorld, 0.0f, 0.0f, 1.0f);
            glm::vec4 ndc = vp * worldPos;
            if (ndc.w != 0.0f) ndc /= ndc.w;
            float screenX = (ndc.x + 1.0f) * 0.5f * width;

            drawList->AddLine(ImVec2(screenX, 0.0f), ImVec2(screenX, height), IM_COL32(255, 255, 255, 45), 1.0f);
        }

        for (int y = startY; y <= endY; ++y) {
            glm::vec4 worldPos = glm::vec4(0.0f, y * _tileSizeInWorld, 0.0f, 1.0f);
            glm::vec4 ndc = vp * worldPos;
            if (ndc.w != 0.0f) ndc /= ndc.w;
            float screenY = (1.0f - ndc.y) * 0.5f * height;

            drawList->AddLine(ImVec2(0.0f, screenY), ImVec2(width, screenY), IM_COL32(255, 255, 255, 45), 1.0f);
        }
    }

    void LevelEditor::DrawPalette(int tilePixelSize) {
        ImGui::Begin("Tileset Palette");

        int texWidth = _tileset->GetImage()->GetWidth();
        int texHeight = _tileset->GetImage()->GetHeight();
        int columns = texWidth / tilePixelSize;
        int rows = texHeight / tilePixelSize;

        VkDescriptorSet textureID = Engine::Get()->GetRenderContext()->imgui_AddTexture("editor_tileset", _tileset->GetImage()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGuiStyle& style = ImGui::GetStyle();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < columns; x++) {
                ImVec2 uv0 = ImVec2((float)(x * tilePixelSize) / texWidth, (float)(y * tilePixelSize) / texHeight);
                ImVec2 uv1 = ImVec2((float)((x + 1) * tilePixelSize) / texWidth, (float)((y + 1) * tilePixelSize) / texHeight);

                ImGui::PushID(y * columns + x);
                bool isSelected = (_brushTileX == x && _brushTileY == y);
                if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));

                if (ImGui::ImageButton("##tile", textureID, ImVec2(32, 32), uv0, uv1)) {
                    _brushTileX = x;
                    _brushTileY = y;
                    _selectedTileID = (y * columns + x) + 1;
                }

                if (isSelected) ImGui::PopStyleColor();

                float lastButtonX2 = ImGui::GetItemRectMin().x;
                float nextButtonX2 = lastButtonX2 + style.ItemSpacing.x + 32.0f;

                if (x + 1 < columns && nextButtonX2 < windowVisibleX2) ImGui::SameLine();
                ImGui::PopID();
            }
        }
        ImGui::PopStyleVar();
        ImGui::End();
	}

	void LevelEditor::DrawEntitySpawner() {}

	void LevelEditor::HandleDrawAndInput(const glm::mat4 & viewProj) {
        if (ImGui::GetIO().WantCaptureMouse) return;

        auto* input = Engine::Get()->GetInput();
        auto* window = Engine::Get()->GetApplication()->GetWindow();
        bool leftClick = input->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
        bool rightClick = input->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);

        if (leftClick || rightClick) {
            glm::vec2 worldPos = math::ScreenSpaceToWorldSpace(input->GetMousePos().x, input->GetMousePos().y, window->GetSpecs().Width, window->GetSpecs().Height, viewProj);

            int gridX = std::floor(worldPos.x / _tileSizeInWorld);
            int gridY = std::floor(worldPos.y / _tileSizeInWorld);

            if (leftClick && _mode == EditorMode::TilePaint) {
                int texWidth = _tileset->GetImage()->GetWidth();
                int texHeight = _tileset->GetImage()->GetHeight();

                glm::vec2 uvScale = glm::vec2(16.0f / (float)texWidth, 16.0f / (float)texHeight);
                glm::vec2 uvOffset = glm::vec2((float)_brushTileX * uvScale.x, (float)_brushTileY * uvScale.y);

                if (_fastPlace) {
                    Tile newTile;
                    newTile.Position = glm::vec3(gridX, gridY, 0.0f);
                    newTile.Rotation = glm::vec3(0.0f);
                    newTile.Layer = _activeLayer;
                    newTile.UVOffset = uvOffset;
                    newTile.UVScale = uvScale;
                    newTile.TextureID = _tileset->GetBindlessIndex();
                    _tileMapContructor.SetTile(newTile);
                }
                else {
                    _targetTile.Position = glm::vec3(gridX, gridY, 0.0f);
                    _targetTile.Layer = _activeLayer;
                    _targetTile.UVOffset = uvOffset;
                    _targetTile.UVScale = uvScale;
                    _targetTile.TextureID = _tileset->GetBindlessIndex();
                }
            }
            else if (rightClick) {
                Tile emptyTile;
                emptyTile.Position = glm::vec3(gridX, gridY, 0.0f);
                emptyTile.Layer = _activeLayer;
                emptyTile.UVOffset = glm::vec2(0.0f);
                emptyTile.UVScale = glm::vec2(1.0f);
                emptyTile.TextureID = render::ImageHandle::Invalid;

                _tileMapContructor.SetTile(emptyTile);
            }
        }
    }

    void TileMapConstructor::Initialize() {
        _chunks.resize(_mapChunkWidth * _mapChunkHeight);
        for (auto& chunk : _chunks) {
            chunk.IsDirty = true;
            chunk.IndexCount = 0;
            chunk.VBO = nullptr;
            chunk.IBO = nullptr;

            for (uint8_t l = 0; l < (uint8_t)StorageLayer::Count; ++l) {
                for (int i = 0; i < TileMapChunk::CHUNK_SIZE * TileMapChunk::CHUNK_SIZE; ++i) {
                    chunk.Tiles[l][i].TextureID = render::ImageHandle::Invalid;
                }
            }
        }
    }

    void TileMapConstructor::SetTile(const Tile & tile) {
        int arrayX = tile.Position.x + 256;
        int arrayY = tile.Position.y + 256;

        int chunkX = arrayX / TileMapChunk::CHUNK_SIZE;
        int chunkY = arrayY / TileMapChunk::CHUNK_SIZE;

        int localX = arrayX % TileMapChunk::CHUNK_SIZE;
        int localY = arrayY % TileMapChunk::CHUNK_SIZE;

        size_t chunkIndex = chunkY * _mapChunkWidth + chunkX;
        if (chunkIndex >= _chunks.size()) return;

        _chunks[chunkIndex].Tiles[(uint32_t)tile.Layer][localY * TileMapChunk::CHUNK_SIZE + localX] = tile;
        _chunks[chunkIndex].IsDirty = true;
    }

    Tile* TileMapConstructor::GetTile(int x, int y, StorageLayer layer) {
        int arrayX = x + 256;
        int arrayY = y + 256;

        if (arrayX < 0 || arrayX >= (_mapChunkWidth * TileMapChunk::CHUNK_SIZE) ||
            arrayY < 0 || arrayY >= (_mapChunkHeight * TileMapChunk::CHUNK_SIZE)) return nullptr;

        int chunkX = arrayX / TileMapChunk::CHUNK_SIZE;
        int chunkY = arrayY / TileMapChunk::CHUNK_SIZE;

        int localX = arrayX % TileMapChunk::CHUNK_SIZE;
        int localY = arrayY % TileMapChunk::CHUNK_SIZE;

        size_t chunkIndex = chunkY * _mapChunkWidth + chunkX;
        return &_chunks[chunkIndex].Tiles[(uint8_t)layer][localY * TileMapChunk::CHUNK_SIZE + localX];
    }

    void TileMapConstructor::Render(render::RenderContext* context) {
        glm::mat4 iM = glm::mat4(1.0f);

        render::EntityPcData entityPc;
        entityPc.id = _tilesetTextureID;
        entityPc.o = glm::vec2(0.0f);
        entityPc.s = glm::vec2(1.0f);
        entityPc.t = iM;

        context->command_PushConstant("main_pipeline_NAME", VK_SHADER_STAGE_VERTEX_BIT, &entityPc, sizeof(render::EntityPcData));

        for (size_t i = 0; i < _chunks.size(); ++i) {
            int chunkX = i % _mapChunkWidth;
            int chunkY = i / _mapChunkWidth;

            int chunkWorldOriginX = (chunkX * TileMapChunk::CHUNK_SIZE) - 256;
            int chunkWorldOriginY = (chunkY * TileMapChunk::CHUNK_SIZE) - 256;

            if (_chunks[i].IsDirty) {
                _chunks[i].ReconstructMesh(_tileSize, chunkWorldOriginX, chunkWorldOriginY);
                _chunks[i].IsDirty = false;
            }

            if (_chunks[i].IndexCount != 0 && _chunks[i].VBO && _chunks[i].IBO) {
                context->command_DrawIndex("main_pipeline_NAME", _chunks[i].VBO, _chunks[i].IBO, _chunks[i].IndexCount);
            }
        }
    }

    void TileMapConstructor::RenderDebugUI() {
        ImGui::Begin("TileMap Performance Debugger");

        size_t totalVBOBytes = 0;
        size_t totalIBOBytes = 0;
        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;
        uint32_t dirtyChunksCount = 0;
        uint32_t allocatedChunksCount = 0;

        for (const auto& chunk : _chunks) {
            if (chunk.IsDirty) dirtyChunksCount++;
            if (chunk.VBO) {
                allocatedChunksCount++;
                totalVBOBytes += chunk.VBO->GetSize();
            }
            if (chunk.IBO) {
                totalIBOBytes += chunk.IBO->GetSize();
            }
            totalVertices += (chunk.IndexCount / 6) * 4;
            totalIndices += chunk.IndexCount;
        }

        ImGui::Text("Map Dimensions: %d x %d Chunks (%d x %d Tiles)", _mapChunkWidth, _mapChunkHeight, _mapChunkWidth * TileMapChunk::CHUNK_SIZE, _mapChunkHeight * TileMapChunk::CHUNK_SIZE);
        ImGui::Separator();

        ImGui::Text("Allocated Chunks: %u / %zu", allocatedChunksCount, _chunks.size());
        ImGui::Text("Dirty Chunks (Pending Rebuild): %u", dirtyChunksCount);
        ImGui::Text("Total Vertices: %u", totalVertices);
        ImGui::Text("Total Indices:  %u", totalIndices);

        float totalMemoryMb = static_cast<float>(totalVBOBytes + totalIBOBytes) / (1024.0f * 1024.0f);
        ImGui::Text("VRAM Footprint: %.2f MB (VBO: %.2f KB, IBO: %.2f KB)", totalMemoryMb, static_cast<float>(totalVBOBytes) / 1024.0f, static_cast<float>(totalIBOBytes) / 1024.0f);

        ImGui::Separator();
        ImGui::Text("Spatial Chunk Map:");

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));

        for (int y = 0; y < _mapChunkHeight; y++) {
            for (int x = 0; x < _mapChunkWidth; x++) {
                size_t idx = y * _mapChunkWidth + x;
                const auto& chunk = _chunks[idx];

                ImVec4 color = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
                if (chunk.IsDirty) {
                    color = ImVec4(0.8f, 0.1f, 0.1f, 1.0f);
                }
                else if (chunk.IndexCount > 0) {
                    color = ImVec4(0.1f, 0.7f, 0.1f, 1.0f);
                }

                ImGui::PushID(static_cast<int>(idx));
                ImGui::PushStyleColor(ImGuiCol_Button, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);

                ImGui::Button("##chunk", ImVec2(12, 12));

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Chunk ID: Slot [%d, %d] (Index: %zu)", x, y, idx);
                    ImGui::Text("Status: %s", chunk.IsDirty ? "Dirty (Needs Update)" : "Clean");
                    ImGui::Text("Indices: %u (Quads: %u)", chunk.IndexCount, chunk.IndexCount / 6);
                    size_t chunkMem = (chunk.VBO ? chunk.VBO->GetSize() : 0) + (chunk.IBO ? chunk.IBO->GetSize() : 0);
                    ImGui::Text("Allocated VRAM: %.2f KB", static_cast<float>(chunkMem) / 1024.0f);
                    ImGui::EndTooltip();
                }

                ImGui::PopStyleColor(2);
                if (x < _mapChunkWidth - 1) {
                    ImGui::SameLine();
                }
                ImGui::PopID();
            }
        }
        ImGui::PopStyleVar();
        ImGui::End();
    }

    bool TileMapConstructor::SaveMap(const std::string& path) {
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "Failed to open file for saving: " << path << std::endl;
            return false;
        }

        char magic[4] = { 'M', 'A', 'P', '_' };
        out.write(magic, 4);

        uint32_t version = 1;
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&_mapChunkWidth), sizeof(_mapChunkWidth));
        out.write(reinterpret_cast<const char*>(&_mapChunkHeight), sizeof(_mapChunkHeight));

        std::vector<size_t> activeChunkIndices;
        for (size_t i = 0; i < _chunks.size(); ++i) {
            bool hasData = false;
            for (uint8_t l = 0; l < (uint8_t)StorageLayer::Count; ++l) {
                for (int t = 0; t < TileMapChunk::CHUNK_SIZE * TileMapChunk::CHUNK_SIZE; ++t) {
                    if (!_chunks[i].Tiles[l][t].EmptyTile()) {
                        hasData = true;
                        break;
                    }
                }
                if (hasData) break;
            }
            if (hasData) activeChunkIndices.push_back(i);
        }

        uint32_t activeChunkCount = static_cast<uint32_t>(activeChunkIndices.size());
        out.write(reinterpret_cast<const char*>(&activeChunkCount), sizeof(activeChunkCount));

        for (size_t idx : activeChunkIndices) {
            int chunkX = idx % _mapChunkWidth;
            int chunkY = idx / _mapChunkWidth;

            out.write(reinterpret_cast<const char*>(&chunkX), sizeof(chunkX));
            out.write(reinterpret_cast<const char*>(&chunkY), sizeof(chunkY));

            std::vector<std::pair<uint16_t, const Tile*>> activeTiles;
            for (uint8_t l = 0; l < (uint8_t)StorageLayer::Count; ++l) {
                for (int t = 0; t < TileMapChunk::CHUNK_SIZE * TileMapChunk::CHUNK_SIZE; ++t) {
                    const Tile& tile = _chunks[idx].Tiles[l][t];
                    if (!tile.EmptyTile()) {
                        uint16_t localKey = (static_cast<uint16_t>(l) << 12) | static_cast<uint16_t>(t);
                        activeTiles.push_back({ localKey, &tile });
                    }
                }
            }

            uint32_t tileCount = static_cast<uint32_t>(activeTiles.size());
            out.write(reinterpret_cast<const char*>(&tileCount), sizeof(tileCount));

            for (const auto& pair : activeTiles) {
                uint16_t localKey = pair.first;
                const Tile& tile = *pair.second;
                out.write(reinterpret_cast<const char*>(&localKey), sizeof(localKey));
                out.write(reinterpret_cast<const char*>(&tile.Position), sizeof(tile.Position));
                out.write(reinterpret_cast<const char*>(&tile.Rotation), sizeof(tile.Rotation));
                out.write(reinterpret_cast<const char*>(&tile.UVOffset), sizeof(tile.UVOffset));
                out.write(reinterpret_cast<const char*>(&tile.UVScale), sizeof(tile.UVScale));
                out.write(reinterpret_cast<const char*>(&tile.TextureID), sizeof(tile.TextureID));
            }
        }

        out.close();
        return true;
    }

    bool TileMapConstructor::LoadMap(const std::string & path) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            std::cerr << "Failed to open file for loading: " << path << std::endl;
            return false;
        }

        char magic[4];
        in.read(magic, 4);
        if (magic[0] != 'M' || magic[1] != 'P' || magic[2] != 'A' || magic[3] != '_') {
            if (magic[0] != 'M' || magic[1] != 'A' || magic[2] != 'P' || magic[3] != '_') {
                std::cerr << "Invalid map file format signature extension." << std::endl;
                return false;
            }
        }

        uint32_t version = 0;
        in.read(reinterpret_cast<char*>(&version), sizeof(version));

        int newWidth = 0, newHeight = 0;
        in.read(reinterpret_cast<char*>(&newWidth), sizeof(newWidth));
        in.read(reinterpret_cast<char*>(&newHeight), sizeof(newHeight));

        _mapChunkWidth = newWidth;
        _mapChunkHeight = newHeight;
        Initialize();

        uint32_t activeChunkCount = 0;
        in.read(reinterpret_cast<char*>(&activeChunkCount), sizeof(activeChunkCount));

        for (uint32_t i = 0; i < activeChunkCount; ++i) {
            int chunkX = 0, chunkY = 0;
            in.read(reinterpret_cast<char*>(&chunkX), sizeof(chunkX));
            in.read(reinterpret_cast<char*>(&chunkY), sizeof(chunkY));

            size_t chunkIndex = chunkY * _mapChunkWidth + chunkX;
            bool validChunk = (chunkIndex < _chunks.size());

            uint32_t tileCount = 0;
            in.read(reinterpret_cast<char*>(&tileCount), sizeof(tileCount));

            for (uint32_t t = 0; t < tileCount; ++t) {
                uint16_t localKey = 0;
                in.read(reinterpret_cast<char*>(&localKey), sizeof(localKey));

                uint8_t layer = static_cast<uint8_t>(localKey >> 12);
                uint16_t localTileIdx = localKey & 0x0FFF;

                Tile tile;
                in.read(reinterpret_cast<char*>(&tile.Position), sizeof(tile.Position));
                in.read(reinterpret_cast<char*>(&tile.Rotation), sizeof(tile.Rotation));
                in.read(reinterpret_cast<char*>(&tile.UVOffset), sizeof(tile.UVOffset));
                in.read(reinterpret_cast<char*>(&tile.UVScale), sizeof(tile.UVScale));
                in.read(reinterpret_cast<char*>(&tile.TextureID), sizeof(tile.TextureID));
                tile.Layer = static_cast<StorageLayer>(layer);

                if (validChunk && localTileIdx < TileMapChunk::CHUNK_SIZE * TileMapChunk::CHUNK_SIZE) {
                    _chunks[chunkIndex].Tiles[layer][localTileIdx] = tile;
                }
            }

            if (validChunk) {
                _chunks[chunkIndex].IsDirty = true;
            }
        }

        in.close();
        return true;
    }

    void TileMapChunk::ReconstructMesh(float tileSize, int orginX, int orginY) {
        std::vector<render::Vertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t vertexOffset = 0;

        float halfSize = tileSize * 0.5f;

        for (uint8_t l = 0; l < (uint8_t)StorageLayer::Count; ++l) {
            float zDepthOffset = (l == (uint8_t)StorageLayer::Foreground) ? 0.01f : 0.0f;

            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    const Tile& tile = Tiles[l][y * CHUNK_SIZE + x];

                    if (tile.EmptyTile()) continue;

                    int tileWorldX = orginX + x;
                    int tileWorldY = orginY + y;

                    float posX = (tileWorldX * tileSize) + halfSize;
                    float posY = (-tileWorldY * tileSize) - halfSize;
                    float posZ = tile.Position.z + zDepthOffset;

                    glm::vec4 localPoints[4] = {
                        { -halfSize, -halfSize, 0.0f, 1.0f },
                        {  halfSize, -halfSize, 0.0f, 1.0f },
                        {  halfSize,  halfSize, 0.0f, 1.0f },
                        { -halfSize,  halfSize, 0.0f, 1.0f } 
                    };

                    glm::mat4 modelMatrix = glm::mat4(1.0f);

                    modelMatrix = glm::translate(modelMatrix, glm::vec3(posX, posY, posZ));
                    if (tile.Rotation.x != 0.0f) modelMatrix = glm::rotate(modelMatrix, glm::radians(tile.Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                    if (tile.Rotation.y != 0.0f) modelMatrix = glm::rotate(modelMatrix, glm::radians(tile.Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                    if (tile.Rotation.z != 0.0f) modelMatrix = glm::rotate(modelMatrix, glm::radians(tile.Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

                    glm::vec3 p0 = glm::vec3(modelMatrix * localPoints[0]);
                    glm::vec3 p1 = glm::vec3(modelMatrix * localPoints[1]);
                    glm::vec3 p2 = glm::vec3(modelMatrix * localPoints[2]);
                    glm::vec3 p3 = glm::vec3(modelMatrix * localPoints[3]);
                    glm::vec2 uv0 = tile.UVOffset;
                    glm::vec2 uv1 = tile.UVOffset + glm::vec2(tile.UVScale.x, 0.0f);
                    glm::vec2 uv2 = tile.UVOffset + tile.UVScale;
                    glm::vec2 uv3 = tile.UVOffset + glm::vec2(0.0f, tile.UVScale.y);

                    vertices.push_back({ p0, uv0 });
                    vertices.push_back({ p1, uv1 });
                    vertices.push_back({ p2, uv2 });
                    vertices.push_back({ p3, uv3 });
                    indices.push_back(vertexOffset + 0);
                    indices.push_back(vertexOffset + 1);
                    indices.push_back(vertexOffset + 2);
                    indices.push_back(vertexOffset + 2);
                    indices.push_back(vertexOffset + 3);
                    indices.push_back(vertexOffset + 0);
                    vertexOffset += 4;
                }
            }
        }

        IndexCount = static_cast<uint32_t>(indices.size());
        if (IndexCount == 0) return;

        size_t vboSize = sizeof(render::Vertex) * vertices.size();
        size_t iboSize = sizeof(uint32_t) * IndexCount;

        if (VBO == nullptr || VBO->GetSize() < vboSize) {
            VBO = Allocator::AllocRef<render::Buffer>(vboSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);
        }
        VBO->Write(vertices.data(), vboSize);

        if (IBO == nullptr || IBO->GetSize() < iboSize) {
            IBO = Allocator::AllocRef<render::Buffer>(iboSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);
        }
        IBO->Write(indices.data(), iboSize);
    }
}